#pragma once

#include "runtime/core/guid_map.hpp"
#include "runtime/resource/virtual_file_system.hpp"
#include "runtime/schema/asset_catalog_generated.h"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace engine::plugins::asset_build
{

// 编辑器离线构建：补齐固定内置条目及其目标平台实体，Shader 工具缓存留在 cache/build。
[[nodiscard]] bool prepare_builtin_resources(std::string_view target,
                                             const std::filesystem::path& cache,
                                             schema::AssetCatalogDataT& catalog,
                                             std::map<Guid, ByteBuffer>& entities,
                                             std::string& error);

} // namespace engine::plugins::asset_build
