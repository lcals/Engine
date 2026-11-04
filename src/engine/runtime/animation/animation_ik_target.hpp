#pragma once
#include "runtime/animation/animation_attachment.hpp"
#include "runtime/animation/animation_math.hpp"
#include <algorithm>
#include <tuple>
namespace engine::animation_detail::ik_detail
{
struct Definition
{
    std::string name;
    std::uint16_t order = 0;
    std::size_t index = 0, start = 0, middle = 0, end = 0;
    bool two_bone = false;
};
inline std::size_t find_joint(std::span<const std::string> paths, std::string_view path)
{
    return static_cast<std::size_t>(std::find(paths.begin(), paths.end(), path) - paths.begin());
}
inline bool definitions(const schema::SkeletonDataT& skeleton, std::span<const std::string> paths,
                        std::vector<Definition>& output)
{
    output.clear();
    for (std::size_t index = 0; index < skeleton.aim_ik.size(); ++index)
    {
        const auto& item = *skeleton.aim_ik[index];
        Definition definition{item.name, item.order, index};
        definition.start = find_joint(paths, item.joint_path);
        if (definition.start == paths.size())
        {
            return false;
        }
        output.push_back(std::move(definition));
    }
    for (std::size_t index = 0; index < skeleton.two_bone_ik.size(); ++index)
    {
        const auto& item = *skeleton.two_bone_ik[index];
        Definition definition{item.name, item.order, index};
        definition.two_bone = true;
        definition.start = find_joint(paths, item.start_path);
        definition.middle = find_joint(paths, item.middle_path);
        definition.end = find_joint(paths, item.end_path);
        if (definition.start == paths.size() || definition.middle == paths.size() ||
            definition.end == paths.size())
        {
            return false;
        }
        output.push_back(std::move(definition));
    }
    std::stable_sort(output.begin(), output.end(),
                     [](const auto& a, const auto& b)
                     {
                         return std::tie(a.order, a.two_bone, a.index) <
                                std::tie(b.order, b.two_bone, b.index);
                     });
    return true;
}
inline bool ancestor(const schema::SkeletonDataT& skeleton, std::size_t parent, std::size_t joint)
{
    for (auto current = static_cast<int>(joint); current >= 0;
         current = skeleton.joints[current]->parent)
    {
        if (static_cast<std::size_t>(current) == parent)
        {
            return true;
        }
    }
    return false;
}
inline bool target_values(const AnimationIkTarget& target)
{
    const bool zero_point = target.point.x() == 0 && target.point.y() == 0 && target.point.z() == 0;
    const bool zero_offset = target.local_offset.x() == 0 && target.local_offset.y() == 0 &&
                             target.local_offset.z() == 0;
    return (target.kind == AnimationIkTargetKind::point ||
            target.kind == AnimationIkTargetKind::node) &&
           (target.space == AnimationIkSpace::model || target.space == AnimationIkSpace::world) &&
           model_value_utils::vector(target.point) && model_value_utils::vector(target.pole) &&
           model_value_utils::vector(target.local_offset) && std::isfinite(target.weight) &&
           target.weight >= 0 && target.weight <= 1 &&
           (target.kind == AnimationIkTargetKind::point
                ? !target.target_node && zero_offset
                : static_cast<bool>(target.target_node) && zero_point);
}
inline bool resolve_target(Animator actor, const schema::SkeletonDataT& skeleton,
                           std::span<const std::string> paths,
                           std::span<const Definition> definitions, std::size_t current_definition,
                           const AnimationIkTarget& target,
                           const ozz::math::Float4x4& world_to_model, IkOperation& output)
{
    output.pole = target.space == AnimationIkSpace::world
                      ? ozz::math::TransformVector(world_to_model, simd_point(target.pole))
                      : simd_point(target.pole);
    if (target.pole.x() == 0 && target.pole.y() == 0 && target.pole.z() == 0)
    {
        output.pole = ozz::math::simd_float4::y_axis();
    }
    if (target.kind == AnimationIkTargetKind::point)
    {
        output.target = target.space == AnimationIkSpace::world
                            ? ozz::math::TransformPoint(world_to_model, simd_point(target.point))
                            : simd_point(target.point);
        return true;
    }
    if (!target.target_node || target.target_node.world() != actor.node().world())
    {
        return false;
    }
    std::vector<Node> children;
    BoneAttachment attachment;
    for (auto current = target.target_node; current; current = current.parent())
    {
        auto candidate = current.get_component<BoneAttachment>();
        if (candidate && candidate.active() && candidate.settings().follow_enabled)
        {
            attachment = candidate;
            break;
        }
        children.push_back(current);
    }
    if (attachment)
    {
        Animator source = attachment.runtime()->source;
        std::uint32_t socket_index;
        std::uint64_t generation;
        if ((!source && !resolve_attachment_source(attachment, attachment.settings(), source,
                                                   socket_index, generation)) ||
            source != actor)
        {
            return false;
        }
        // 仅支持当前Actor的直接挂点子树，不递归解另一角色或附件的动画输出。
        for (auto parent = actor.node(); parent; parent = parent.parent())
        {
            const auto nested = parent.get_component<BoneAttachment>();
            if (nested && nested.active() && nested.settings().follow_enabled)
            {
                return false;
            }
        }
        const auto settings = attachment.settings();
        const schema::SkeletonSocketDataT* socket = nullptr;
        for (const auto& candidate : skeleton.sockets)
        {
            if (candidate && candidate->name == settings.socket_name)
            {
                socket = candidate.get();
            }
        }
        if (!socket)
        {
            return false;
        }
        const auto joint = find_joint(paths, socket->joint_path);
        if (joint == paths.size())
        {
            return false;
        }
        // 目标所依赖的骨不能被本约束或其后的约束改写（包含祖先）。
        for (std::size_t index = current_definition; index < definitions.size(); ++index)
        {
            const auto& modified = definitions[index];
            if (ancestor(skeleton, modified.start, joint) ||
                (modified.two_bone && ancestor(skeleton, modified.middle, joint)))
            {
                return false;
            }
        }
        output.attachment_target = true;
        output.attachment_joint = joint;
        output.attachment_offset = pose_matrix(socket->offset) * pose_matrix(settings.offset);
        for (auto child = children.rbegin(); child != children.rend(); ++child)
        {
            auto transform = child->get_component<Transform>();
            if (!transform)
            {
                return false;
            }
            output.attachment_offset =
                output.attachment_offset * authored_matrix(transform.settings());
        }
        output.target = simd_point(target.local_offset);
        return true;
    }
    std::array<float, 16> matrix;
    if (!target.target_node.get_component<Transform>().world_matrix(matrix.data()))
    {
        return false;
    }
    output.target = ozz::math::TransformPoint(world_to_model * load_matrix(matrix),
                                              simd_point(target.local_offset));
    return true;
}
} // namespace engine::animation_detail::ik_detail
