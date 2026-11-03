#pragma once

#include "runtime/scene/scene_types.hpp"
#include "runtime/schema/material_generated.h"

#include <array>
#include <cstdint>

namespace engine::render_detail
{

// 矩阵均为列主序，输入/输出指向16个有效float；返回false时输出不可用于提交。
[[nodiscard]] bool finite_matrix(const float* matrix);
[[nodiscard]] double linear_determinant(const float* matrix);
[[nodiscard]] std::uint64_t cull_state(schema::CullMode mode, const float* model);
[[nodiscard]] bool normal_matrix(const float* model, float* normal);
// Reconstruct a conservative symmetric frustum, including off-centre hemicube projections.
[[nodiscard]] bool projection_camera_settings(const float* projection, float aspect,
                                              bool homogeneous, CameraSettings& output);
[[nodiscard]] std::array<float, 3> transform_point(const float* matrix,
                                                   const std::array<float, 3>& point) noexcept;

} // namespace engine::render_detail
