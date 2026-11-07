#pragma once

#include "runtime/schema/animation_generated.h"
#include "runtime/schema/mesh_generated.h"
#include "runtime/schema/skeleton_generated.h"

#include "runtime/resource/codecs/model_value_utils.hpp"
#include "runtime/resource/resource.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace engine::animation_detail
{

inline bool joint_paths(const schema::SkeletonDataT& skeleton, std::vector<std::string>& output)
{
    output.clear();
    if (skeleton.joints.empty() || skeleton.joints.size() > 1024)
    {
        return false;
    }
    std::set<std::string> names;
    for (std::size_t index = 0; index < skeleton.joints.size(); ++index)
    {
        const auto* joint = skeleton.joints[index].get();
        if (joint == nullptr || joint->parent < -1 || joint->parent >= static_cast<int>(index))
        {
            return false;
        }
        std::string path = joint->parent < 0 ? std::string{} : output[joint->parent];
        path += std::to_string(joint->name.size()) + ":" + joint->name;
        if (!names.insert(path).second)
        {
            return false;
        }
        output.push_back(std::move(path));
    }
    return true;
}

inline bool compressible(const schema::Vec3& value, bool scale)
{
    const auto valid = [scale](double scalar)
    {
        return std::isfinite(scalar) && std::abs(scalar) <= 65504.0 &&
               (!scale || std::abs(scalar) >= 0x1p-24);
    };
    return valid(value.x()) && valid(value.y()) && valid(value.z());
}

// 仅校验跨资源绑定，不持有状态，也不依赖 ozz；组件可在提交动作前同步预检。
inline bool valid_binding(const schema::MeshDataT& mesh, const schema::SkeletonDataT& target,
                          const schema::SkeletonDataT& source,
                          const schema::AnimationDataT& animation)
{
    if (mesh.skin.empty() || mesh.skin.size() != mesh.vertices.size() ||
        !model_value_utils::affine(mesh.mesh_to_skeleton))
    {
        return false;
    }
    std::vector<std::string> targets;
    std::vector<std::string> sources;
    if (!joint_paths(target, targets) || !joint_paths(source, sources))
    {
        return false;
    }
    std::map<std::string, std::size_t> names;
    for (std::size_t index = 0; index < sources.size(); ++index)
    {
        names.emplace(sources[index], index);
    }
    std::set<std::size_t> animated;
    for (const auto& track : animation.tracks)
    {
        if (track == nullptr || track->joint >= sources.size() ||
            !animated.insert(track->joint).second)
        {
            return false;
        }
    }
    for (std::size_t index = 0; index < targets.size(); ++index)
    {
        const auto& joint = *target.joints[index];
        const auto found = names.find(targets[index]);
        if (found == names.end() || !model_value_utils::affine(joint.inverse_bind) ||
            (!animated.contains(found->second) &&
             (!compressible(joint.translation, false) || !compressible(joint.scale, true))))
        {
            return false;
        }
    }
    for (const auto& skin : mesh.skin)
    {
        if (skin.j0() >= targets.size() || skin.j1() >= targets.size() ||
            skin.j2() >= targets.size() || skin.j3() >= targets.size())
        {
            return false;
        }
    }
    return true;
}

} // namespace engine::animation_detail
