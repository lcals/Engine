#include "editor/asset_pipeline/importers/scene_importer.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"
#include "editor/asset_pipeline/prefab_composition.hpp"
#include "editor/asset_pipeline/scene_importer.hpp"
#include "runtime/resource/resource_system.hpp"

#include <algorithm>
#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>
#include <span>

namespace engine::editor::asset_pipeline
{
namespace
{
bool retain_generated_mappings(const ImportAsset& asset, const ImportContext& context,
                               schema::SceneDataT& scene, std::string& error)
{
    if (!context.previous_catalog || scene.prefab_instances.empty())
    {
        return true;
    }
    const auto found = std::find_if(context.previous_catalog->entries.begin(),
                                    context.previous_catalog->entries.end(),
                                    [&](const auto& entry) { return entry->guid == asset.guid; });
    if (found == context.previous_catalog->entries.end())
    {
        return true;
    }
    const auto path = context.project / "cache" / "gen" / asset.guid;
    std::error_code filesystem_error;
    if (!std::filesystem::exists(path, filesystem_error) && !filesystem_error)
    {
        return true;
    }
    std::vector<std::byte> entity, payload;
    if (filesystem_error || !ResourceSystem::read_binary(path, entity) ||
        !ResourceSystem::decode_resource(**found, entity, payload))
    {
        error = "Cannot preserve previous scene instance identities: " + asset.guid;
        return false;
    }
    flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(payload.data()),
                                   payload.size());
    if (!schema::VerifySceneDataBuffer(verifier))
    {
        error = "Invalid previous scene instance data";
        return false;
    }
    const auto previous = schema::UnPackSceneData(payload.data());
    for (auto& instance : scene.prefab_instances)
    {
        const auto old =
            std::find_if(previous->prefab_instances.begin(), previous->prefab_instances.end(),
                         [&](const auto& item)
                         {
                             return item->prefab_guid == instance->prefab_guid &&
                                    item->root_id == instance->root_id;
                         });
        if (old == previous->prefab_instances.end())
        {
            continue;
        }
        for (const auto& mapping : (*old)->node_mappings)
        {
            const auto explicit_mapping = std::find_if(
                instance->node_mappings.begin(), instance->node_mappings.end(),
                [&](const auto& item) { return item->source_id == mapping->source_id; });
            if (explicit_mapping == instance->node_mappings.end())
            {
                instance->node_mappings.push_back(
                    std::make_unique<schema::NodeIdMappingT>(*mapping));
            }
        }
    }
    return true;
}
} // namespace

schema::AssetKind SceneAssetImporter::kind() const noexcept
{
    return schema::AssetKind::scene;
}
bool SceneAssetImporter::matches(const std::filesystem::path& source) const
{
    return source_extension(source) == ".scene";
}
bool SceneAssetImporter::inspect_batch(const ImportContext&, std::string& fingerprint,
                                       std::string& error)
{
    return schema_fingerprint({"scene.fbs", "prefab.fbs"}, fingerprint, error);
}
bool SceneAssetImporter::scan_dependencies(ImportAsset& asset, const ImportContext& context,
                                           std::string& error) const
{
    asset.build_dependencies.clear();
    asset.runtime_dependencies.clear();
    schema::SceneDataT scene;
    if (!SceneImporter::read(asset.source, scene, error))
    {
        return false;
    }
    if (!scan_node_dependencies(scene.nodes, context, asset.build_dependencies, error))
    {
        return false;
    }
    for (const auto& instance : scene.prefab_instances)
    {
        if (!reference(instance->prefab_guid, schema::AssetKind::prefab, context,
                       asset.build_dependencies, error) ||
            !scan_override_dependencies(instance->overrides, context, asset.build_dependencies,
                                        error))
        {
            return false;
        }
    }
    std::sort(asset.build_dependencies.begin(), asset.build_dependencies.end());
    return true;
}
bool SceneAssetImporter::convert(const ImportAsset& asset, const ImportContext& context,
                                 std::string& error) const
{
    schema::SceneDataT scene;
    if (!SceneImporter::read(asset.source, scene, error))
    {
        return false;
    }
    if (!retain_generated_mappings(asset, context, scene, error) ||
        !compose_scene(scene, context, error))
    {
        return false;
    }
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishSceneDataBuffer(builder, schema::SceneData::Pack(builder, &scene));
    return write_bytes(context.output / asset.guid,
                       std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())),
                       error);
}
bool SceneAssetImporter::finalize_dependencies(ImportAsset& asset, const ImportContext& context,
                                               std::string& error) const
{
    std::vector<std::byte> payload;
    if (!read_generated_payload(asset.guid, context, payload, error))
    {
        return false;
    }
    flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(payload.data()),
                                   payload.size());
    if (!schema::VerifySceneDataBuffer(verifier))
    {
        error = "Invalid generated scene";
        return false;
    }
    const auto scene = schema::UnPackSceneData(payload.data());
    asset.runtime_dependencies.clear();
    return scan_node_dependencies(scene->nodes, context, asset.runtime_dependencies, error);
}
} // namespace engine::editor::asset_pipeline
