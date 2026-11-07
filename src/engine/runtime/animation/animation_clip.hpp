#pragma once

#include "runtime/resource/resource_reference.hpp"
#include "runtime/resource/types/animation_resource.hpp"
#include "runtime/resource/types/animation_retarget_profile_resource.hpp"
#include "runtime/resource/types/skeleton_resource.hpp"

#include "runtime/animation/animation_binding.hpp"
#include "runtime/animation/animation_math.hpp"
#include "runtime/animation/animation_retargeter.hpp"

#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
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
    resource_detail::ResourceReference target;
    std::vector<ozz::math::SoaTransform> locals;
};

inline ozz::math::Float3 vector(const schema::Vec3& value)
{
    return {static_cast<float>(value.x()), static_cast<float>(value.y()),
            static_cast<float>(value.z())};
}

inline ozz::math::Quaternion quaternion(const schema::Vec4& value)
{
    return {static_cast<float>(value.x()), static_cast<float>(value.y()),
            static_cast<float>(value.z()), static_cast<float>(value.w())};
}

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

inline void build_joint(const schema::SkeletonDataT& data, std::size_t index,
                        const std::vector<std::vector<std::size_t>>& children,
                        ozz::animation::offline::RawSkeleton::Joint& output,
                        std::vector<std::size_t>& order)
{
    const auto& joint = *data.joints[index];
    order.push_back(index);
    output.name = joint.name.c_str();
    output.transform.translation = vector(joint.translation);
    output.transform.rotation = quaternion(joint.rotation);
    output.transform.scale = vector(joint.scale);
    output.children.resize(children[index].size());
    for (std::size_t child = 0; child < children[index].size(); ++child)
    {
        build_joint(data, children[index][child], children, output.children[child], order);
    }
}

struct CompiledSkeleton
{
    resource_detail::ResourceReference resource;
    const schema::SkeletonDataT* data = nullptr;
    ozz::unique_ptr<ozz::animation::Skeleton> skeleton;
    std::vector<std::size_t> order;
    std::vector<std::string> paths;
    std::vector<std::string> readable_paths;
    std::vector<ozz::math::Float4x4> inverse_bind;

    bool prepare(const resource_detail::ResourceReference& target)
    {
        const auto* data = target ? target.data<SkeletonResource>() : nullptr;
        if (data == nullptr || !joint_paths(*data, paths) ||
            !readable_joint_paths(*data, readable_paths))
        {
            return false;
        }
        const auto count = data->joints.size();
        std::vector<std::vector<std::size_t>> children(count);
        std::vector<std::size_t> roots;
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto parent = data->joints[index]->parent;
            if (parent < 0)
            {
                roots.push_back(index);
            }
            else
            {
                children[parent].push_back(index);
            }
        }
        ozz::animation::offline::RawSkeleton raw;
        raw.roots.resize(roots.size());
        for (std::size_t index = 0; index < roots.size(); ++index)
        {
            build_joint(*data, roots[index], children, raw.roots[index], order);
        }
        skeleton = ozz::animation::offline::SkeletonBuilder{}(raw);
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
    resource_detail::ResourceReference resource;
    resource_detail::ResourceReference source;
    std::shared_ptr<const CompiledSkeleton> target;
    std::shared_ptr<const CompiledSkeleton> sampling;
    std::shared_ptr<const AnimationRetargetBinding> retarget;
    ozz::unique_ptr<ozz::animation::Animation> animation;

    bool prepare(const resource_detail::ResourceReference& clip,
                 const resource_detail::ResourceReference& source_resource,
                 std::shared_ptr<const CompiledSkeleton> skeleton,
                 std::shared_ptr<const CompiledSkeleton> sampling_skeleton = nullptr,
                 std::shared_ptr<const AnimationRetargetBinding> binding = nullptr)
    {
        const auto final_target = skeleton;
        if (sampling_skeleton)
        {
            skeleton = sampling_skeleton;
        }
        const auto* data = clip ? clip.data<AnimationResource>() : nullptr;
        const auto* source_data =
            source_resource ? source_resource.data<SkeletonResource>() : nullptr;
        if (data == nullptr || source_data == nullptr || !skeleton)
        {
            return false;
        }
        std::vector<std::string> source_paths;
        if (!joint_paths(*source_data, source_paths))
        {
            return false;
        }
        std::map<std::string, std::size_t> source_indices;
        for (std::size_t index = 0; index < source_paths.size(); ++index)
        {
            source_indices.emplace(source_paths[index], index);
        }
        std::vector<const schema::AnimationTrackDataT*> tracks(source_paths.size(), nullptr);
        for (const auto& track : data->tracks)
        {
            if (!track || track->joint >= tracks.size() || tracks[track->joint] != nullptr)
            {
                return false;
            }
            tracks[track->joint] = track.get();
        }
        ozz::animation::offline::RawAnimation raw;
        raw.name = data->name.c_str();
        raw.duration = data->duration;
        raw.tracks.resize(skeleton->order.size());
        for (std::size_t dfs = 0; dfs < skeleton->order.size(); ++dfs)
        {
            const auto target_index = skeleton->order[dfs];
            const auto found = source_indices.find(skeleton->paths[target_index]);
            if (found == source_indices.end())
            {
                return false;
            }
            const auto* track = tracks[found->second];
            const auto& rest = *skeleton->data->joints[target_index];
            auto& result = raw.tracks[dfs];
            if (track != nullptr)
            {
                for (const auto& key : track->translations)
                {
                    result.translations.push_back({key.time(), vector(key.value())});
                }
                for (const auto& key : track->rotations)
                {
                    result.rotations.push_back({key.time(), quaternion(key.value())});
                }
                for (const auto& key : track->scales)
                {
                    result.scales.push_back({key.time(), vector(key.value())});
                }
            }
            if (result.translations.empty())
            {
                if (!compressible(rest.translation, false))
                {
                    return false;
                }
                result.translations.push_back({0.0F, vector(rest.translation)});
            }
            if (result.rotations.empty())
            {
                result.rotations.push_back({0.0F, quaternion(rest.rotation)});
            }
            if (result.scales.empty())
            {
                if (!compressible(rest.scale, true))
                {
                    return false;
                }
                result.scales.push_back({0.0F, vector(rest.scale)});
            }
        }
        animation = ozz::animation::offline::AnimationBuilder{}(raw);
        if (!animation || animation->num_tracks() != skeleton->skeleton->num_joints())
        {
            return false;
        }
        if (!resource.retain(clip) || !source.retain(source_resource))
        {
            return false;
        }
        target = final_target;
        sampling = std::move(skeleton);
        retarget = std::move(binding);
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
