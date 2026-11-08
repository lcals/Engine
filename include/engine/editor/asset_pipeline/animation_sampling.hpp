#pragma once

#include "runtime/schema/animation_generated.h"
#include "runtime/schema/skeleton_generated.h"

#include <string>

namespace engine::editor::asset_pipeline
{
// 同步 CPU 转换，在既有导入任务域执行；失败保留输入，成功替换派生采样数据。
[[nodiscard]] bool prepare_skeleton_sampling(schema::SkeletonDataT& data, std::string& error);
[[nodiscard]] bool prepare_animation_sampling(schema::AnimationDataT& data,
                                              const schema::SkeletonDataT& skeleton,
                                              std::string& error);
} // namespace engine::editor::asset_pipeline
