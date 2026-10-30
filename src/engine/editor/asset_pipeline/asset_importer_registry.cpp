#include "editor/asset_pipeline/asset_importer_registry.hpp"

#include "editor/asset_pipeline/importers/audio_importer.hpp"
#include "editor/asset_pipeline/importers/material_importer.hpp"
#include "editor/asset_pipeline/importers/mesh_importer.hpp"
#include "editor/asset_pipeline/importers/scene_importer.hpp"
#include "editor/asset_pipeline/importers/script_importer.hpp"
#include "editor/asset_pipeline/importers/shader_importer.hpp"
#include "editor/asset_pipeline/importers/source_importer.hpp"
#include "editor/asset_pipeline/importers/texture_importer.hpp"

#include <utility>

namespace engine::editor::asset_pipeline
{
bool AssetImporterRegistry::initialize()
{
    if (!importers_.empty())
    {
        return false;
    }
    if (!add<MeshImporter>() || !add<ShaderImporter>() || !add<MaterialImporter>() ||
        !add<SceneAssetImporter>() || !add<TextureImporter>() || !add<AudioImporter>() ||
        !add<ScriptImporter>() || !add<SourceImporter>(true))
    {
        clear();
        return false;
    }
    return true;
}
bool AssetImporterRegistry::add(std::unique_ptr<AssetImporter>& importer, bool fallback)
{
    if (!importer || importer->id().empty() ||
        importer->id().find_first_not_of("abcdefghijklmnopqrstuvwxyz_") != std::string_view::npos ||
        find(importer->id()) != nullptr || (fallback && fallback_ != nullptr) ||
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
bool AssetImporterRegistry::replace(std::unique_ptr<AssetImporter>& importer)
{
    if (!importer || importer->kind() < schema::AssetKind::MIN ||
        importer->kind() > schema::AssetKind::MAX)
    {
        return false;
    }
    for (auto& registered : importers_)
    {
        if (registered->id() == importer->id())
        {
            if (registered.get() == fallback_)
            {
                fallback_ = importer.get();
            }
            registered = std::move(importer);
            return true;
        }
    }
    return false;
}
void AssetImporterRegistry::clear() noexcept
{
    fallback_ = nullptr;
    importers_.clear();
}
AssetImporter* AssetImporterRegistry::find(std::string_view id) const noexcept
{
    for (const auto& importer : importers_)
    {
        if (importer->id() == id)
        {
            return importer.get();
        }
    }
    return nullptr;
}
AssetImporter* AssetImporterRegistry::find(schema::AssetKind kind) const noexcept
{
    AssetImporter* match = nullptr;
    for (const auto& importer : importers_)
    {
        if (importer->kind() == kind)
        {
            if (match != nullptr)
            {
                return nullptr;
            }
            match = importer.get();
        }
    }
    return match;
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
} // namespace engine::editor::asset_pipeline
