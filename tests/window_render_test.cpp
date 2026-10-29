#include "runtime/engine.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <bgfx/bgfx.h>

#include <cstdio>
#include <limits>

namespace
{

constexpr double delta_time = 1.0 / 60.0;
constexpr int max_frame_attempts = 200;

bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "window/render test: %s (SDL: %s)\n", message, SDL_GetError());
    }
    return condition;
}

bool push_application_event(SDL_EventType type)
{
    SDL_Event event{};
    event.type = type;
    return expect(SDL_PushEvent(&event), "push application lifecycle event");
}

bool render_frames(engine::WindowSystem& windows, engine::RenderSystem& renderer, int count)
{
    int submitted = 0;
    for (int attempt = 0; attempt < max_frame_attempts && submitted < count; ++attempt)
    {
        if (!expect(windows.tick(delta_time), "poll window events") ||
            !expect(!windows.exit_requested(), "window must remain open"))
        {
            return false;
        }
        if (windows.can_render())
        {
            if (!expect(renderer.render(), "submit a real GPU frame"))
            {
                return false;
            }
            ++submitted;
        }
        else
        {
            SDL_Delay(10);
        }
    }
    return expect(submitted == count, "window must become drawable within the attempt limit");
}

bool resize_window(engine::WindowSystem& windows, engine::RenderSystem& renderer)
{
    SDL_Window* window = windows.native_window();
    if (!expect(SDL_SetWindowSize(window, 800, 450), "request window resize"))
    {
        return false;
    }

    bool resized = false;
    for (int attempt = 0; attempt < max_frame_attempts; ++attempt)
    {
        if (!expect(windows.tick(delta_time), "poll resize events"))
        {
            return false;
        }
        int width = 0;
        int height = 0;
        if (!expect(SDL_GetWindowSize(window, &width, &height), "query resized window"))
        {
            return false;
        }
        if (width == 800 && height == 450)
        {
            resized = true;
            break;
        }
        SDL_Delay(10);
    }
    if (!expect(resized, "window resize must complete within the attempt limit") ||
        !render_frames(windows, renderer, 8))
    {
        return false;
    }

    int pixel_width = 0;
    int pixel_height = 0;
    if (!expect(SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height),
                "query resized drawable"))
    {
        return false;
    }
    const bgfx::Stats* stats = bgfx::getStats();
    return expect(stats != nullptr && stats->width == pixel_width && stats->height == pixel_height,
                  "GPU backbuffer must follow the actual drawable size");
}

bool test_window_and_renderer(const engine::EngineConfig& config)
{
    engine::WindowSystem windows;
    engine::RenderSystem renderer(windows);
    engine::EngineConfig invalid_config = config;
    invalid_config.width = 0;
    if (!expect(!windows.initialize(invalid_config), "reject zero window width") ||
        !expect(windows.native_window() == nullptr,
                "failed window initialization leaves no window") ||
        !expect(!renderer.render(), "reject rendering before initialization") ||
        !expect(!renderer.initialize(config), "renderer requires an initialized window") ||
        !expect(windows.initialize(config), "initialize SDL window") ||
        !expect(renderer.initialize(config), "initialize GPU renderer") ||
        !expect(bgfx::getRendererType() != bgfx::RendererType::Noop, "Noop is not a real renderer"))
    {
        return false;
    }
#if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
    if (!expect(bgfx::getRendererType() == bgfx::RendererType::Metal,
                "Apple renderer must be Metal"))
    {
        return false;
    }
#elif defined(SDL_PLATFORM_WINDOWS)
    if (!expect(bgfx::getRendererType() == bgfx::RendererType::Direct3D12,
                "Windows renderer must be Direct3D12"))
    {
        return false;
    }
#else
    if (!expect(bgfx::getRendererType() == bgfx::RendererType::Vulkan, "renderer must be Vulkan"))
    {
        return false;
    }
#endif

    engine::RenderSystem second_renderer(windows);
    if (!expect(!second_renderer.initialize(config), "reject a second active bgfx instance") ||
        !render_frames(windows, renderer, 8) || !resize_window(windows, renderer) ||
        !push_application_event(SDL_EVENT_WILL_ENTER_BACKGROUND) ||
        !expect(windows.tick(delta_time), "poll background event") ||
        !expect(!windows.can_render(), "background application must pause drawing") ||
        !expect(!renderer.render(), "renderer must reject a paused window") ||
        !push_application_event(SDL_EVENT_DID_ENTER_FOREGROUND) ||
        !render_frames(windows, renderer, 8))
    {
        return false;
    }

    SDL_Event close{};
    close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    close.window.windowID = SDL_GetWindowID(windows.native_window());
    if (!expect(SDL_PushEvent(&close), "push window close event") ||
        !expect(windows.tick(delta_time), "poll close event") ||
        !expect(windows.exit_requested() && !windows.can_render(), "close requests engine exit"))
    {
        return false;
    }

    renderer.shutdown();
    renderer.shutdown();
    windows.shutdown();
    windows.shutdown();
    if (!expect(!renderer.is_initialized() && windows.native_window() == nullptr,
                "repeated shutdown releases resources") ||
        !expect(windows.initialize(config), "reinitialize window") ||
        !expect(renderer.initialize(config), "reinitialize GPU renderer") ||
        !render_frames(windows, renderer, 8))
    {
        return false;
    }
    return true;
}

