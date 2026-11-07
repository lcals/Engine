#include "runtime/resource/codecs/light_probe_resource_codec.hpp"
#include "runtime/resource/codecs/resource_decode.hpp"

#include <set>

#include <algorithm>
#include <cmath>
#include <utility>

namespace engine
{
bool LightProbeResourceCodec::validate(const schema::LightProbeDataT& data) noexcept
{
    constexpr std::size_t maximum_probes = 262144;
    if (data.schema_version != 1 || !valid_path(data.path) || data.source_fingerprint.empty() ||
        data.source_fingerprint.size() > 256 || data.grid_x < 2 || data.grid_y < 2 ||
        data.grid_z < 2 || data.grid_x > 128 || data.grid_y > 128 || data.grid_z > 128 ||
        data.baked_transform.size() != 16)
    {
        return false;
    }
    const auto count = std::size_t{data.grid_x} * data.grid_y * data.grid_z;
    if (count > maximum_probes || data.valid.size() != count ||
        data.coefficients.size() != count * 9)
    {
        return false;
    }
    const double lo[] = {data.bounds_min.x(), data.bounds_min.y(), data.bounds_min.z()};
    const double hi[] = {data.bounds_max.x(), data.bounds_max.y(), data.bounds_max.z()};
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(lo[axis]) || !std::isfinite(hi[axis]) || lo[axis] >= hi[axis])
        {
            return false;
        }
    }
    if (!std::all_of(data.baked_transform.begin(), data.baked_transform.end(),
                     [](float value) { return std::isfinite(value); }) ||
        !std::all_of(data.valid.begin(), data.valid.end(),
                     [](std::uint8_t value) { return value <= 1; }) ||
        data.baked_transform[3] != 0 || data.baked_transform[7] != 0 ||
        data.baked_transform[11] != 0 || data.baked_transform[15] != 1)
    {
        return false;
    }
    const auto& m = data.baked_transform;
    const double determinant = m[0] * (double{m[5]} * m[10] - double{m[6]} * m[9]) -
                               m[4] * (double{m[1]} * m[10] - double{m[2]} * m[9]) +
                               m[8] * (double{m[1]} * m[6] - double{m[2]} * m[5]);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-12)
    {
        return false;
    }
    return std::all_of(data.coefficients.begin(), data.coefficients.end(),
                       [](const auto& value)
                       {
                           return std::isfinite(value.x()) && std::isfinite(value.y()) &&
                                  std::isfinite(value.z());
                       });
}

schema::AssetKind LightProbeResourceCodec::kind() const noexcept
{
    return schema::AssetKind::light_probe;
}

bool LightProbeResourceCodec::decode(const schema::AssetEntryT& entry, ByteView payload,
                                     std::unique_ptr<Resource>& output, std::string& error) const
{
    const auto validate = [](ByteView payload, std::string& error)
    {
        return resource_decode_detail::verify_flatbuffer(
            payload, schema::VerifyLightProbeDataBuffer, "Invalid LightProbe FlatBuffer", error);
    };
    return resource_decode_detail::decode_flatbuffer<ResourceType>(
        entry, payload, 32U * 1024U * 1024U, validate, [](const std::uint8_t* bytes)
        { return schema::UnPackLightProbeData(bytes); }, output, error);
}

bool LightProbeResourceCodec::validate_data(const Resource& resource, std::string& error) const
{
    const auto* data = resource_detail::data<ResourceType>(&resource);
    if (!data || !(resource.info().dependencies.empty() && data->path == resource.info().path &&
                   validate(*data)))
    {
        error = "Invalid LightProbe data or declared dependencies";
        return false;
    }
    error.clear();
    return true;
}

bool LightProbeResourceCodec::validate_dependencies(const Resource& resource,
                                                    ResourceView dependencies,
                                                    std::string& error) const
{
    error.clear();
    return resource.info().dependencies.empty() && dependencies.empty();
}
} // namespace engine
