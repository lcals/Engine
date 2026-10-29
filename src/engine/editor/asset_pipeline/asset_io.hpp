#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace engine::asset_pipeline
{

[[nodiscard]] std::string hash_bytes(std::span<const std::byte> bytes);
[[nodiscard]] std::string hash_text(const std::string& text);
[[nodiscard]] bool read_bytes(const std::filesystem::path& path, std::vector<std::byte>& bytes,
                              std::string& error);
[[nodiscard]] bool write_bytes(const std::filesystem::path& path, std::span<const std::byte> bytes,
                               std::string& error);
[[nodiscard]] bool write_text(const std::filesystem::path& path, const std::string& text,
                              std::string& error);
[[nodiscard]] bool run_process(const std::vector<std::string>& arguments, std::string& error,
                               std::string* output = nullptr);
[[nodiscard]] std::string new_guid();

} // namespace engine::asset_pipeline
