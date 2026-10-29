#include "editor/asset_pipeline/asset_importer.hpp"

namespace engine::editor::asset_pipeline
{
std::string_view AssetImporter::id() const noexcept
{
    return schema::EnumNameAssetKind(kind());
}
bool AssetImporter::discover(const ImportAsset&, std::vector<ImportSubresource>& output,
                             std::string&) const
{
    output.clear();
    return true;
}
std::string_view AssetImporter::default_meta() const noexcept
{
    return {};
}
std::string AssetImporter::module_path(const std::filesystem::path&) const
{
    return {};
}
bool AssetImporter::inspect_batch(const ImportContext&, std::string& fingerprint, std::string&)
{
    fingerprint.clear();
    return true;
}
bool AssetImporter::scan_dependencies(ImportAsset& asset, const ImportContext&, std::string&) const
{
    asset.build_dependencies.clear();
    asset.runtime_dependencies.clear();
    return true;
}
bool AssetImporter::prepare_batch(const ImportContext&, TaskDescription& task, std::string&) const
{
    task = {};
    return true;
}
} // namespace engine::editor::asset_pipeline
