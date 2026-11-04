#pragma once

#include "plugins/editor/asset_import/fbx_document.hpp"

#include <toml++/toml.hpp>

namespace engine::plugins::asset_import
{
// Shared only by the FBX importer and the editor animation authoring commands.
[[nodiscard]] bool read_animation_authoring(const toml::table& meta, FbxDocument& document,
                                            std::string& error);
[[nodiscard]] bool validate_animation_authoring(const FbxDocument& document, std::string& error);
} // namespace engine::plugins::asset_import
