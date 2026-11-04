#pragma once

#include "runtime/schema/animation_retarget_generated.h"
#include "runtime/schema/skeleton_generated.h"

#include <set>
#include <string>
#include <vector>

namespace engine::retarget_resource_detail
{
inline bool joint_paths(const schema::SkeletonDataT& skeleton, std::set<std::string>& output)
{
    output.clear();
    if (skeleton.joints.empty() || skeleton.joints.size() > 1024)
    {
        return false;
    }
    std::vector<std::string> paths;
    for (std::size_t index = 0; index < skeleton.joints.size(); ++index)
    {
        const auto& joint = skeleton.joints[index];
        if (!joint || joint->name.empty() || joint->name.find('/') != std::string::npos ||
            joint->parent < -1 || joint->parent >= static_cast<int>(index))
        {
            return false;
        }
        paths.push_back(joint->parent < 0 ? joint->name : paths[joint->parent] + "/" + joint->name);
        if (!output.insert(paths.back()).second)
        {
            return false;
        }
    }
    return true;
}

// Only cross-resource shape validation; runtime algorithm capabilities belong to the provider.
inline bool binding_shape(const schema::AnimationRetargetProfileDataT& profile,
                          const schema::SkeletonDataT& source, const schema::SkeletonDataT& target)
{
    if ((!profile.source_reference_pose.empty() &&
         profile.source_reference_pose.size() != source.joints.size()) ||
        (!profile.target_reference_pose.empty() &&
         profile.target_reference_pose.size() != target.joints.size()))
    {
        return false;
    }
    std::set<std::string> source_paths, target_paths;
    if (!joint_paths(source, source_paths) || !joint_paths(target, target_paths))
    {
        return false;
    }
    for (const auto& mapping : profile.joint_mappings)
    {
        if (!mapping || !source_paths.contains(mapping->source_joint_path) ||
            !target_paths.contains(mapping->target_joint_path))
        {
            return false;
        }
    }
    return true;
}
} // namespace engine::retarget_resource_detail
