#pragma once

#include "editor/asset_pipeline/import_asset.hpp"

#include <map>
#include <span>

namespace engine::asset_pipeline
{
// 借用的数据覆盖完整批次和所有任务；转换期间只读。
struct ImportContext
{
    const GuidMap& identities;
    const std::map<Guid, schema::AssetKind>& kinds;
    std::filesystem::path project;
    std::filesystem::path assets;
    std::filesystem::path output;
    std::string target_platform;
    const schema::AssetCatalogDataT* previous_catalog = nullptr;
    std::span<const ImportAsset* const> sources;
};
} // namespace engine::asset_pipeline
