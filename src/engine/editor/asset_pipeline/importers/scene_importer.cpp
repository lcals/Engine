#include "editor/asset_pipeline/importers/scene_importer.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"
#include "editor/asset_pipeline/scene_importer.hpp"

#include <algorithm>
#include <flatbuffers/flatbuffers.h>
#include <span>

namespace engine::editor::asset_pipeline
{
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
    return schema_fingerprint({"scene.fbs"}, fingerprint, error);
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
    for (const auto& node : scene.nodes)
    {
        for (const auto& component : node->components)
        {
            if (const auto* animator = component->payload.AsAnimatorData();
                animator != nullptr && !animator->animation_guid.empty())
            {
                if (!reference(animator->animation_guid, schema::AssetKind::animation, context,
                               asset.build_dependencies, error))
                {
                    return false;
                }
            }
            if (const auto* audio = component->payload.AsAudioSourceData();
                audio != nullptr && !audio->clip_guid.empty())
            {
                if (!reference(audio->clip_guid, schema::AssetKind::audio, context,
                               asset.build_dependencies, error))
                {
                    return false;
                }
            }
            if (const auto* mesh = component->payload.AsMeshRendererData())
            {
                if (!reference(mesh->mesh_guid, schema::AssetKind::mesh, context,
                               asset.build_dependencies, error) ||
                    !reference(mesh->material_guid, schema::AssetKind::material, context,
                               asset.build_dependencies, error))
                {
                    return false;
                }
                for (const auto& material : mesh->additional_material_guids)
                {
                    if (!reference(material, schema::AssetKind::material, context,
                                   asset.build_dependencies, error))
                    {
                        return false;
                    }
                }
            }
        }
    }
    std::sort(asset.build_dependencies.begin(), asset.build_dependencies.end());
    asset.runtime_dependencies = asset.build_dependencies;
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
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishSceneDataBuffer(builder, schema::SceneData::Pack(builder, &scene));
    return write_bytes(context.output / asset.guid,
                       std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())),
                       error);
}
} // namespace engine::editor::asset_pipeline
