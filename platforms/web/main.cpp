#include "runtime_entry.hpp"

#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"
#include "web/render/web_surface_state.hpp"

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_main.h>
#include <atomic>
#include <chrono>
#include <emscripten.h>
#include <memory>

namespace
{
std::unique_ptr<engine::host_detail::RuntimeAppState> app;
std::atomic<bool> exit_requested{false};
std::chrono::steady_clock::time_point previous;
bool stopping = false;
bool notified_ready = false;

void stopped(int code, const std::string& error)
{
    MAIN_THREAD_EM_ASM(
        {
            const code = $0;
            const error = UTF8ToString($1);
        queueMicrotask(() => Module.onEngineStopped?.(code, error));
        },
        code, error.c_str());
}

void frame()
{
    if (stopping)
    {
        if (engine::render_detail::web_gpu_callbacks_pending())
        {
            return;
        }
        const auto code = app->exit_code;
        const auto error = app->error;
        app.reset();
        emscripten_cancel_main_loop();
        stopped(code, error);
        return;
    }
    if (exit_requested.load(std::memory_order_relaxed))
    {
        app->host.request_exit();
    }
    const auto now = std::chrono::steady_clock::now();
    const auto delta = std::chrono::duration<double>(now - previous).count();
    previous = now;
    const auto status = engine::host_detail::step_runtime_app(*app, delta);
    if (status == engine::FrameStatus::paused)
    {
        previous = std::chrono::steady_clock::now();
        return;
    }
    if (status == engine::FrameStatus::failed || status == engine::FrameStatus::exit_requested)
    {
        engine::host_detail::shutdown_runtime_app(*app);
        stopping = true;
        return;
    }
    if (notified_ready || app->starting)
    {
        return;
    }
    engine::RuntimeHostStatus host;
    engine::GameplayPrepareSnapshot prepared;
    engine::RenderFrameSnapshot rendered;
    auto* gameplay = app->host.gameplay();
    auto* renderer = app->host.renderer();
    auto* world = app->host.world();
    if (!gameplay || !renderer || !world || !app->host.status(host) ||
        !gameplay->prepare_status(host.prepare_id, prepared) ||
        prepared.state != engine::GameplayPrepareStatus::active ||
        !renderer->frame_status({}, rendered) || rendered.kind != engine::RenderFrameKind::scene ||
        world->scene() == nullptr || world->scene()->id() != rendered.scene_id)
    {
        return;
    }
    if (prepared.scene_load_id != engine::SceneLoadId{})
    {
        engine::SceneLoadSnapshot load;
        if (!gameplay->scene_load_status(prepared.scene_load_id, load) ||
            !renderer->frame_status(load.scope, rendered))
        {
            return;
        }
    }
    notified_ready = true;
    const auto device = std::to_string(rendered.device_generation);
    const auto submission = std::to_string(rendered.submission_id);
    const auto scene = std::to_string(rendered.scene_id);
    MAIN_THREAD_EM_ASM(
        {
            Module.onEngineReady ?.({
                                      deviceGeneration : UTF8ToString($0),
                                      submissionId : UTF8ToString($1),
                                      sceneId : UTF8ToString($2)
                                  });
        },
        device.c_str(), submission.c_str(), scene.c_str());
}
} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE void engine_web_request_exit()
{
    exit_requested.store(true, std::memory_order_relaxed);
}

int main(int argc, char** argv)
{
    char selector[256]{};
    EM_ASM({ stringToUTF8("#" + Module.canvas.id, $0, $1); }, selector, sizeof(selector));
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_CANVAS_SELECTOR, selector);
    app = std::make_unique<engine::host_detail::RuntimeAppState>();
    engine::AppConfig config;
    config.application_name = ENGINE_PROJECT_APPLICATION_NAME;
    if (!engine::host_detail::initialize_runtime_app(*app, config, argc, argv))
    {
        stopping = true;
    }
    previous = std::chrono::steady_clock::now();
    emscripten_set_main_loop_arg([](void*) { frame(); }, nullptr, 0, true);
    return 0;
}
