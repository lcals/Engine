#pragma once

#include "runtime/animation/animation_system.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"
#include "runtime/world/world_system.hpp"

#include <cstddef>

namespace engine
{

// 运行预算由宿主从完整配置传入；域并发 0 在装配时解析为 worker 预算。
struct RuntimeParallelConfig
{
    ThreadConfig threads;
    TaskConfig runtime{"Runtime"};
    TaskConfig background{"Background"};
    TaskConfig file{"File"};
    TaskConfig resource_read{"ResourceRead"};
    TaskConfig resource_graph{"ResourceGraph"};
    TaskConfig movie{"Movie"};
    TaskConfig render{"Render"};
    TaskConfig physics{"Physics"};
    TaskConfig animation{"Animation"};
    TaskConfig navigation{"Navigation"};
    ResourceReadConfig resources;
    SceneRestoreConfig scene_restore;
    RenderConfig rendering;
    AnimationConfig animation_sampling;
    std::size_t navigation_tile_threshold{};
    std::size_t completions_per_frame{};
    double completion_budget_seconds{};
};

} // namespace engine
