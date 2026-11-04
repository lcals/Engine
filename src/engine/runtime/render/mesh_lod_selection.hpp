#pragma once

#include "runtime/schema/mesh_generated.h"

#include <array>
#include <cstddef>

namespace engine::render_detail
{

// Unclipped normalized projected box height. Infinity means unsafe projection: use LOD0.
[[nodiscard]] float projected_mesh_height(const std::array<float, 3>& minimum,
                                          const std::array<float, 3>& maximum, const float* model,
                                          const float* view, const float* projection,
                                          float near_clip) noexcept;
[[nodiscard]] std::size_t select_mesh_lod(const schema::MeshDataT& data, float screen_height,
                                          std::size_t previous_level = 0,
                                          float hysteresis = 0.1F) noexcept;

} // namespace engine::render_detail
