#include "runtime/animation/animation_ik.hpp"

#include "runtime/animation/animation_clip.hpp"
#include "runtime/animation/animation_ik_target.hpp"
#include "runtime/animation/animation_math.hpp"

#include <ozz/animation/runtime/ik_aim_job.h>
#include <ozz/animation/runtime/ik_two_bone_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/base/maths/simd_quaternion.h>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace engine::animation_detail
{
using namespace ik_detail;
namespace
{
void correct_joint(std::vector<ozz::math::SoaTransform>& locals, std::size_t dfs,
                   const ozz::math::SimdQuaternion& correction)
{
    auto pose = local_pose(locals[dfs / 4], dfs % 4);
    ozz::math::SimdQuaternion current{ozz::math::simd_float4::Load(
        static_cast<float>(pose.rotation().x()), static_cast<float>(pose.rotation().y()),
        static_cast<float>(pose.rotation().z()), static_cast<float>(pose.rotation().w()))};
    const auto result = ozz::math::Normalize(current * correction);
    float quaternion[4];
    ozz::math::StorePtrU(result.xyzw, quaternion);
    pose = schema::JointLocalPose(pose.translation(),
                                  Vec4(quaternion[0], quaternion[1], quaternion[2], quaternion[3]),
                                  pose.scale());
    set_local_pose(locals[dfs / 4], dfs % 4, pose);
}
bool fk(const CompiledSkeleton& skeleton, std::vector<ozz::math::SoaTransform>& locals,
        std::vector<ozz::math::Float4x4>& models)
{
    ozz::animation::LocalToModelJob job;
    job.skeleton = skeleton.skeleton.get();
    job.input = ozz::make_span(locals);
    job.output = ozz::make_span(models);
    return job.Run();
}
} // namespace
bool collect_ik_operations(Animator actor, const CompiledSkeleton& skeleton,
                           const ozz::math::Float4x4& model_to_world,
                           std::vector<IkOperation>& output)
{
    output.clear();
    if (!actor.settings().ik_enabled)
    {
        return true;
    }
    ozz::math::Float4x4 world_to_model;
    if (!invert_matrix(model_to_world, world_to_model))
    {
        return false;
    }
    const auto& data = *skeleton.resource->skeleton();
    std::vector<Definition> resolved;
    if (!definitions(data, skeleton.readable_paths, resolved))
    {
        return false;
    }
    std::vector<std::size_t> dfs(skeleton.order.size());
    for (std::size_t index = 0; index < skeleton.order.size(); ++index)
    {
        dfs[skeleton.order[index]] = index;
    }
    for (std::size_t index = 0; index < resolved.size(); ++index)
    {
        const auto& definition = resolved[index];
        const auto found = actor.runtime()->ik_targets_.find(definition.name);
        if (found == actor.runtime()->ik_targets_.end() || !found->second.enabled ||
            found->second.weight == 0)
        {
            continue;
        }
        const auto& target = found->second;
        if (!target_values(target))
        {
            continue; // 删除/换World的目标不会复用旧坐标。
        }
        IkOperation operation;
        if (!resolve_target(actor, data, skeleton.readable_paths, resolved, index, target,
                            world_to_model, operation))
        {
            return false;
        }
        operation.start = dfs[definition.start];
        operation.middle = dfs[definition.middle];
        operation.end = dfs[definition.end];
        if (operation.attachment_target)
        {
            operation.attachment_joint = dfs[operation.attachment_joint];
        }
        operation.weight = target.weight;
        operation.two_bone = definition.two_bone;
        if (definition.two_bone)
        {
            const auto& source = *data.two_bone_ik[definition.index];
            operation.mid_axis = simd_point(source.mid_axis_joint);
            operation.soften = source.soften;
            operation.twist = source.twist_radians;
        }
        else
        {
            const auto& source = *data.aim_ik[definition.index];
            operation.forward = simd_point(source.forward_joint);
            operation.up = simd_point(source.up_joint);
            operation.offset = simd_point(source.offset_joint);
        }
        output.push_back(operation);
    }
    return true;
}
bool apply_ik(const CompiledSkeleton& skeleton, std::span<const IkOperation> operations,
              std::vector<ozz::math::SoaTransform>& locals,
              std::vector<ozz::math::Float4x4>& models)
{
    for (const auto& operation : operations)
    {
        const auto target = operation.attachment_target
                                ? ozz::math::TransformPoint(models[operation.attachment_joint] *
                                                                operation.attachment_offset,
                                                            operation.target)
                                : operation.target;
        if (operation.two_bone)
        {
            ozz::animation::IKTwoBoneJob job;
            ozz::math::SimdQuaternion start, middle;
            job.target = target;
            job.pole_vector = operation.pole;
            job.mid_axis = operation.mid_axis;
            job.weight = operation.weight;
            job.soften = operation.soften;
            job.twist_angle = operation.twist;
            job.start_joint = &models[operation.start];
            job.mid_joint = &models[operation.middle];
            job.end_joint = &models[operation.end];
            job.start_joint_correction = &start;
            job.mid_joint_correction = &middle;
            if (!job.Run())
            {
                return false;
            }
            correct_joint(locals, operation.start, start);
            correct_joint(locals, operation.middle, middle);
        }
        else
        {
            ozz::animation::IKAimJob job;
            ozz::math::SimdQuaternion correction;
            job.target = target;
            job.pole_vector = operation.pole;
            job.forward = operation.forward;
            job.up = operation.up;
            job.offset = operation.offset;
            job.weight = operation.weight;
            job.joint = &models[operation.start];
            job.joint_correction = &correction;
            if (!job.Run())
            {
                return false;
            }
            correct_joint(locals, operation.start, correction);
        }
        if (!fk(skeleton, locals, models))
        {
            return false;
        }
    }
    return true;
}
} // namespace engine::animation_detail
