#include "editor/asset_pipeline/asset_importer_registry.hpp"

#include "editor/asset_pipeline/importers/material_importer.hpp"
#include "editor/asset_pipeline/importers/mesh_importer.hpp"
#include "editor/asset_pipeline/importers/scene_importer.hpp"
#include "editor/asset_pipeline/importers/script_importer.hpp"
#include "editor/asset_pipeline/importers/shader_importer.hpp"
#include "editor/asset_pipeline/importers/source_importer.hpp"
#include "editor/asset_pipeline/importers/texture_importer.hpp"

#include <utility>

namespace engine::asset_pipeline
{
bool AssetImporterRegistry::initialize()
{
    if (!importers_.empty())
    {
        return false;
    }
    if (!add<MeshImporter>() || !add<ShaderImporter>() || !add<MaterialImporter>() ||
        !add<SceneAssetImporter>() || !add<TextureImporter>() || !add<ScriptImporter>() ||
        !add<SourceImporter>(true))
    {
        importers_.clear();
        fallback_ = nullptr;
        return false;
    }
    return true;
}
bool AssetImporterRegistry::add(std::unique_ptr<AssetImporter>& importer, bool fallback)
{
    if (!importer || find(importer->kind()) != nullptr || (fallback && fallback_ != nullptr) ||
        importer->kind() < schema::AssetKind::MIN || importer->kind() > schema::AssetKind::MAX)
    {
        return false;
    }
    if (fallback)
    {
        fallback_ = importer.get();
    }
    importers_.push_back(std::move(importer));
    return true;
}
AssetImporter* AssetImporterRegistry::find(schema::AssetKind kind) const noexcept
{
    for (const auto& importer : importers_)
    {
        if (importer->kind() == kind)
        {
            return importer.get();
        }
    }
    return nullptr;
}
AssetImporter* AssetImporterRegistry::select(const std::filesystem::path& source) const
{
    AssetImporter* match = nullptr;
    for (const auto& importer : importers_)
    {
        if (importer.get() != fallback_ && importer->matches(source))
        {
            if (match != nullptr)
            {
                return nullptr;
            }
            match = importer.get();
        }
    }
    return match != nullptr ? match : fallback_;
}
} // namespace engine::asset_pipeline
