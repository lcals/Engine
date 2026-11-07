#pragma once

#include "runtime/schema/mesh_generated.h"

#include <toml++/toml.hpp>

#include <filesystem>
#include <string>

namespace engine::editor
{

[[nodiscard]] bool generate_mesh_lods(schema::MeshDataT& mesh, const toml::table& meta,
                                      std::string& error);
[[nodiscard]] bool generate_mesh_lods(schema::MeshDataT& mesh, const std::filesystem::path& source,
                                      std::string& error);
[[nodiscard]] std::string mesh_lod_fingerprint();

} // namespace engine::editor
