#pragma once

#include "runtime/core/guid_map.hpp"
#include "runtime/schema/asset_catalog_generated.h"

#include <filesystem>
#include <string>
#include <vector>

namespace engine::editor::asset_pipeline
{
struct ImportAsset
{
    Guid guid;
    std::string path;
    std::filesystem::path source;
    schema::AssetKind kind = schema::AssetKind::source;
    std::vector<Guid> build_dependencies;
    std::vector<Guid> runtime_dependencies;
};
} // namespace engine::editor::asset_pipeline