struct GpuCounts
{
    std::uint16_t vertices;
    std::uint16_t indices;
    std::uint16_t shaders;
    std::uint16_t programs;
    std::uint16_t uniforms;

    bool operator==(const GpuCounts&) const = default;
};

GpuCounts gpu_counts()
{
    const bgfx::Stats& stats = *bgfx::getStats();
    return {stats.numVertexBuffers, stats.numIndexBuffers, stats.numShaders, stats.numPrograms,
            stats.numUniforms};
}

bool create_cube_scene(engine::WorldSystem& world, bool unlit)
{
    engine::Scene* scene = world.create_scene("GPU test scene");
    if (!expect(scene != nullptr, "create a scene"))
    {
        return false;
    }
    engine::Node* cube = scene->create_node("Cube");
    engine::Node* camera_node = scene->create_node("Camera");
    engine::Node* light_node = scene->create_node("Light");
    if (!expect(cube != nullptr && camera_node != nullptr && light_node != nullptr,
                "all scene objects must be nodes"))
    {
        return false;
    }
    engine::Transform cube_transform;
    cube_transform.rotation = {20.0, 35.0, 0.0};
    cube_transform.scale = {1.0, 0.8, 1.2};
    engine::Transform camera_transform;
    camera_transform.position.z = 6.0;
    camera_transform.scale = {2.0, 0.5, 3.0};
    engine::Transform light_transform;
    light_transform.rotation = {-20.0, 30.0, 0.0};
    if (!expect(cube->set_transform(cube_transform), "set nonuniform cube transform") ||
        !expect(camera_node->set_transform(camera_transform), "place camera node") ||
        !expect(light_node->set_transform(light_transform), "aim light node"))
    {
        return false;
    }
    engine::MeshRenderer* mesh = cube->add_mesh_renderer();
    engine::Camera* camera = camera_node->add_camera();
    engine::Light* light = light_node->add_light();
    if (!expect(mesh != nullptr && camera != nullptr && light != nullptr,
                "attach mesh, camera, and light components"))
    {
        return false;
    }
    engine::Material material;
    material.metallic = 0.25F;
    material.roughness = 0.35F;
    material.unlit = unlit;
    return expect(mesh->set_material(material), "set cube material") &&
           expect(scene->set_active_camera(camera), "select scene camera component");
}

