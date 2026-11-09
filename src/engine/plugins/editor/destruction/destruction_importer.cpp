#include "plugins/editor/destruction/destruction_importer.hpp"
#include "editor/file_text.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"
#include "plugins/editor/destruction/destruction_authoring.hpp"
#include "plugins/editor/destruction/destruction_source.hpp"
#include "runtime/resource/resource_system.hpp"

#include <flatbuffers/flatbuffers.h>

#include <span>
#include <system_error>

namespace engine::plugins::destruction
{
namespace
{

bool source(const std::filesystem::path& path, schema::DestructionGeneratorDataT& settings,
            std::string& error)
{
    std::error_code filesystem_error;
    const auto size = std::filesystem::file_size(path, filesystem_error);
    std::string text;
    if (filesystem_error || size > 1024U * 1024U ||
        !engine::editor::file_detail::read_text(path, text))
    {
        error = "cannot read destruction source, or source exceeds 1 MiB";
        return false;
    }
    return read_destruction_source(text, settings, error);
}

} // namespace

std::string_view DestructionImporter::id() const noexcept
{
    return "destruction";
}

schema::AssetKind DestructionImporter::kind() const noexcept
{
    return schema::AssetKind::destruction;
}

bool DestructionImporter::matches(const std::filesystem::path& path) const
{
    return editor::asset_pipeline::source_extension(path) == ".destruction";
}

bool DestructionImporter::inspect_batch(const editor::asset_pipeline::ImportContext&,
                                        std::string& fingerprint, std::string& error)
{
    if (!editor::asset_pipeline::schema_fingerprint(
            {"editor/destruction_authoring.fbs", "runtime/destruction.fbs", "runtime/mesh.fbs"},
            fingerprint, error))
    {
        return false;
    }
    fingerprint += ":blast-authoring-5.1-generator-2-exact-triangle";
    return true;
}

bool DestructionImporter::scan_dependencies(editor::asset_pipeline::ImportAsset& asset,
                                            const editor::asset_pipeline::ImportContext&,
                                            std::string& error) const
{
    asset.build_dependencies.clear();
    asset.runtime_dependencies.clear();
    schema::DestructionGeneratorDataT settings;
    return source(asset.source, settings, error);
}

bool DestructionImporter::convert(const editor::asset_pipeline::ImportAsset& asset,
                                  const editor::asset_pipeline::ImportContext& context,
                                  std::string& error) const
{
    schema::DestructionGeneratorDataT settings;
    schema::DestructionAssetDataT data;
    if (!source(asset.source, settings, error) ||
        !DestructionAuthoring::generate(settings, data, error))
    {
        return false;
    }
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishDestructionAssetDataBuffer(builder,
                                             schema::DestructionAssetData::Pack(builder, &data));
    return editor::asset_pipeline::write_bytes(
        context.output / asset.guid,
        std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())), error);
}

} // namespace engine::plugins::destruction
