#pragma once

#include "runtime/app/app.hpp"
#include "runtime/app/runtime_host.hpp"
#include "runtime/app/runtime_settings.hpp"
#include "runtime/movie/movie_player.hpp"
#if ENGINE_HAS_RESOURCE_GRAPH_PLUGIN
#include "plugins/runtime/resource_graph/resource_graph_system.hpp"
#endif
#if defined(ENGINE_HAS_RUNTIME_MCP_ENDPOINT)
#include "plugins/runtime/mcp/runtime_mcp_endpoint.hpp"
#endif
#include <cstdint>
#include <future>
#include <memory>
#include <string>

namespace engine::host_detail
{

struct RuntimeAppState
{
    RuntimeSettings settings;
    RuntimeHost host;
    RuntimeSetup setup;
    ScriptRuntimeFactory script_factory = nullptr;
    AnimationRetargeterFactory retargeter_factory = nullptr;
#if ENGINE_HAS_RESOURCE_GRAPH_PLUGIN
    std::unique_ptr<ThreadSystem> threads;
    std::unique_ptr<TaskSystem> file_tasks;
    std::unique_ptr<TaskSystem> read_tasks;
    std::unique_ptr<TaskSystem> graph_tasks;
    ResourceSystem resources;
    std::unique_ptr<plugins::resource_graph::ResourceGraphSystem> graph;
#endif
#if ENGINE_START_PACKAGED_GAMEPLAY && ENGINE_HAS_RESOURCE_GRAPH_PLUGIN && ENGINE_HAS_APP_SERVICES
    struct StartupDirectory
    {
        ResourceCatalog catalog;
        MountList mounts;
        std::string entry;
        std::string error;
        std::string warning;
    };
    std::shared_ptr<ResourceCatalog> startup_catalog = std::make_shared<ResourceCatalog>();
    std::shared_ptr<MountList> startup_mounts = std::make_shared<MountList>();
    std::shared_ptr<StartupDirectory> published = std::make_shared<StartupDirectory>();
    std::future<bool> published_result;
    MoviePlaybackHandle intro_handle;
    std::unique_ptr<ResourceLoad> intro_load;
    bool has_intro = false;
    bool intro_active = false;
    bool starting = false;
    bool preparing_gameplay = false;
    bool gameplay_ready = false;
#endif
#if defined(ENGINE_HAS_RUNTIME_MCP_ENDPOINT)
    plugins::mcp::RuntimeMcpEndpoint mcp_bridge;
#endif
    std::uint64_t frame_limit = 0;
    std::uint64_t frames = 0;
    int exit_code = 0;
    std::string error;
    bool app_ready = false;
    bool log_ready = false;
    bool initialized = false;
    bool exit_requested = false;
};

[[nodiscard]] bool initialize_runtime_app(RuntimeAppState& state, const AppConfig& app_config,
                                          int argc, char** argv);
[[nodiscard]] FrameStatus step_runtime_app(RuntimeAppState& state, double delta_time);
void shutdown_runtime_app(RuntimeAppState& state) noexcept;

} // namespace engine::host_detail