bool test_scene_renderer(const engine::EngineConfig& config)
{
    engine::WorldSystem world;
    engine::WindowSystem windows;
    engine::RenderSystem renderer(windows, &world);
    if (!expect(!renderer.prepare_scene(), "preparation requires initialized renderer") ||
        !expect(world.initialize(config), "initialize scene world") ||
        !expect(windows.initialize(config), "initialize scene window") ||
        !expect(renderer.initialize(config), "initialize scene renderer") ||
        !render_frames(windows, renderer, 8) ||
        !expect(!renderer.scene_prepared() && renderer.gpu_resource_count() == 0,
                "idle editor owns no project GPU resources"))
    {
        return false;
    }
    SDL_Window* const original_window = windows.native_window();
    const GpuCounts idle = gpu_counts();
    // bgfx::touch 本身也进入 numDraw；比较空白基线，不能将它计为项目绘制。
    const std::uint32_t idle_draws = bgfx::getStats()->numDraw;
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        if (!create_cube_scene(world, cycle == 0) ||
            !expect(renderer.prepare_scene(), "prepare embedded cube resources") ||
            !expect(renderer.scene_prepared() && renderer.gpu_resource_count() == 11,
                    "prepared scene owns the expected GPU handles") ||
            !expect(renderer.prepare_scene(), "preparing twice is idempotent") ||
            !render_frames(windows, renderer, 8) ||
            !expect(bgfx::getStats()->numDraw == idle_draws + 1,
                    "one mesh component adds one draw above blank baseline") ||
            !expect(gpu_counts().vertices == idle.vertices + 1 &&
                        gpu_counts().indices == idle.indices + 1 &&
                        gpu_counts().programs == idle.programs + 1,
                    "bgfx sees cube buffers and program") ||
            !resize_window(windows, renderer))
        {
            return false;
        }

        // 热重载替换场景时不提前通知渲染器，render 按场景编号重新准备资源。
        world.destroy_scene();
        if (!create_cube_scene(world, false) || !render_frames(windows, renderer, 8) ||
            !expect(renderer.gpu_resource_count() == 11 &&
                        bgfx::getStats()->numDraw == idle_draws + 1,
                    "scene replacement recreates one resource set"))
        {
            return false;
        }

        world.destroy_scene();
        renderer.release_scene();
        renderer.release_scene();
        if (!expect(!renderer.scene_prepared() && renderer.gpu_resource_count() == 0,
                    "stop releases project ownership immediately") ||
            !render_frames(windows, renderer, 8) ||
            !expect(bgfx::getStats()->numDraw == idle_draws,
                    "stop restores the blank frame baseline") ||
            !expect(gpu_counts() == idle, "bgfx retires all project GPU resources") ||
            !expect(windows.native_window() == original_window,
                    "all starts and stops reuse the same editor window"))
        {
            return false;
        }
    }

    engine::Scene* empty = world.create_scene("No hidden camera or mesh");
    if (!expect(empty != nullptr, "create empty scene") || !render_frames(windows, renderer, 4) ||
        !expect(renderer.gpu_resource_count() == 0 && bgfx::getStats()->numDraw == idle_draws,
                "empty scene does not create hidden render objects"))
    {
        return false;
    }
    return true;
}

bool tick_engine_frames(engine::Engine& engine, int count)
{
    int submitted = 0;
    for (int attempt = 0; attempt < max_frame_attempts && submitted < count; ++attempt)
    {
        const engine::FrameStatus status = engine.tick(delta_time);
        if (status == engine::FrameStatus::running)
        {
            ++submitted;
        }
        else if (status == engine::FrameStatus::paused)
        {
            SDL_Delay(10);
        }
        else
        {
            return expect(false, "engine frame must run or pause");
        }
    }
    return expect(submitted == count, "engine must submit frames within the attempt limit");
}

bool test_engine(const engine::EngineConfig& config)
{
    engine::Engine& engine = engine::Engine::instance();
    engine.shutdown();
    engine::EngineConfig invalid_config = config;
    invalid_config.width = 0;
    if (!expect(&engine == &engine::Engine::instance(), "Engine must be a singleton") ||
        !expect(!engine.initialize(invalid_config), "reject invalid window configuration") ||
        !expect(engine.tick(delta_time) == engine::FrameStatus::failed,
                "tick must fail before initialization") ||
        !expect(engine.initialize(config), "initialize engine with all runtime systems") ||
        !expect(!engine.initialize(config), "reject repeated initialization while running") ||
        !tick_engine_frames(engine, 8) ||
        !expect(engine.tick(std::numeric_limits<double>::quiet_NaN()) ==
                    engine::FrameStatus::failed,
                "reject non-finite frame duration") ||
        !expect(engine.tick(-1.0) == engine::FrameStatus::failed,
                "reject negative frame duration") ||
        !push_application_event(SDL_EVENT_WILL_ENTER_BACKGROUND) ||
        !expect(engine.tick(delta_time) == engine::FrameStatus::paused,
                "engine must report background pause") ||
        !push_application_event(SDL_EVENT_DID_ENTER_FOREGROUND) || !tick_engine_frames(engine, 8))
    {
        return false;
    }

    engine.request_exit();
    if (!expect(engine.tick(delta_time) == engine::FrameStatus::exit_requested,
                "engine must report requested exit"))
    {
        return false;
    }
    engine.shutdown();
    engine.shutdown();
    return expect(engine.tick(delta_time) == engine::FrameStatus::failed,
                  "shutdown engine must reject frames") &&
           expect(engine.initialize(config), "reinitialize engine after shutdown") &&
           tick_engine_frames(engine, 8);
}

} // namespace

int main(int, char**)
{
    engine::EngineConfig config;
    config.window_title = "Engine GPU integration test";
    config.width = 640;
    config.height = 360;

    const bool windows_passed = test_window_and_renderer(config);
    const bool scene_passed = windows_passed && test_scene_renderer(config);
    const bool engine_passed = scene_passed && test_engine(config);
    engine::Engine::instance().shutdown();
    if (!windows_passed || !scene_passed || !engine_passed)
    {
        return 1;
    }
    std::puts("Window, cube/PBR draws, scene resource retirement, and Engine integration passed.");
    return 0;
}
