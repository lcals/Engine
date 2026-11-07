#pragma once
#include "runtime/animation/animation_binding.hpp"
#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>
#include <ozz/base/memory/unique_ptr.h>

namespace engine::animation_detail
{
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

inline bool build_skeleton(const schema::SkeletonDataT& data,
                           ozz::unique_ptr<ozz::animation::Skeleton>& output,
                           std::vector<std::size_t>& order)
{
    const auto count = data.joints.size();
    std::vector<std::vector<std::size_t>> children(count);
    std::vector<std::size_t> roots;
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto parent = data.joints[index]->parent;
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
    order.clear();
    for (std::size_t index = 0; index < roots.size(); ++index)
    {
        build_joint(data, roots[index], children, raw.roots[index], order);
    }
    output = ozz::animation::offline::SkeletonBuilder{}(raw);
    return output && static_cast<std::size_t>(output->num_joints()) == count;
}

inline bool build_animation(const schema::AnimationDataT& data,
                            const schema::SkeletonDataT& skeleton,
                            const std::vector<std::size_t>& order,
                            ozz::unique_ptr<ozz::animation::Animation>& output)
{
    std::vector<const schema::AnimationTrackDataT*> tracks(skeleton.joints.size());
    for (const auto& track : data.tracks)
    {
        if (!track || track->joint >= tracks.size() || tracks[track->joint])
        {
            return false;
        }
        tracks[track->joint] = track.get();
    }
    ozz::animation::offline::RawAnimation raw;
    raw.name = data.name.c_str();
    raw.duration = data.duration;
    raw.tracks.resize(order.size());
    for (std::size_t dfs = 0; dfs < order.size(); ++dfs)
    {
        const auto index = order[dfs];
        const auto& rest = *skeleton.joints[index];
        auto& result = raw.tracks[dfs];
        if (const auto* track = tracks[index])
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
            result.translations.push_back({0, vector(rest.translation)});
        }
        if (result.rotations.empty())
        {
            result.rotations.push_back({0, quaternion(rest.rotation)});
        }
        if (result.scales.empty())
        {
            if (!compressible(rest.scale, true))
            {
                return false;
            }
            result.scales.push_back({0, vector(rest.scale)});
        }
    }
    output = ozz::animation::offline::AnimationBuilder{}(raw);
    return output && output->num_tracks() == static_cast<int>(order.size());
}

// 仅接受已经由资源 codec 验证的当前版本归档。
template <class T> bool read_sampling(const std::vector<std::uint8_t>& bytes, T& output)
{
    ozz::io::MemoryStream stream;
    if (stream.Write(bytes.data(), bytes.size()) != bytes.size() ||
        stream.Seek(0, ozz::io::Stream::kSet) != 0)
    {
        return false;
    }
    ozz::io::IArchive archive(&stream);
    if (!archive.TestTag<T>())
    {
        return false;
    }
    archive >> output;
    return static_cast<std::size_t>(stream.Tell()) == bytes.size();
}
} // namespace engine::animation_detail
