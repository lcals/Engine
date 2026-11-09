#include "editor/asset_pipeline/importers/prefab_importer.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"
#include "editor/asset_pipeline/prefab_composition.hpp"
#include "editor/asset_pipeline/prefab_importer.hpp"

#include <algorithm>
#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

namespace engine::editor::asset_pipeline
{
schema::AssetKind PrefabAssetImporter::kind() const noexcept
{
    return schema::AssetKind::prefab;
}

bool PrefabAssetImporter::matches(const std::filesystem::path& source) const
{
    return source_extension(source) == ".prefab";
}

bool PrefabAssetImporter::inspect_batch(const ImportContext&, std::string& fingerprint,
                                        std::string& error)
{
    return schema_fingerprint({"runtime/scene.fbs", "runtime/prefab.fbs", "runtime/model.fbs",
                               "runtime/camera.fbs", "runtime/character_controller.fbs"},
                              fingerprint, error);
}

bool PrefabAssetImporter::scan_dependencies(ImportAsset& asset, const ImportContext& context,
                                            std::string& error) const
{
    asset.build_dependencies.clear();
    asset.runtime_dependencies.clear();
    schema::PrefabDataT prefab;
    if (!PrefabImporter::read(asset.source, prefab, error))
    {
        return false;
    }
    if (!prefab.model_guid.empty())
    {
        Guid material;
        if (!reference(prefab.model_guid, schema::AssetKind::model, context,
                       asset.build_dependencies, error) ||
            !context.identities.path_to_guid("builtin://materials/default-pbr", material) ||
            !reference(material, schema::AssetKind::material, context, asset.build_dependencies,
                       error))
        {
            if (error.empty())
            {
                error = "Prefab default material identity is unavailable";
            }
            return false;
        }
    }
    if (!scan_node_dependencies(prefab.nodes, context, asset.build_dependencies, error) ||
        !scan_override_dependencies(prefab.model_overrides, context, asset.build_dependencies,
                                    error))
    {
        return false;
    }
    std::sort(asset.build_dependencies.begin(), asset.build_dependencies.end());
    return true;
}

bool PrefabAssetImporter::convert(const ImportAsset& asset, const ImportContext& context,
                                  std::string& error) const
{
    schema::PrefabDataT prefab;
    if (!PrefabImporter::read(asset.source, prefab, error))
    {
        return false;
    }
    if (!prefab.model_guid.empty())
    {
        std::vector<std::byte> payload;
        if (!read_generated_payload(prefab.model_guid, context, payload, error))
        {
            return false;
        }
        flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(payload.data()),
                                       payload.size());
        if (!schema::VerifyModelDataBuffer(verifier))
        {
            error = "Invalid generated Model for prefab";
            return false;
        }
        const auto model = schema::UnPackModelData(payload.data());
        Guid material;
        if (!context.identities.path_to_guid("builtin://materials/default-pbr", material) ||
            !PrefabImporter::from_model(*model, material, prefab, error))
        {
            if (error.empty())
            {
                error = "Prefab default material identity is unavailable";
            }
            return false;
        }
    }
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishPrefabDataBuffer(builder, schema::PrefabData::Pack(builder, &prefab));
    return write_bytes(context.output / asset.guid,
                       std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())),
                       error);
}

bool PrefabAssetImporter::finalize_dependencies(ImportAsset& asset, const ImportContext& context,
                                                std::string& error) const
{
    std::vector<std::byte> payload;
    if (!read_generated_payload(asset.guid, context, payload, error))
    {
        return false;
    }
    flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(payload.data()),
                                   payload.size());
    if (!schema::VerifyPrefabDataBuffer(verifier))
    {
        error = "Invalid generated prefab";
        return false;
    }
    const auto prefab = schema::UnPackPrefabData(payload.data());
    asset.runtime_dependencies.clear();
    return scan_node_dependencies(prefab->nodes, context, asset.runtime_dependencies, error);
}
} // namespace engine::editor::asset_pipeline
