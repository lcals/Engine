#include "plugins/runtime/terrain/codecs/terrain_resource_codec.hpp"
#include "plugins/runtime/terrain/terrain_volume.hpp"

#include <flatbuffers/verifier.h>

namespace engine::plugins::terrain
{
schema::AssetKind TerrainResourceCodec::kind() const noexcept
{
    return schema::AssetKind::terrain;
}
bool TerrainResourceCodec::decode(const schema::AssetEntryT& entry, ByteView payload,
                                  std::unique_ptr<Resource>& output, std::string& error) const
{
    error.clear();
    if (output || entry.kind != kind() || payload.empty() || payload.size() > 64U * 1024U * 1024U)
    {
        error = "Invalid resource output, kind or payload size";
        return false;
    }
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
    flatbuffers::Verifier verifier(bytes, payload.size());
    if (!schema::VerifyTerrainAssetDataBuffer(verifier))
    {
        error = "Invalid Terrain FlatBuffer";
        return false;
    }
    auto data = schema::UnPackTerrainAssetData(bytes);
    output = std::make_unique<ResourceType>(entry, std::move(*data));
    return true;
}
bool TerrainResourceCodec::validate_data(const Resource& resource, std::string& error) const
{
    error.clear();
    const auto* data = resource_detail::data<ResourceType>(&resource);
    if (!data || !resource.info().dependencies.empty())
    {
        error = "Invalid terrain type or dependency declaration";
        return false;
    }
    return TerrainVolume::validate(*data, error);
}
bool TerrainResourceCodec::validate_dependencies(const Resource& resource,
                                                 ResourceView dependencies,
                                                 std::string& error) const
{
    error.clear();
    return resource.info().dependencies.empty() && dependencies.empty();
}
} // namespace engine::plugins::terrain
