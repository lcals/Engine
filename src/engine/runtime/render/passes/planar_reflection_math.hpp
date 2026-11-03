#pragma once
#include <array>

namespace engine::render_detail
{
// Pure camera math shared by the GPU pass; all matrices use the renderer's column-major convention.
[[nodiscard]] bool prepare_planar_reflection(const float* plane_world, const float* camera_world,
                                             const float* camera_projection, float clip_bias,
                                             bool homogeneous, std::array<float, 16>& world,
                                             std::array<float, 16>& view,
                                             std::array<float, 16>& projection,
                                             std::array<float, 16>& view_projection);
} // namespace engine::render_detail
