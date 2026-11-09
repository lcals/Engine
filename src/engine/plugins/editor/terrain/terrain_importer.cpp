#include "plugins/editor/terrain/terrain_importer.hpp"
#include "editor/file_text.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"
#include "plugins/editor/terrain/terrain_source.hpp"
#include "plugins/runtime/terrain/terrain_volume.hpp"
#include "runtime/resource/resource_system.hpp"

#include <flatbuffers/flatbuffers.h>

#include <span>
#include <system_error>

namespace engine::plugins::terrain
{
namespace
{

bool source(const std::filesystem::path& path, schema::TerrainGeneratorDataT& settings,
            std::string& error)
{
    std::error_code filesystem_error;
    const auto size = std::filesystem::file_size(path, filesystem_error);
    std::string text;
    if (filesystem_error || size > 1024U * 1024U ||
        !engine::editor::file_detail::read_text(path, text))
    {
        error = "cannot read terrain source, or source exceeds 1 MiB";
        return false;
    }
    return read_terrain_source(text, settings, error);
}

} // namespace

std::string_view TerrainImporter::id() const noexcept
{
    return "terrain";
}

schema::AssetKind TerrainImporter::kind() const noexcept
{
    return schema::AssetKind::terrain;
}

bool TerrainImporter::matches(const std::filesystem::path& path) const
{
    return editor::asset_pipeline::source_extension(path) == ".terrain";
}

bool TerrainImporter::inspect_batch(const editor::asset_pipeline::ImportContext&,
                                    std::string& fingerprint, std::string& error)
{
    if (!editor::asset_pipeline::schema_fingerprint({"runtime/terrain.fbs"}, fingerprint, error))
    {
        return false;
    }
    fingerprint += ":terrain-generator-1";
    return true;
}

bool TerrainImporter::scan_dependencies(editor::asset_pipeline::ImportAsset& asset,
                                        const editor::asset_pipeline::ImportContext&,
                                        std::string& error) const
{
    asset.build_dependencies.clear();
    asset.runtime_dependencies.clear();
    schema::TerrainGeneratorDataT settings;
    return source(asset.source, settings, error);
}

bool TerrainImporter::convert(const editor::asset_pipeline::ImportAsset& asset,
                              const editor::asset_pipeline::ImportContext& context,
                              std::string& error) const
{
    schema::TerrainGeneratorDataT settings;
    schema::TerrainAssetDataT data;
    if (!source(asset.source, settings, error) || !TerrainVolume::generate(settings, data, error))
    {
        return false;
    }
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishTerrainAssetDataBuffer(builder, schema::TerrainAssetData::Pack(builder, &data));
    return editor::asset_pipeline::write_bytes(
        context.output / asset.guid,
        std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())), error);
}

} // namespace engine::plugins::terrain
