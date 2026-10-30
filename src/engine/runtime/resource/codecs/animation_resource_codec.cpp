#include "runtime/resource/codecs/animation_resource_codec.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/resource/codecs/model_value_utils.hpp"

#include <flatbuffers/verifier.h>
#include <limits>
#include <set>
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
bool AnimationResourceCodec::decode(const ResourceCatalog& catalog, ByteView payload,
                                    Resource& output) const
{
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
    flatbuffers::Verifier verifier(bytes, payload.size());
    if (!schema::VerifyAnimationDataBuffer(verifier))
    {
        return false;
    }
    auto data = schema::UnPackAnimationData(bytes);
    if (data->path != output.info().path || !validate(*data) ||
        output.info().dependencies.size() != 1 ||
        !declares(catalog, output, data->skeleton_guid, schema::AssetKind::skeleton))
    {
        return false;
    }
    store(output, std::move(data));
    return true;
}
bool AnimationResourceCodec::validate_dependencies(const ResourceCatalog&, const Resource& resource,
                                                   std::span<const ResourcePtr> dependencies) const
{
    const auto* data = resource.animation();
    const auto* skeleton = data ? dependency(dependencies, data->skeleton_guid) : nullptr;
    if (!skeleton || !skeleton->skeleton())
    {
        return false;
    }
    for (const auto& track : data->tracks)
    {
        if (track->joint >= skeleton->skeleton()->joints.size())
        {
            return false;
        }
    }
    return true;
}
} // namespace engine
