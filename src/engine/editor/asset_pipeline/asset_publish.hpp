#pragma once

#include "editor/asset_pipeline/asset_database.hpp"
#include <string>
#include <vector>

namespace engine::asset_pipeline
{

[[nodiscard]] bool recover_publish(const std::filesystem::path& cache, AssetDatabase& database,
                                   std::string& error);
[[nodiscard]] bool publish_assets(const std::filesystem::path& cache, AssetDatabase& database,
                                  const std::vector<AssetRecord>& records,
                                  const std::string& revision, std::string& error);

} // namespace engine::asset_pipeline
