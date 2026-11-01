#pragma once

#include "runtime/scene/scene_types.hpp"

namespace engine::camera_detail
{

// 列主序矩阵，输入/输出均指向 16 个有效 float；失败输出不可用于绘制。
[[nodiscard]] bool view_matrix(const float* world, float* view) noexcept;
[[nodiscard]] bool projection_matrix(const CameraSettings& settings, double aspect,
                                     bool homogeneous_depth, float* projection) noexcept;

} // namespace engine::camera_detail
