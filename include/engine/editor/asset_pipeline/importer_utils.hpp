#pragma once

#include "editor/asset_pipeline/import_context.hpp"

#include <toml++/toml.hpp>

#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace engine::editor::asset_pipeline
{
[[nodiscard]] bool read_generated_payload(const Guid& guid, const ImportContext& context,
                                          std::vector<std::byte>& output, std::string& error);
[[nodiscard]] bool read_table(const std::filesystem::path& path, toml::table& table,
                              std::string& error);
[[nodiscard]] bool reference(const Guid& guid, schema::AssetKind kind, const ImportContext& context,
                             std::vector<Guid>& dependencies, std::string& error);
[[nodiscard]] bool file_hash(const std::filesystem::path& path, std::string& hash,
                             std::string& error);
// names 使用相对 include/schemas/ 的 runtime/ 或 editor/ 路径。
[[nodiscard]] bool schema_fingerprint(std::initializer_list<std::string_view> names,
                                      std::string& fingerprint, std::string& error);
[[nodiscard]] std::string source_extension(const std::filesystem::path& path);
[[nodiscard]] std::string generic_path(const std::filesystem::path& path);
} // namespace engine::editor::asset_pipeline
