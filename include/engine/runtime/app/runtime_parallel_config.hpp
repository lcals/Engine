#pragma once

#include "runtime/animation/animation_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"
#include "runtime/world/world_system.hpp"

#include <cstddef>

namespace engine
{

// 宿主选择预算，领域代码不区分桌面/移动端。域并发 0 在装配时解析为 worker 预算。
struct RuntimeParallelConfig
{
    ThreadConfig threads;
    TaskConfig runtime{"Runtime", 0, TaskPriority::high};
    TaskConfig background{"Background", 2, TaskPriority::low};
    TaskConfig loading{"Loading", 2, TaskPriority::low};
    TaskConfig physics{"Physics", 0, TaskPriority::high};
    TaskConfig animation{"Animation", 0, TaskPriority::high};
    TaskConfig navigation{"Navigation", 0, TaskPriority::normal};
    ResourceAsyncConfig resources;
    AnimationConfig animation_sampling;
    SceneRestoreConfig scene_restore;
    RenderConfig render;
    std::size_t navigation_tile_threshold = 4;
    std::size_t completions_per_frame = 16;
    double completion_budget_seconds = 0.001;
};

} // namespace engine
