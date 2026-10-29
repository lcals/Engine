#include "asset_pipeline_system.hpp"

#include <SDL3/SDL.h>
#include <efsw/efsw.hpp>
#include <meshoptimizer.h>
#include <pxr/usd/usd/stage.h>
#include <sqlite3.h>
#include <taskflow/core/taskflow.hpp>

#include <cstdio>

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

bool AssetPipelineSystem::build_scripts(const std::filesystem::path& project_root)
{
    last_error_.clear();
    if (!is_initialized())
    {
        last_error_ = "asset pipeline is not initialized";
        return false;
    }
    std::error_code error;
    const auto root = std::filesystem::absolute(project_root, error);
    if (error)
    {
        last_error_ = "cannot resolve project directory: " + error.message();
        return false;
    }
    const auto root_utf8 = root.generic_u8string();
    const std::string root_text(reinterpret_cast<const char*>(root_utf8.data()), root_utf8.size());
    const char* arguments[] = {ENGINE_NODE_EXECUTABLE, ENGINE_SCRIPT_BUILDER, "--project",
                               root_text.c_str(), nullptr};
    const SDL_PropertiesID properties = SDL_CreateProperties();
    const bool configured =
        properties != 0 &&
        SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, arguments) &&
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
                              SDL_PROCESS_STDIO_NULL) &&
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                              SDL_PROCESS_STDIO_APP) &&
        SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    SDL_Process* process = configured ? SDL_CreateProcessWithProperties(properties) : nullptr;
    SDL_DestroyProperties(properties);
    if (process == nullptr)
    {
        last_error_ = std::string("cannot start TypeScript compiler: ") + SDL_GetError();
        return false;
    }
    std::size_t output_size = 0;
    int exit_code = -1;
    void* output = SDL_ReadProcess(process, &output_size, &exit_code);
    if (output != nullptr && output_size != 0)
    {
        last_error_.assign(static_cast<const char*>(output), output_size);
        std::fwrite(output, 1, output_size, stderr);
    }
    SDL_free(output);
    const bool waited = SDL_WaitProcess(process, true, &exit_code);
    SDL_DestroyProcess(process);
    if (!waited || exit_code != 0)
    {
        if (last_error_.empty())
        {
            last_error_ =
                "TypeScript compilation failed with exit code " + std::to_string(exit_code);
        }
        return false;
    }
    last_error_.clear();
    return true;
}

const std::string& AssetPipelineSystem::last_error() const noexcept
{
    return last_error_;
}

} // namespace engine
