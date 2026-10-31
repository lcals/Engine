#pragma once

#include "runtime/schema/material_generated.h"

#include <array>
#include <cstdint>

namespace engine::render_detail
{

// 矩阵均为列主序，输入/输出指向16个有效float；返回false时输出不可用于提交。
[[nodiscard]] bool finite_matrix(const float* matrix);
[[nodiscard]] double linear_determinant(const float* matrix);
[[nodiscard]] std::uint64_t cull_state(schema::CullMode mode, const float* model);
[[nodiscard]] bool camera_view(const float* world, float* view);
[[nodiscard]] bool normal_matrix(const float* model, float* normal);
[[nodiscard]] std::array<float, 3> transform_point(const float* matrix,
                                                   const std::array<float, 3>& point) noexcept;

} // namespace engine::render_detail
