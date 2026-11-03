#pragma once

#include "runtime/schema/prefab_generated.h"
#include <filesystem>
#include <string>
#include <toml++/toml.hpp>

namespace engine::editor::prefab_detail
{
[[nodiscard]] bool read_overrides(const toml::array& input,
                                  std::vector<std::unique_ptr<schema::PrefabNodeOverrideT>>& output,
                                  std::string& error);
[[nodiscard]] bool
write_overrides(const std::vector<std::unique_ptr<schema::PrefabNodeOverrideT>>& input,
                toml::array& output, std::string& error);
[[nodiscard]] bool read_instances(const toml::table& input,
                                  std::vector<std::unique_ptr<schema::PrefabInstanceDataT>>& output,
                                  std::string& error);
[[nodiscard]] bool
write_instances(const std::vector<std::unique_ptr<schema::PrefabInstanceDataT>>& input,
                toml::table& output, std::string& error);
[[nodiscard]] bool write_document(const std::filesystem::path& destination,
                                  const toml::table& document, std::string& error);
} // namespace engine::editor::prefab_detail
