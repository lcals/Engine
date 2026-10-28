#include "asset_pipeline_system.hpp"

#include <efsw/efsw.hpp>
#include <meshoptimizer.h>
#include <pxr/usd/usd/stage.h>
#include <sqlite3.h>
#include <taskflow/core/taskflow.hpp>

namespace engine
{

AssetPipelineSystem::AssetPipelineSystem() noexcept = default;

AssetPipelineSystem::~AssetPipelineSystem() noexcept
{
    shutdown();
}

bool AssetPipelineSystem::on_initialize(const EngineConfig&)
{
    // 本阶段接入任务图，不启动 Taskflow 执行器或目录监听线程。
    tf::Taskflow taskflow;
    taskflow.emplace([] {});
    if (taskflow.num_tasks() != 1)
    {
        return false;
    }
    efsw::FileWatcher watcher(true);

    sqlite3* database = nullptr;
    const int open_status = sqlite3_open(":memory:", &database);
    const int close_status = sqlite3_close(database);
    if (open_status != SQLITE_OK || close_status != SQLITE_OK)
    {
        return false;
    }

    constexpr float vertices[][3] = {{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}};
    unsigned int remap[2]{};
    if (meshopt_generateVertexRemap(remap, nullptr, 2, vertices, 2, sizeof(vertices[0])) != 1)
    {
        return false;
    }

    // 内存 stage 验证 USD 插件与链接，不读写项目文件。
    const auto stage = pxr::UsdStage::CreateInMemory();
    return static_cast<bool>(stage);
}

void AssetPipelineSystem::on_shutdown() noexcept
{
}

} // namespace engine
