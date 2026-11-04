#pragma once

#include "runtime/schema/mesh_generated.h"

#include <toml++/toml.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace engine::editor
{

// Editor-only import values; runtime persistent topology is generated from mesh.fbs.
struct MeshLodSettings
{
    bool enabled = false;
    std::vector<float> triangle_ratios{0.5F, 0.25F, 0.125F};
    std::vector<float> screen_heights{0.3F, 0.15F, 0.075F};
    float max_relative_error = 0.01F;
};

[[nodiscard]] bool read_mesh_lod_settings(const toml::table& meta, MeshLodSettings& output,
                                          std::string& error);
[[nodiscard]] bool generate_mesh_lods(schema::MeshDataT& mesh, const toml::table& meta,
                                      std::string& error);
[[nodiscard]] bool generate_mesh_lods(schema::MeshDataT& mesh, const std::filesystem::path& source,
                                      std::string& error);
[[nodiscard]] std::string mesh_lod_fingerprint();

} // namespace engine::editor
