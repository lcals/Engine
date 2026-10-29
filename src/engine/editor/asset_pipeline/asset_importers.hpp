#pragma once

#include "runtime/core/guid_map.hpp"
#include "runtime/schema/asset_catalog_generated.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace engine::asset_pipeline
{

struct ImportAsset
{
    Guid guid;
    std::string path;
    std::filesystem::path source;
    schema::AssetKind kind = schema::AssetKind::source;
    std::vector<Guid> dependencies;
};

struct ImportContext
{
    const GuidMap& identities;
    const std::map<Guid, schema::AssetKind>& kinds;
    std::filesystem::path assets;
    std::filesystem::path output;
    std::string shader_platform;
    std::string shader_profile;
};

[[nodiscard]] bool scan_dependencies(ImportAsset& asset, const ImportContext& context,
                                     std::string& error);
[[nodiscard]] bool convert_asset(const ImportAsset& asset, const ImportContext& context,
                                 std::string& error);

} // namespace engine::asset_pipeline
