#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::editor::asset_pipeline
{

[[nodiscard]] std::string hash_bytes(std::span<const std::byte> bytes);
[[nodiscard]] std::string hash_text(const std::string& text);
[[nodiscard]] bool read_bytes(const std::filesystem::path& path, std::vector<std::byte>& bytes,
                              std::string& error);
[[nodiscard]] bool write_bytes(const std::filesystem::path& path, std::span<const std::byte> bytes,
                               std::string& error);
[[nodiscard]] bool write_text(const std::filesystem::path& path, const std::string& text,
                              std::string& error);
[[nodiscard]] bool project_file_path(const std::filesystem::path& project,
                                     std::string_view directory, std::string_view relative,
                                     std::string_view extension, std::filesystem::path& output,
                                     std::string& error);
// 调用方已校验 destination 位于项目内，并持有项目资源锁；失败时 guid 为空。
[[nodiscard]] bool create_source_with_meta(const std::filesystem::path& destination,
                                           const std::string& source, std::string_view importer,
                                           std::string& guid, std::string& error);
[[nodiscard]] std::string new_guid();

} // namespace engine::editor::asset_pipeline
