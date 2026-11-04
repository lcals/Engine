#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace engine::schema
{
struct EditableMeshDataT;
struct MeshDataT;
} // namespace engine::schema

namespace engine::plugins::modeling::detail
{
// 命令内部的源文件访问，不持有场景、选择或撤销状态。
[[nodiscard]] bool safe_asset_path(const std::filesystem::path& root, std::string_view name,
                                   std::filesystem::path& output, std::string& error);
[[nodiscard]] bool save_source(const std::filesystem::path& path,
                               const schema::EditableMeshDataT& source,
                               const schema::MeshDataT& mesh, std::string& error);
[[nodiscard]] bool load_source(const std::filesystem::path& path, schema::EditableMeshDataT& source,
                               std::string& error);
} // namespace engine::plugins::modeling::detail
