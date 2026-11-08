#pragma once

#include "runtime/resource/resource_reference.hpp"
#include "runtime/resource/types/animation_resource.hpp"
#include "runtime/resource/types/animation_retarget_profile_resource.hpp"
#include "runtime/resource/types/skeleton_resource.hpp"

#include "runtime/animation/animation_binding.hpp"
#include "runtime/animation/animation_math.hpp"
#include "runtime/animation/animation_retargeter.hpp"
#include "runtime/animation/sampling_data.hpp"

#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>

#include <map>
#include <memory>
#include <tuple>
#include <utility>

namespace engine::animation_detail
{

// 打断时的临时局部姿态，不进入场景数据；由组件保留以支持停用后的恢复。
struct AnimationPose
{
    ResourceReference target;
    std::vector<ozz::math::SoaTransform> locals;
};

inline bool matrix(const std::vector<float>& values, ozz::math::Float4x4& output)
{
    if (!model_value_utils::affine(values))
    {
        return false;
    }
    for (std::size_t index = 0; index < 4; ++index)
    {
        output.cols[index] = ozz::math::simd_float4::LoadPtrU(values.data() + index * 4);
    }
    return true;
}

inline bool inverse(const ozz::math::Float4x4& value, ozz::math::Float4x4& output)
{
    ozz::math::SimdInt4 invertible;
    output = ozz::math::Invert(value, &invertible);
    return ozz::math::AreAllTrue1(invertible);
}

struct CompiledSkeleton
{
    ResourceReference resource;
    const schema::SkeletonDataT* data = nullptr;
    ozz::unique_ptr<ozz::animation::Skeleton> skeleton;
    std::vector<std::size_t> order;
    std::vector<std::string> paths;
    std::vector<std::string> readable_paths;
    std::vector<ozz::math::Float4x4> inverse_bind;

    bool prepare(const ResourceReference& target)
    {
        const auto* data = target ? target.data<SkeletonResource>() : nullptr;
        if (data == nullptr || !joint_paths(*data, paths) ||
            !readable_joint_paths(*data, readable_paths))
        {
            return false;
        }
        const auto count = data->joints.size();
        if (data->sampling_encoding == schema::SamplingEncoding::ozz_archive)
        {
            skeleton = ozz::make_unique<ozz::animation::Skeleton>();
            order.assign(data->sampling_joint_order.begin(), data->sampling_joint_order.end());
            if (!read_sampling(data->sampling_archive, *skeleton))
            {
                return false;
            }
        }
        else if (!build_skeleton(*data, skeleton, order))
        {
            return false;
        }
        if (!skeleton || static_cast<std::size_t>(skeleton->num_joints()) != count)
        {
            return false;
        }
        inverse_bind.resize(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            if (!matrix(data->joints[index]->inverse_bind, inverse_bind[index]))
            {
                return false;
            }
        }
        if (!resource.retain(target))
        {
            return false;
        }
        this->data = target.data<SkeletonResource>();
        return true;
    }
};

struct CompiledAnimation
{
    ResourceReference resource;
    ResourceReference source;
    std::shared_ptr<const CompiledSkeleton> target;
    std::shared_ptr<const CompiledSkeleton> sampling;
    std::shared_ptr<const AnimationRetargetBinding> retarget;
    std::shared_ptr<const ozz::animation::Animation> animation;
    std::vector<std::size_t> source_dfs;
    std::vector<std::uint8_t> channels;

    bool prepare(const ResourceReference& clip, const ResourceReference& source_resource,
                 std::shared_ptr<const CompiledSkeleton> skeleton,
                 std::shared_ptr<const CompiledSkeleton> sampling_skeleton,
                 std::shared_ptr<const AnimationRetargetBinding> binding,
                 std::shared_ptr<const ozz::animation::Animation> compiled)
    {
        const auto* data = clip.data<AnimationResource>();
        if (!data || !skeleton || !sampling_skeleton || !compiled || !resource.retain(clip) ||
            !source.retain(source_resource))
        {
            return false;
        }
        target = std::move(skeleton);
        sampling = std::move(sampling_skeleton);
        retarget = std::move(binding);
        animation = std::move(compiled);
        channels.resize(sampling->order.size());
        for (const auto& track : data->tracks)
        {
            if (!track || track->joint >= channels.size())
            {
                return false;
            }
            channels[track->joint] = (!track->translations.empty() ? 1 : 0) |
                                     (!track->rotations.empty() ? 2 : 0) |
                                     (!track->scales.empty() ? 4 : 0);
        }
        if (!retarget)
        {
            std::map<std::string, std::size_t, std::less<>> indices;
            for (std::size_t dfs = 0; dfs < sampling->order.size(); ++dfs)
            {
                indices.emplace(sampling->paths[sampling->order[dfs]], dfs);
            }
            for (auto index : target->order)
            {
                const auto found = indices.find(target->paths[index]);
                if (found == indices.end())
                {
                    return false;
                }
                source_dfs.push_back(found->second);
            }
        }
        return true;
    }
};

using ResourceIdentity = std::pair<std::string, std::uint64_t>;
using AnimationIdentity =
    std::tuple<ResourceIdentity, ResourceIdentity, ResourceIdentity, ResourceIdentity>;

inline ResourceIdentity identity(const Resource& resource)
{
    return {resource.info().guid, resource.generation()};
}

} // namespace engine::animation_detail
