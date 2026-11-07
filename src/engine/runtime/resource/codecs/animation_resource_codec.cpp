#include "runtime/resource/codecs/animation_resource_codec.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/resource/codecs/model_value_utils.hpp"
#include "runtime/resource/types/skeleton_resource.hpp"
#include <set>

#include <flatbuffers/verifier.h>
#include <limits>
#include <utility>

namespace engine
{
namespace
{
bool animation_vector(const schema::Vec3& value)
{
    // ozz 的平移与缩放关键帧使用 IEEE float16 保存。
    constexpr double maximum = 65504.0;
    return model_value_utils::vector(value) && std::abs(value.x()) <= maximum &&
           std::abs(value.y()) <= maximum && std::abs(value.z()) <= maximum;
}

bool animation_scale(const schema::Vec3& value)
{
    constexpr double minimum = 0x1p-24;
    return animation_vector(value) && std::abs(value.x()) >= minimum &&
           std::abs(value.y()) >= minimum && std::abs(value.z()) >= minimum;
}

template <typename Key, typename Predicate>
bool keys(const std::vector<Key>& values, float duration, Predicate validate)
{
    if (values.empty() || values.size() > 1000000)
    {
        return false;
    }
    float previous = -1.0F;
    for (const auto& key : values)
    {
        if (!std::isfinite(key.time()) || key.time() < 0.0F || key.time() > duration ||
            key.time() <= previous || !validate(key.value()))
        {
            return false;
        }
        previous = key.time();
    }
    return true;
}
} // namespace
bool AnimationResourceCodec::validate(const schema::AnimationDataT& data)
{
    if (data.schema_version != 1 || !valid_path(data.path) || data.name.empty() ||
        !GuidMap::valid_guid(data.skeleton_guid) || !std::isfinite(data.duration) ||
        data.duration <= 0.0F || !std::isfinite(1.0F / data.duration) || data.duration > 86400.0F ||
        data.tracks.empty() || data.tracks.size() > 1024)
    {
        return false;
    }
    std::set<std::uint32_t> joints;
    if (data.events.size() > 4096)
    {
        return false;
    }
    std::set<std::uint32_t> marker_ids;
    for (const auto& marker : data.events)
    {
        if (!marker || marker->marker_id == 0 || marker->name.empty() ||
            !std::isfinite(marker->time) || marker->time < 0.0F || marker->time > data.duration ||
            !marker_ids.insert(marker->marker_id).second)
        {
            return false;
        }
        switch (marker->payload.type)
        {
        case schema::AnimationEventPayload::NONE:
            if (marker->payload.value != nullptr)
            {
                return false;
            }
            break;
        case schema::AnimationEventPayload::AnimationEventBoolean:
            if (!marker->payload.AsAnimationEventBoolean())
            {
                return false;
            }
            break;
        case schema::AnimationEventPayload::AnimationEventNumber:
            if (!marker->payload.AsAnimationEventNumber() ||
                !std::isfinite(marker->payload.AsAnimationEventNumber()->value))
            {
                return false;
            }
            break;
        case schema::AnimationEventPayload::AnimationEventString:
            if (!marker->payload.AsAnimationEventString())
            {
                return false;
            }
            break;
        default:
            return false;
        }
    }
    // ozz 运行对象以 uint16 索引唯一时间点，首尾时间也由 Builder 使用。
    std::set<float> times{0.0F, data.duration};
    for (const auto& track : data.tracks)
    {
        if (!track || track->joint >= 1024 || !joints.insert(track->joint).second ||
            !keys(track->translations, data.duration, animation_vector) ||
            !keys(track->rotations, data.duration, model_value_utils::quaternion) ||
            !keys(track->scales, data.duration, animation_scale))
        {
            return false;
        }
        const auto record_times = [&times](const auto& keys)
        {
            for (const auto& key : keys)
            {
                times.insert(key.time());
                if (times.size() > std::numeric_limits<std::uint16_t>::max())
                {
                    return false;
                }
            }
            return true;
        };
        if (!record_times(track->translations) || !record_times(track->rotations) ||
            !record_times(track->scales))
        {
            return false;
        }
    }
    return true;
}
schema::AssetKind AnimationResourceCodec::kind() const noexcept
{
    return schema::AssetKind::animation;
}

bool AnimationResourceCodec::decode(const schema::AssetEntryT& entry, ByteView payload,
                                    std::unique_ptr<Resource>& output, std::string& error) const
{
    if (output)
    {
        error = "Resource output must be empty";
        return false;
    }
    error.clear();
    if (entry.kind != kind() || payload.empty() || payload.size() > 64U * 1024U * 1024U)
    {
        error = "Invalid resource kind or payload size";
        return false;
    }
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
    flatbuffers::Verifier verifier(bytes, payload.size());
    if (!schema::VerifyAnimationDataBuffer(verifier))
    {
        error = "Invalid Animation FlatBuffer";
        return false;
    }
    auto data = schema::UnPackAnimationData(bytes);
    output = std::make_unique<ResourceType>(entry, std::move(*data));
    return true;
}

bool AnimationResourceCodec::validate_data(const Resource& resource, std::string& error) const
{
    const auto* data = resource_detail::data<ResourceType>(&resource);
    if (!data ||
        !(data->path == resource.info().path && validate(*data) &&
          resource.info().dependencies.size() == 1 && declares(resource, data->skeleton_guid)))
    {
        error = "Invalid Animation data or declared dependencies";
        return false;
    }
    error.clear();
    return true;
}

bool AnimationResourceCodec::validate_dependencies(const Resource& resource,
                                                   ResourceView dependencies,
                                                   std::string& error) const
{
    const auto* data = resource_detail::data<AnimationResource>(&resource);
    const auto* skeleton = data ? dependency(dependencies, data->skeleton_guid) : nullptr;
    if (!skeleton || !resource_detail::data<SkeletonResource>(skeleton))
    {
        return false;
    }
    for (const auto& track : data->tracks)
    {
        if (track->joint >= resource_detail::data<SkeletonResource>(skeleton)->joints.size())
        {
            return false;
        }
    }
    return true;
}
} // namespace engine
