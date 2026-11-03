#include "scene_snapshot.hpp"

#include "animation_fixture.hpp"
#include "resource_fixture.hpp"

#if defined(ENGINE_TEST_RESOURCE_GRAPH)
#include "plugins/runtime/resource_graph/resource_graph_system.hpp"
#endif

#include "runtime/app/runtime_host.hpp"
#include "runtime/engine.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/gameplay/script_runtime.hpp"
#include "runtime/render/render_context.hpp"
#include "runtime/render/render_extension.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/planar_reflection_component.hpp"
#include "runtime/scene/post_process_component.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"
#include "runtime/utils/mesh_utils.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <basisu/encoder/basisu_comp.h>
#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

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

bool render_frames(engine::WindowSystem& windows, engine::RenderSystem& renderer, int count,
                   engine::Camera camera = {})
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
            if (!expect(renderer.render(camera), "submit a real GPU frame"))
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
    if (submitted != count)
    {
        int width = 0;
        int height = 0;
        const bool sized = SDL_GetWindowSizeInPixels(windows.native_window(), &width, &height);
        std::fprintf(stderr,
                     "window/render test: submitted=%d/%d, flags=0x%llx, sized=%d, pixels=%dx%d\n",
                     submitted, count,
                     static_cast<unsigned long long>(SDL_GetWindowFlags(windows.native_window())),
                     sized, width, height);
    }
    return expect(submitted == count, "window must become drawable within the attempt limit");
}

bool resize_window(engine::WindowSystem& windows, engine::RenderSystem& renderer)
{
    SDL_Window* window = windows.native_window();
    if (!expect(windows.set_size(800, 450), "request logical window resize through WindowSystem"))
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

bool test_window_controls(engine::WindowSystem& windows)
{
    engine::WindowSize size;
    engine::WindowMode mode;
    engine::MouseMode mouse;
    std::vector<engine::DisplayMode> modes;
    int logical_width = 0;
    int logical_height = 0;
    int pixel_width = 0;
    int pixel_height = 0;
    const engine::DisplayMode unsupported{0x7FFFFFFFU, 0x7FFFFFFFU, 1.0F, 60.0F};
    if (!expect(windows.size(size) &&
                    SDL_GetWindowSize(windows.native_window(), &logical_width, &logical_height) &&
                    SDL_GetWindowSizeInPixels(windows.native_window(), &pixel_width, &pixel_height),
                "query logical and drawable sizes independently") ||
        !expect(size.logical_width == static_cast<std::uint32_t>(logical_width) &&
                    size.logical_height == static_cast<std::uint32_t>(logical_height) &&
                    size.pixel_width == static_cast<std::uint32_t>(pixel_width) &&
                    size.pixel_height == static_cast<std::uint32_t>(pixel_height),
                "window size preserves SDL logical/pixel semantics on high-density displays") ||
        !expect(windows.mode(mode) && mode == engine::WindowMode::windowed,
                "new window starts in windowed mode") ||
        !expect(!windows.set_size(0, 1) && !windows.set_size(1, 0) &&
                    !windows.set_size(std::numeric_limits<std::uint32_t>::max(), 1),
                "invalid logical sizes fail without a resize request") ||
        !expect(!windows.set_mode(static_cast<engine::WindowMode>(99)) &&
                    !windows.set_mode(engine::WindowMode::fullscreen) &&
                    !windows.set_mode(engine::WindowMode::windowed, &unsupported) &&
                    !windows.set_mode(engine::WindowMode::fullscreen, &unsupported),
                "invalid or non-enumerated fullscreen modes do not silently change window mode") ||
        !expect(windows.mode(mode) && mode == engine::WindowMode::windowed,
                "rejected fullscreen request keeps actual window mode queryable") ||
        !expect(windows.display_modes(modes), "enumerate SDL modes of the window display"))
    {
        return false;
    }
    for (const auto& value : modes)
    {
        if (!expect(value.width > 0 && value.height > 0 && std::isfinite(value.pixel_density) &&
                        value.pixel_density > 0.0F && std::isfinite(value.refresh_rate) &&
                        value.refresh_rate >= 0.0F,
                    "enumerated modes preserve dimensions, density, and refresh rate"))
        {
            return false;
        }
    }
    return expect(!windows.set_mouse_mode(static_cast<engine::MouseMode>(99)),
                  "reject an invalid mouse mode") &&
           expect(windows.set_mouse_mode(engine::MouseMode::hidden) && windows.mouse_mode(mouse) &&
                      mouse == engine::MouseMode::hidden,
                  "hidden mouse uses SDL cursor visibility") &&
           expect(windows.set_mouse_mode(engine::MouseMode::normal) && windows.mouse_mode(mouse) &&
                      mouse == engine::MouseMode::normal,
                  "normal mouse restores a visible cursor without relative capture");
}

bool test_window_and_renderer(const engine::EngineConfig& config)
{
    engine::WindowSystem windows;
    engine::RenderSystem renderer(windows);
    engine::EngineConfig invalid_config = config;
    invalid_config.width = 0;
    engine::WindowSize absent_size{1, 1, 1, 1};
    engine::MouseMode absent_mouse = engine::MouseMode::relative;
    if (!expect(!windows.size(absent_size) && absent_size.logical_width == 0 &&
                    absent_size.logical_height == 0 && absent_size.pixel_width == 0 &&
                    absent_size.pixel_height == 0 && !windows.request_exit() &&
                    !windows.mouse_mode(absent_mouse) && absent_mouse == engine::MouseMode::normal,
                "uninitialized window operations fail with safe outputs") ||
        !expect(renderer.vsync() && !renderer.set_vsync(false),
                "VSync defaults on and rejects requests before renderer initialization") ||
        !expect(!windows.initialize(invalid_config), "reject zero window width") ||
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
        !test_window_controls(windows) ||
        !expect(renderer.set_vsync(false) && !renderer.vsync(), "accept VSync disable request") ||
        !render_frames(windows, renderer, 2) ||
        !expect(renderer.set_vsync(true) && renderer.vsync(), "accept VSync enable request") ||
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
    return expect(windows.request_exit() && windows.request_exit() && windows.exit_requested(),
                  "explicit exit request is accepted repeatedly and latched for the host");
}

struct GpuCounts
{
    std::uint16_t vertices;
    std::uint16_t indices;
    std::uint16_t dynamic_vertices;
    std::uint16_t dynamic_indices;
    std::uint16_t shaders;
    std::uint16_t programs;
    std::uint16_t uniforms;
    std::uint16_t textures;

    bool operator==(const GpuCounts&) const = default;
};

GpuCounts gpu_counts()
{
    const bgfx::Stats& stats = *bgfx::getStats();
    return {stats.numVertexBuffers,
            stats.numIndexBuffers,
            stats.numDynamicVertexBuffers,
            stats.numDynamicIndexBuffers,
            stats.numShaders,
            stats.numPrograms,
            stats.numUniforms,
            stats.numTextures};
}

bool create_cube_scene(engine::WorldSystem& world, bool unlit)
{
    engine::Scene* scene = world.create_scene("GPU test scene");
    if (!expect(scene != nullptr, "create a scene"))
    {
        return false;
    }
    engine::Node cube = scene->create_node("Cube");
    engine::Node camera_node = scene->create_node("Camera");
    engine::Node light_node = scene->create_node("Light");
    if (!expect(cube.valid() && camera_node.valid() && light_node.valid(),
                "all scene objects must be nodes"))
    {
        return false;
    }
    engine::TransformSettings cube_transform;
    cube_transform.rotation = {20.0, 35.0, 0.0};
    cube_transform.scale = {1.0, 0.8, 1.2};
    engine::TransformSettings camera_transform;
    camera_transform.position.mutate_z(6.0);
    camera_transform.scale = {2.0, 0.5, 3.0};
    engine::TransformSettings light_transform;
    light_transform.rotation = {-20.0, 30.0, 0.0};
    if (!expect(cube.get_component<engine::Transform>().set_settings(cube_transform),
                "set nonuniform cube transform") ||
        !expect(camera_node.get_component<engine::Transform>().set_settings(camera_transform),
                "place camera node") ||
        !expect(light_node.get_component<engine::Transform>().set_settings(light_transform),
                "aim light node"))
    {
        return false;
    }
    engine::MeshRenderer mesh = cube.add_component<engine::MeshRenderer>();
    engine::Camera camera = camera_node.add_component<engine::Camera>();
    engine::Light light = light_node.add_component<engine::Light>();
    if (!expect(mesh.valid() && camera.valid() && light.valid(),
                "attach mesh, camera, and light components"))
    {
        return false;
    }
    auto material = mesh.material();
    return expect(material.set_type(unlit ? "builtin://materials/default-unlit"
                                          : "builtin://materials/default-pbr"),
                  "select material type") &&
           expect(material.set_v4("baseColor", engine::Vec4(0.7, 0.3, 0.15, 1.0)),
                  "set named material color") &&
           (unlit || (expect(material.set_float("metallic", 0.25F), "set metallic parameter") &&
                      expect(material.set_float("roughness", 0.35F), "set roughness parameter"))) &&
           expect(scene->set_active_camera(camera), "select scene camera component");
}

bool test_scene_renderer(const engine::EngineConfig& config)
{
    engine::ResourceSystem resources;
    if (!expect(resources.initialize(config), "initialize render resource definitions"))
    {
        return false;
    }
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
    // 自定义资源路径和逻辑参数名复用同一着色器二进制，渲染器不识别材质种类。
    engine::schema::ShaderDataT custom(
        *engine::ResourceSystem::find_shader("builtin://shaders/pbr"));
    custom.path = "shaders/tests/custom";
    custom.parameters.front()->name = "tint";
    engine::schema::ShaderDataT incomplete(custom);
    incomplete.path = "shaders/tests/incomplete";
    incomplete.parameters.erase(incomplete.parameters.begin());
    engine::schema::ShaderDataT unknown(custom);
    unknown.path = "shaders/tests/unknown-uniform";
    unknown.parameters.front()->uniform_name = "u_missing_parameter";
    engine::schema::MaterialDataT custom_material;
    custom_material.type_path = "materials/tests/custom";
    custom_material.shader_path = custom.path;
    test_support::ResourceFixture fixture;
    if (!expect(fixture.add(custom) && fixture.add(incomplete) && fixture.add(unknown) &&
                    fixture.add(custom_material) && fixture.publish(resources),
                "submit custom shader and material entity directory"))
    {
        return false;
    }
    SDL_Window* const original_window = windows.native_window();
    const GpuCounts idle = gpu_counts();
    // bgfx::touch 本身也进入 numDraw；比较空白基线，不能将它计为项目绘制。
    const std::uint32_t idle_draws = bgfx::getStats()->numDraw;
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        const auto active_resources = [&]()
        {
            const auto active = gpu_counts();
            // Forward+ also allocates a dynamic cluster index buffer; bgfx may count its
            // backing allocation among static buffers. The stop check retains exact equality.
            const bool valid = active.vertices > idle.vertices && active.indices > idle.indices &&
                               active.dynamic_indices > idle.dynamic_indices &&
                               active.programs > idle.programs;
            if (!valid)
            {
                std::fprintf(stderr, "window/render resources: idle=%u/%u/%u active=%u/%u/%u\n",
                             idle.vertices, idle.indices, idle.programs, active.vertices,
                             active.indices, active.programs);
            }
            return valid;
        };
        if (!create_cube_scene(world, cycle == 0) ||
            !expect(renderer.prepare_scene(), "prepare embedded cube resources") ||
            !expect(renderer.scene_prepared() && renderer.gpu_resource_count() > 0,
                    "prepared scene owns GPU handles") ||
            !expect(renderer.prepare_scene(), "preparing twice is idempotent") ||
            !render_frames(windows, renderer, 8) ||
            !expect(bgfx::getStats()->numDraw > idle_draws,
                    "mesh contributes real depth, surface and color draws above blank baseline") ||
            !expect(active_resources(), "bgfx sees cube buffers and the active frame programs") ||
            !resize_window(windows, renderer))
        {
            return false;
        }

        const auto scene_draws = bgfx::getStats()->numDraw;
        auto mesh =
            test_support::scene_nodes(*world.scene()).front().get_component<engine::MeshRenderer>();
        if (!expect(mesh.material().set_type(custom_material.type_path) &&
                        mesh.material().set_v4("tint", {0.2, 0.7, 0.3, 1.0}) &&
                        !mesh.material().set_v4("baseColor", {0.2, 0.7, 0.3, 1.0}),
                    "custom material resolves names from its shader") ||
            !render_frames(windows, renderer, 8) ||
            !expect(renderer.gpu_resource_count() > 0 && bgfx::getStats()->numDraw == scene_draws,
                    "shader switching retires previous program and renders custom definition"))
        {
            return false;
        }
        for (const auto* invalid_path :
             {"shaders/tests/incomplete", "shaders/tests/unknown-uniform"})
        {
            if (!expect(mesh.material().set_shader(invalid_path), "select schema-valid shader") ||
                !expect(
                    !renderer.prepare_scene() && renderer.gpu_resource_count() == 0,
                    "shader reflection rejects missing and unknown uniforms with full cleanup") ||
                !expect(mesh.material().set_shader(custom.path), "restore valid shader") ||
                !render_frames(windows, renderer, 8) ||
                !expect(renderer.gpu_resource_count() > 0,
                        "renderer recovers without retaining failed program handles"))
            {
                return false;
            }
        }
        if (!mesh.set_enabled(false))
        {
            std::abort();
        }
        if (!render_frames(windows, renderer, 8) ||
            !expect(renderer.gpu_resource_count() == 0 && gpu_counts() == idle,
                    "disabling the last drawable mesh releases all scene GPU resources"))
        {
            return false;
        }
        if (!mesh.set_enabled(true))
        {
            std::abort();
        }
        if (!render_frames(windows, renderer, 8))
        {
            return false;
        }

        // 热重载替换场景时不提前通知渲染器，render 按存活节点增量准备资源。
        world.destroy_scene();
        if (!create_cube_scene(world, false) || !render_frames(windows, renderer, 8) ||
            !expect(renderer.gpu_resource_count() > 0 && bgfx::getStats()->numDraw == scene_draws,
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
    world.destroy_scene();
    if (!create_cube_scene(world, true))
    {
        return false;
    }
    auto retained = test_support::scene_nodes(*world.scene()).front();
    if (!expect(retained.set_persistent(true), "retain a rendered mesh") ||
        !render_frames(windows, renderer, 8))
    {
        return false;
    }
    const auto retained_counts = gpu_counts();
    const auto retained_draws = bgfx::getStats()->numDraw;
    world.unload_scene();
    if (!render_frames(windows, renderer, 8))
    {
        return false;
    }
    const auto camera_less = gpu_counts();
    if (!expect(world.scene() == nullptr && camera_less.vertices > idle.vertices &&
                    camera_less.indices == retained_counts.indices &&
                    camera_less.programs > idle.programs && bgfx::getStats()->numDraw == idle_draws,
                "camera-less interval keeps mesh buffers and releases camera targets") ||
        !create_cube_scene(world, true) || !render_frames(windows, renderer, 8) ||
        !expect(bgfx::getStats()->numDraw == retained_draws + 3,
                "new active camera renders both retained and current meshes") ||
        !expect(retained.scene()->destroy_node(retained.id()), "delete retained mesh explicitly") ||
        !render_frames(windows, renderer, 8) ||
        !expect(bgfx::getStats()->numDraw == retained_draws,
                "only surviving meshes remain in the GPU cache"))
    {
        return false;
    }
    world.destroy_scene();
    return render_frames(windows, renderer, 8) &&
           expect(gpu_counts() == idle, "full stop releases retained mesh resources");
}

struct Screenshot
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::array<std::uint8_t, 3>> pixels;

    std::array<std::uint8_t, 3> pixel(std::uint32_t x, std::uint32_t y) const
    {
        return pixels[static_cast<std::size_t>(y) * width + x];
    }
};

bool read_screenshot(const std::filesystem::path& path, Screenshot& output)
{
    engine::ByteBuffer bytes;
    if (!engine::ResourceSystem::read_binary(path, bytes) || bytes.size() < 18)
    {
        return false;
    }
    const auto byte = [&bytes](std::size_t index)
    { return std::to_integer<std::uint8_t>(bytes[index]); };
    const auto type = byte(2);
    const auto pixel_size = byte(16) / 8;
    const std::uint32_t width = byte(12) | (static_cast<std::uint32_t>(byte(13)) << 8);
    const std::uint32_t height = byte(14) | (static_cast<std::uint32_t>(byte(15)) << 8);
    if (byte(1) != 0 || (type != 2 && type != 10) || (pixel_size != 3 && pixel_size != 4) ||
        width == 0 || height == 0 || width > 4096 || height > 4096)
    {
        return false;
    }
    output.width = width;
    output.height = height;
    output.pixels.resize(static_cast<std::size_t>(width) * height);
    std::size_t offset = 18 + byte(0);
    std::size_t current = 0;
    const auto read_pixel = [&]()
    {
        const std::array<std::uint8_t, 3> result = {byte(offset + 2), byte(offset + 1),
                                                    byte(offset)};
        offset += pixel_size;
        return result;
    };
    while (current < output.pixels.size())
    {
        if (offset >= bytes.size())
        {
            return false;
        }
        const auto header = type == 10 ? byte(offset++) : 0;
        const std::size_t count = type == 10 ? (header & 127) + 1 : 1;
        const bool repeated = (header & 128) != 0;
        if (count > output.pixels.size() - current ||
            (repeated ? 1 : count) * pixel_size > bytes.size() - offset)
        {
            return false;
        }
        std::array<std::uint8_t, 3> color{};
        if (repeated)
        {
            color = read_pixel();
        }
        for (std::size_t item = 0; item < count; ++item)
        {
            if (!repeated)
            {
                color = read_pixel();
            }
            const auto column = static_cast<std::uint32_t>(current % width);
            const auto row = static_cast<std::uint32_t>(current / width);
            const auto x = (byte(17) & 16) != 0 ? width - 1 - column : column;
            const auto y = (byte(17) & 32) != 0 ? row : height - 1 - row;
            output.pixels[static_cast<std::size_t>(y) * width + x] = color;
            ++current;
        }
    }
    return true;
}

bool screenshot(engine::WindowSystem& windows, engine::RenderSystem& renderer,
                const std::filesystem::path& root, const char* name, Screenshot& output,
                engine::Camera camera = {})
{
    const auto path = root / name;
    const auto native_path = engine::PathUtils::to_utf8(path);
    bgfx::requestScreenShot(BGFX_INVALID_HANDLE, native_path.c_str());
    if (!render_frames(windows, renderer, 8, camera))
    {
        return false;
    }
    for (int attempt = 0; attempt < max_frame_attempts &&
                          renderer.screenshot_status() == engine::ScreenshotStatus::pending;
         ++attempt)
    {
        if (!render_frames(windows, renderer, 1, camera))
        {
            return false;
        }
    }
    if (!expect(renderer.screenshot_status() == engine::ScreenshotStatus::complete,
                "GPU readback is encoded and written before inspecting pixels"))
    {
        return false;
    }
    // bgfx 的默认回调保存 TGA；读取实际 GPU 输出，不给正式接口添加测试入口。
    auto image_path = path;
    image_path += ".tga";
    if (const char* artifacts = std::getenv("ENGINE_RENDER_TEST_ARTIFACTS");
        artifacts != nullptr && *artifacts != '\0')
    {
        const auto destination = engine::PathUtils::from_utf8(artifacts);
        std::error_code error;
        std::filesystem::create_directories(destination, error);
        if (!expect(!error, "create render review artifact directory"))
        {
            return false;
        }
        std::filesystem::copy_file(image_path, destination / image_path.filename(),
                                   std::filesystem::copy_options::overwrite_existing, error);
        if (!expect(!error, "preserve screenshot for render review"))
        {
            return false;
        }
    }
    return expect(read_screenshot(image_path, output), "read screenshot of real GPU output");
}

engine::schema::MeshDataT textured_quad()
{
    engine::schema::MeshDataT data;
    data.path = "meshes/textured-quad";
    data.vertices = {
        {-1, -1, 0, 0, 0, 1}, {1, -1, 0, 0, 0, 1}, {1, 1, 0, 0, 0, 1}, {-1, 1, 0, 0, 0, 1}};
    data.indices = {0, 1, 2, 0, 2, 3};
    data.texcoords = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    data.tangents.assign(4, {1, 0, 0, 1});
    data.colors.assign(4, {1, 1, 1, 1});
    return data;
}

engine::schema::TextureDataT test_texture(const char* path, bool srgb, bool blocks)
{
    engine::schema::TextureDataT data;
    data.path = path;
    data.width = 8;
    data.height = 8;
    data.srgb = srgb;
    data.encoding = engine::schema::TextureEncoding::rgba8;
    data.format = engine::schema::TextureFormat::rgba8;
    for (std::uint32_t y = 0; y < data.height; ++y)
    {
        for (std::uint32_t x = 0; x < data.width; ++x)
        {
            const bool bottom = y >= 4;
            const bool right = x >= 4;
            data.data.push_back(blocks ? ((!bottom && !right) || (bottom && right) ? 255 : 0)
                                       : 128);
            data.data.push_back(blocks ? (right ? 255 : 0) : 128);
            data.data.push_back(blocks ? (bottom ? 255 : 0) : 128);
            data.data.push_back(255);
        }
    }
    return data;
}

bool near_color(const std::array<std::uint8_t, 3>& color, int r, int g, int b)
{
    const bool matched = std::abs(static_cast<int>(color[0]) - r) <= 12 &&
                         std::abs(static_cast<int>(color[1]) - g) <= 12 &&
                         std::abs(static_cast<int>(color[2]) - b) <= 12;
    if (!matched)
    {
        std::fprintf(stderr, "GPU pixel: actual (%u,%u,%u), expected (%d,%d,%d)\n", color[0],
                     color[1], color[2], r, g, b);
    }
    return matched;
}

bool test_scene_sampling(const engine::EngineConfig& config)
{
    engine::ResourceSystem resources;
    engine::WorldSystem world;
    engine::WindowSystem windows;
    engine::RenderSystem renderer(windows, &world);
    test_support::ResourceFixture fixture;
    if (!expect(!renderer.set_msaa_samples(4) && !renderer.supports_msaa_samples(1) &&
                    !renderer.set_max_anisotropy(true) && !renderer.supports_max_anisotropy(),
                "sampling controls reject an unavailable GPU") ||
        !resources.initialize(config) || !world.initialize(config) || !windows.initialize(config) ||
        !renderer.initialize(config) || !render_frames(windows, renderer, 4))
    {
        return false;
    }
    const auto idle = gpu_counts();
    if (!expect(renderer.msaa_samples() == 1 && !renderer.max_anisotropy() &&
                    renderer.supports_msaa_samples(1) && !renderer.supports_msaa_samples(0) &&
                    !renderer.supports_msaa_samples(3) && !renderer.supports_msaa_samples(16) &&
                    !renderer.set_msaa_samples(0) && !renderer.set_msaa_samples(16),
                "sampling defaults and exact supported sample counts are explicit") ||
        !create_cube_scene(world, true))
    {
        return false;
    }
    auto* scene = world.scene();
    auto mesh = test_support::scene_nodes(*scene).front().get_component<engine::MeshRenderer>();
    auto environment =
        scene->create_node("Sampling background").add_component<engine::EnvironmentComponent>();
    auto background = environment.settings();
    background.clear_color = 0x000000ff;
    Screenshot baseline;
    if (!mesh || !environment || !environment.set_settings(background) ||
        !mesh.material().set_v4("baseColor", {1, 1, 1, 1}) ||
        !screenshot(windows, renderer, fixture.root, "msaa-off", baseline))
    {
        return false;
    }
    const auto partial_edges = [](const Screenshot& image)
    {
        return std::count_if(image.pixels.begin(), image.pixels.end(),
                             [](const auto& pixel) { return pixel[0] > 8 && pixel[0] < 245; });
    };
    const auto baseline_edges = partial_edges(baseline);
    const auto prepared_resources = renderer.gpu_resource_count();
    std::uint8_t supported_samples = 1;
    for (const std::uint8_t samples : {2, 4, 8})
    {
        if (!renderer.supports_msaa_samples(samples))
        {
            if (!expect(!renderer.set_msaa_samples(samples) &&
                            renderer.msaa_samples() == supported_samples,
                        "unsupported device sample counts reject without silent downgrade"))
            {
                return false;
            }
            continue;
        }
        supported_samples = samples;
        Screenshot multisampled;
        const auto name = "msaa-" + std::to_string(samples);
        if (!expect(renderer.set_msaa_samples(samples) && renderer.msaa_samples() == samples,
                    "accepted sample count is immediately queryable") ||
            !screenshot(windows, renderer, fixture.root, name.c_str(), multisampled) ||
            !expect(
                partial_edges(multisampled) > baseline_edges + 20,
                "MSAA resolves real geometry coverage through offscreen HDR and transparency") ||
            !expect(renderer.set_msaa_samples(samples) && render_frames(windows, renderer, 2) &&
                        renderer.gpu_resource_count() == prepared_resources,
                    "repeating a sample count retains the current target allocation"))
        {
            return false;
        }
        std::fprintf(stderr, "scene sampling: MSAA %u partial edges %td -> %td\n", samples,
                     baseline_edges, partial_edges(multisampled));
    }
    if (!resize_window(windows, renderer) ||
        !SDL_SetWindowSize(windows.native_window(), config.width, config.height) ||
        !renderer.set_msaa_samples(1))
    {
        return false;
    }
    Screenshot restored;
    if (!screenshot(windows, renderer, fixture.root, "msaa-restored", restored) ||
        !expect(restored.pixels == baseline.pixels && renderer.msaa_samples() == 1,
                "resize and MSAA disable restore exact original single-sample pixels"))
    {
        return false;
    }
    engine::schema::TextureDataT checker;
    checker.path = "textures/sampling-checker";
    checker.width = checker.height = 256;
    checker.mip_count = 9;
    checker.srgb = false;
    checker.encoding = engine::schema::TextureEncoding::basis;
    checker.format = engine::schema::TextureFormat::rgba8;
    std::vector<std::uint8_t> checker_pixels;
    for (std::uint32_t y = 0; y < checker.height; ++y)
    {
        for (std::uint32_t x = 0; x < checker.width; ++x)
        {
            const auto value = static_cast<std::uint8_t>(((x / 32) % 2) ? 255 : 0);
            checker_pixels.insert(checker_pixels.end(), {value, value, value, 255});
        }
    }
    if (!expect(basisu::basisu_encoder_init(), "initialize Basis encoder for real mip fixture"))
    {
        return false;
    }
    std::size_t basis_size = 0;
    void* basis = basisu::basis_compress(basist::basis_tex_format::cUASTC_LDR_4x4,
                                         checker_pixels.data(), checker.width, checker.height,
                                         checker.width, basisu::cFlagGenMipsWrap, 0, &basis_size);
    if (!expect(basis != nullptr && basis_size != 0, "encode full mip chain through Basis"))
    {
        return false;
    }
    const auto* basis_bytes = static_cast<const std::uint8_t*>(basis);
    checker.data.assign(basis_bytes, basis_bytes + basis_size);
    basisu::basis_free_data(basis);
    auto plane = textured_quad();
    for (auto& uv : plane.texcoords)
    {
        uv.mutate_x(uv.x() * 16);
        uv.mutate_y(uv.y() * 16);
    }
    auto transform = mesh.node().get_component<engine::Transform>();
    engine::TransformSettings pose;
    pose.rotation = {80, 0, 0};
    auto camera = scene->active_camera();
    auto lens = camera.settings();
    lens.projection = engine::schema::Projection::orthographic;
    lens.orthographic_size = 2;
    engine::TransformSettings camera_pose;
    camera_pose.position = {0, 0, 3};
    if (!expect(fixture.add(checker) && fixture.publish(resources),
                "publish mipmapped sampler fixture") ||
        !mesh.set_mesh_data(plane) ||
        !mesh.material().set_type("builtin://materials/default-unlit") ||
        !mesh.material().set_v4("baseColor", {1, 1, 1, 1}) ||
        !mesh.material().set_cull_mode(engine::schema::CullMode::none) ||
        !mesh.material().set_texture("baseColorTexture", checker.path) ||
        !transform.set_settings(pose) || !camera.set_settings(lens) ||
        !camera.node().get_component<engine::Transform>().set_settings(camera_pose))
    {
        return false;
    }
    Screenshot linear, anisotropic, linear_restored;
    if (!screenshot(windows, renderer, fixture.root, "anisotropy-off", linear))
    {
        return false;
    }
    if (renderer.supports_max_anisotropy())
    {
        if (!renderer.set_max_anisotropy(true) || !renderer.max_anisotropy() ||
            !screenshot(windows, renderer, fixture.root, "anisotropy-on", anisotropic))
        {
            return false;
        }
        std::size_t changed = 0;
        for (std::size_t index = 0; index < linear.pixels.size(); ++index)
        {
            changed += std::abs(static_cast<int>(linear.pixels[index][0]) -
                                anisotropic.pixels[index][0]) > 8;
        }
        std::fprintf(stderr, "scene sampling: anisotropy changed pixels %zu\n", changed);
        if (!expect(changed > 100,
                    "anisotropic material sampler changes oblique mipmapped texture pixels") ||
            !renderer.set_max_anisotropy(false) || renderer.max_anisotropy() ||
            !screenshot(windows, renderer, fixture.root, "anisotropy-restored", linear_restored) ||
            !expect(linear_restored.pixels == linear.pixels,
                    "disabling anisotropy restores linear filtering without stale sampler cache"))
        {
            return false;
        }
    }
    else if (!expect(!renderer.set_max_anisotropy(true) && !renderer.max_anisotropy(),
                     "unsupported anisotropy never reports accepted"))
    {
        return false;
    }
    world.destroy_scene();
    renderer.release_scene();
    return render_frames(windows, renderer, 8) &&
           expect(renderer.gpu_resource_count() == 0 && gpu_counts() == idle,
                  "sampling changes and resize leave no retired scene GPU resources");
}

bool test_gpu_skinning(const engine::EngineConfig& config)
{
    engine::ResourceSystem resources;
#if defined(ENGINE_TEST_RESOURCE_GRAPH)
    engine::plugins::resource_graph::ResourceGraphSystem graph(resources);
#endif
    engine::WorldSystem world;
    engine::WindowSystem windows;
    engine::RenderSystem renderer(windows, &world);
    test_support::ResourceFixture fixture;
    if (!resources.initialize(config) || !world.initialize(config) || !windows.initialize(config) ||
        !renderer.initialize(config))
    {
        return false;
    }
    const auto skeleton = test_support::animation::skeleton(false);
    auto mesh = textured_quad();
    mesh.path = "meshes/gpu-skinning";
    mesh.skeleton_guid = test_support::animation::target_guid;
    mesh.mesh_to_skeleton = test_support::animation::translation(0, 0, 0);
    mesh.skin.assign(mesh.vertices.size(), {0, 0, 0, 0, 1, 0, 0, 0});
    for (auto& vertex : mesh.vertices)
    {
        vertex.mutate_nx(0.577350269F);
        vertex.mutate_ny(0.577350269F);
        vertex.mutate_nz(0.577350269F);
    }
    if (!engine::MeshUtils::build_tangents(mesh, mesh.tangents))
    {
        return false;
    }
    flatbuffers::FlatBufferBuilder skeleton_builder;
    engine::schema::FinishSkeletonDataBuffer(
        skeleton_builder, engine::schema::SkeletonData::Pack(skeleton_builder, &skeleton));
    flatbuffers::FlatBufferBuilder mesh_builder;
    engine::schema::FinishMeshDataBuffer(mesh_builder,
                                         engine::schema::MeshData::Pack(mesh_builder, &mesh));
    if (!test_support::animation::add(fixture, test_support::animation::target_guid, skeleton.path,
                                      engine::schema::AssetKind::skeleton, skeleton_builder) ||
        !test_support::animation::add(fixture, test_support::animation::mesh_guid, mesh.path,
                                      engine::schema::AssetKind::mesh, mesh_builder,
                                      {test_support::animation::target_guid}) ||
        !fixture.publish(resources))
    {
        return false;
    }
#if defined(ENGINE_TEST_RESOURCE_GRAPH)
    if (!graph.initialize(config))
    {
        return false;
    }
    resources.set_loader(&graph);
#endif
    if (!render_frames(windows, renderer, 4))
    {
        return false;
    }
    const auto idle = gpu_counts();
    auto* scene = world.create_scene("Independent GPU poses");
    auto camera_node = scene ? scene->create_node("Camera") : engine::Node{};
    auto camera = camera_node ? camera_node.add_component<engine::Camera>() : engine::Camera{};
    auto light_node = scene ? scene->create_node("Light") : engine::Node{};
    auto light = light_node ? light_node.add_component<engine::Light>() : engine::Light{};
    if (!camera || !light)
    {
        return false;
    }
    auto camera_settings = camera.settings();
    camera_settings.projection = engine::schema::Projection::orthographic;
    camera_settings.orthographic_size = 2;
    auto pose = camera_node.get_component<engine::Transform>().settings();
    pose.position = {0, 0, 3};
    if (!camera.set_settings(camera_settings) ||
        !camera_node.get_component<engine::Transform>().set_settings(pose) ||
        !scene->set_active_camera(camera))
    {
        return false;
    }
    const std::array<float, 16> first_matrix{0.5F, 0, 0,    0, 0,  0.8F, 0, 0,
                                             0,    0, 0.4F, 0, -1, 0,    0, 1};
    const std::array<float, 16> first_normal{2, 0, 0, 0, 0, 1.25F, 0, 0, 0, 0, 2.5F, 0, 0, 0, 0, 1};
    auto second_matrix = first_matrix;
    auto second_normal = first_normal;
    second_matrix[0] = -0.5F;
    second_matrix[12] = 1;
    second_normal[0] = -2;
    const auto make_renderer = [&](const char* name, const auto& transform, const auto& normal)
    {
        auto node = scene->create_node(name);
        auto result = node ? node.add_component<engine::MeshRenderer>() : engine::MeshRenderer{};
        if (!result || !result.set_mesh(mesh.path) ||
            !result.material().set_cull_mode(engine::schema::CullMode::none) ||
            !result.material().set_v4("baseColor", {0.7, 0.4, 0.2, 1}) ||
            !result.set_skinning_palette({&transform, 1}, {&normal, 1}))
        {
            return engine::MeshRenderer{};
        }
        return result;
    };
    auto first = make_renderer("First", first_matrix, first_normal);
    if (!expect(first.valid(), "submit first GPU pose") || !render_frames(windows, renderer, 4))
    {
        return false;
    }
    const auto single = gpu_counts();
#if defined(ENGINE_TEST_RESOURCE_GRAPH)
    const auto single_resource_count = renderer.gpu_resource_count();
#endif
    auto second = make_renderer("Second", second_matrix, second_normal);
    if (!expect(second.valid(), "submit an independent mirrored GPU pose") ||
        !render_frames(windows, renderer, 4) ||
        !expect(gpu_counts().dynamic_vertices == single.dynamic_vertices + 2,
                "each character owns one palette buffer and one output vertex buffer"))
    {
        return false;
    }
#if defined(ENGINE_TEST_RESOURCE_GRAPH)
    // bgfx 的 numVertexBuffers 同时计入每个 compute dynamic buffer 的底层存储。
    if (!expect(first.mesh_resource() == second.mesh_resource() &&
                    gpu_counts().vertices == single.vertices + 2 &&
                    gpu_counts().indices == single.indices &&
                    renderer.gpu_resource_count() == single_resource_count + 2,
                "shared immutable mesh uploads one source stream, skin stream and index buffer"))
    {
        return false;
    }
#endif
    Screenshot gpu_pose;
    Screenshot cpu_reference;
    if (!screenshot(windows, renderer, fixture.root, "gpu-skinning", gpu_pose))
    {
        return false;
    }
    const auto reference =
        [&](engine::MeshRenderer target, const auto& transform, const auto& normal)
    {
        auto vertices = mesh.vertices;
        auto tangents = mesh.tangents;
        for (std::size_t index = 0; index < vertices.size(); ++index)
        {
            const auto& input = mesh.vertices[index];
            const auto& tangent = mesh.tangents[index];
            double nx = input.nx() * normal[0], ny = input.ny() * normal[5],
                   nz = input.nz() * normal[10];
            const double normal_length = std::hypot(nx, ny, nz);
            nx /= normal_length;
            ny /= normal_length;
            nz /= normal_length;
            double tx = tangent.x() * transform[0], ty = tangent.y() * transform[5],
                   tz = tangent.z() * transform[10];
            const double dot = nx * tx + ny * ty + nz * tz;
            tx -= nx * dot;
            ty -= ny * dot;
            tz -= nz * dot;
            const double tangent_length = std::hypot(tx, ty, tz);
            tx /= tangent_length;
            ty /= tangent_length;
            tz /= tangent_length;
            vertices[index] = {input.x() * transform[0] + transform[12],
                               input.y() * transform[5],
                               input.z() * transform[10],
                               static_cast<float>(nx),
                               static_cast<float>(ny),
                               static_cast<float>(nz)};
            tangents[index] = {tx, ty, tz, tangent.w() * (transform[0] < 0 ? -1 : 1)};
        }
        return target.set_deformation(vertices, tangents);
    };
    if (!reference(first, first_matrix, first_normal) ||
        !reference(second, second_matrix, second_normal) || !render_frames(windows, renderer, 4) ||
        !screenshot(windows, renderer, fixture.root, "cpu-skinning-reference", cpu_reference) ||
        gpu_pose.pixels.size() != cpu_reference.pixels.size())
    {
        return false;
    }
    std::size_t mismatches = 0;
    std::size_t lit_pixels = 0;
    for (std::size_t pixel = 0; pixel < gpu_pose.pixels.size(); ++pixel)
    {
        int difference = 0;
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            difference += std::abs(static_cast<int>(gpu_pose.pixels[pixel][channel]) -
                                   cpu_reference.pixels[pixel][channel]);
        }
        mismatches += difference > 6;
        lit_pixels += gpu_pose.pixels[pixel][0] > 80;
    }
    if (!expect(
            lit_pixels > 100 && mismatches < gpu_pose.pixels.size() / 1000 + 1,
            "independent GPU poses match CPU positions, nonuniform normals and mirrored tangents"))
    {
        return false;
    }
    world.destroy_scene();
    renderer.release_scene();
    return render_frames(windows, renderer, 4) &&
           expect(gpu_counts() == idle,
                  "shared geometry and per-instance skinning buffers retire together");
}

bool test_textured_renderer(const engine::EngineConfig& config, bool shared)
{
    engine::ThreadSystem threads({.worker_limit = 2});
    engine::TaskSystem tasks(threads, {.name = "Texture preparation", .max_concurrency = 2});
    engine::ResourceSystem resources;
#if defined(ENGINE_TEST_RESOURCE_GRAPH)
    engine::plugins::resource_graph::ResourceGraphSystem graph(resources);
#endif
    engine::WorldSystem world;
    engine::WindowSystem windows;
    engine::RenderSystem renderer(windows, &world, nullptr, nullptr, shared ? &tasks : nullptr);
    test_support::ResourceFixture fixture;
    if (!expect((!shared || (threads.initialize(config) && tasks.initialize(config))) &&
                    resources.initialize(config) && world.initialize(config) &&
                    windows.initialize(config) && renderer.initialize(config),
                "initialize texture rendering systems") ||
        !expect(fixture.add(test_texture("textures/blocks", true, true)) &&
                    fixture.add(test_texture("textures/gray-srgb", true, false)) &&
                    fixture.add(test_texture("textures/gray-linear", false, false)) &&
                    fixture.publish(resources),
                "publish texture fixtures through the resource catalog") ||
        !render_frames(windows, renderer, 8))
    {
        return false;
    }
#if defined(ENGINE_TEST_RESOURCE_GRAPH)
    if (shared)
    {
        if (!expect(graph.initialize(config), "initialize shared texture loader"))
        {
            return false;
        }
        resources.set_loader(&graph);
    }
#endif
    const auto idle = gpu_counts();
    auto* scene = world.create_scene("Texture GPU test");
    auto quad_node = scene ? scene->create_node("Quad") : engine::Node{};
    auto camera_node = scene ? scene->create_node("Camera") : engine::Node{};
    auto mesh =
        quad_node ? quad_node.add_component<engine::MeshRenderer>() : engine::MeshRenderer{};
    auto camera = camera_node ? camera_node.add_component<engine::Camera>() : engine::Camera{};
    engine::TransformSettings camera_transform;
    camera_transform.position = {0, 0, 3};
    engine::CameraSettings camera_settings;
    camera_settings.projection = engine::schema::Projection::orthographic;
    camera_settings.orthographic_size = 2;
    auto data = textured_quad();
    if (!expect(mesh && camera && camera.set_settings(camera_settings) &&
                    camera_node.get_component<engine::Transform>().set_settings(camera_transform) &&
                    scene->set_active_camera(camera) && mesh.set_mesh_data(data) &&
                    mesh.material().set_type("builtin://materials/default-unlit") &&
                    mesh.material().set_v4("baseColor", {1, 1, 1, 1}) &&
                    mesh.material().set_texture("baseColorTexture", "textures/blocks"),
                "assemble orthographic textured quad") ||
        !render_frames(windows, renderer, 8))
    {
        return false;
    }
    Screenshot image;
    if (!screenshot(windows, renderer, fixture.root, "blocks", image))
    {
        return false;
    }
    const auto cx = image.width / 2;
    const auto cy = image.height / 2;
    const auto sample = image.height / 8;
    if (!expect(near_color(image.pixel(cx - sample, cy - sample), 255, 0, 0) &&
                    near_color(image.pixel(cx + sample, cy - sample), 0, 255, 0) &&
                    near_color(image.pixel(cx - sample, cy + sample), 0, 0, 255) &&
                    near_color(image.pixel(cx + sample, cy + sample), 255, 255, 255),
                "GPU samples all texture quadrants with the expected UV orientation"))
    {
        return false;
    }
    if (!expect(mesh.material().set_texture("baseColorTexture", "textures/gray-srgb"),
                "select sRGB gray texture") ||
        !render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "gray-srgb", image) ||
        !expect(near_color(image.pixel(cx, cy), 128, 128, 128),
                "sRGB sampling and display encoding preserve middle gray") ||
        !expect(mesh.material().set_texture("baseColorTexture", "textures/gray-linear"),
                "select linear gray texture") ||
        !render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "gray-linear", image) ||
        !expect(near_color(image.pixel(cx, cy), 188, 188, 188),
                "linear data receives display encoding exactly once"))
    {
        return false;
    }

    const auto single_mesh = gpu_counts();
    const auto single_draws = bgfx::getStats()->numDraw;
    const auto single_resource_count = renderer.gpu_resource_count();
    const auto source_revision = mesh.mesh_revision();
    auto deformed = data.vertices;
    for (auto& vertex : deformed)
    {
        vertex.mutate_x(vertex.x() + 5);
    }
    if (!expect(mesh.set_deformation(deformed, data.tangents), "submit transient deformed pose") ||
        !render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "deformed-vertices", image) ||
        !expect(image.pixel(cx, cy)[0] < 80 && gpu_counts().indices == single_mesh.indices &&
                    gpu_counts().dynamic_vertices == single_mesh.dynamic_vertices + 1 &&
                    renderer.gpu_resource_count() == single_resource_count + 1 &&
                    mesh.mesh_revision() == source_revision,
                "pose changes GPU pixels with one dynamic VB and unchanged source topology"))
    {
        return false;
    }
    const auto active_pose = gpu_counts();
    if (!expect(mesh.set_deformation(data.vertices, data.tangents), "update existing dynamic VB") ||
        !render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "deformed-rest-pose", image) ||
        !expect(near_color(image.pixel(cx, cy), 188, 188, 188) && gpu_counts() == active_pose,
                "another pose updates the existing GPU allocation"))
    {
        return false;
    }
    mesh.clear_deformation();
    if (!render_frames(windows, renderer, 8) ||
        !expect(gpu_counts() == single_mesh &&
                    renderer.gpu_resource_count() == single_resource_count,
                "clearing deformation releases dynamic VB and restores source buffers"))
    {
        return false;
    }
    if (!mesh.set_visible(false))
    {
        std::abort();
    }
    if (!render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "hidden-mesh", image) ||
        !expect(image.pixel(cx, cy)[0] < 80 && mesh.enabled(),
                "transient visibility removes GPU draw without disabling the component"))
    {
        return false;
    }
    if (!mesh.set_visible(true))
    {
        std::abort();
    }
    if (!render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "visible-mesh-restored", image) ||
        !expect(near_color(image.pixel(cx, cy), 188, 188, 188) && gpu_counts() == single_mesh,
                "showing mesh restores the same pixels and resource allocation"))
    {
        return false;
    }
    auto visibility_parent = scene->create_node("Visibility parent");
    if (!expect(visibility_parent.valid() && quad_node.set_parent(visibility_parent),
                "group the mesh under a parent without changing its transform"))
    {
        return false;
    }
    if (!visibility_parent.set_visible(false))
    {
        std::abort();
    }
    if (!render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "hidden-parent", image) ||
        !expect(image.pixel(cx, cy)[0] < 80 && mesh.active() && mesh.visible() &&
                    !mesh.visible_in_hierarchy(),
                "a hidden ancestor suppresses mesh pixels while preserving local active state"))
    {
        return false;
    }
    if (!visibility_parent.set_visible(true))
    {
        std::abort();
    }
    if (!visibility_parent.set_enabled(false))
    {
        std::abort();
    }
    if (!render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "disabled-parent", image) ||
        !expect(image.pixel(cx, cy)[0] < 80 && mesh.enabled() && !mesh.active(),
                "a disabled ancestor suppresses mesh rendering without changing local enabled"))
    {
        return false;
    }
    if (!visibility_parent.set_enabled(true))
    {
        std::abort();
    }
    if (!render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "parent-restored", image) ||
        !expect(near_color(image.pixel(cx, cy), 188, 188, 188) && gpu_counts() == single_mesh,
                "restoring parent state restores mesh pixels and the original GPU allocation"))
    {
        return false;
    }
    if (!shared)
    {
        auto observer_node = scene->create_node("Observer camera");
        auto observer =
            observer_node ? observer_node.add_component<engine::Camera>() : engine::Camera{};
        const std::array hidden = {mesh.instance_id(), mesh.instance_id()};
        const std::array invalid_hidden = {mesh.instance_id(), std::uint64_t{0}};
        engine::schema::ComponentDataT saved_camera;
        if (!expect(observer.valid() && observer.set_settings(camera_settings) &&
                        observer_node.get_component<engine::Transform>().set_settings(
                            camera_transform) &&
                        camera.set_hidden_meshes(hidden) &&
                        !camera.set_hidden_meshes(invalid_hidden) &&
                        !camera.mesh_visible(hidden.front()) && !camera.mesh_visible(0) &&
                        observer.mesh_visible(hidden.front()) && camera.build_data(saved_camera),
                    "camera filter is independent, accepts duplicate IDs and rejects zero "
                    "without replacing the prior list") ||
            !render_frames(windows, renderer, 8, camera) ||
            !screenshot(windows, renderer, fixture.root, "camera-hidden", image, camera) ||
            !expect(image.pixel(cx, cy)[0] < 80 && mesh.visible() && mesh.enabled() &&
                        gpu_counts() == single_mesh &&
                        renderer.gpu_resource_count() == single_resource_count,
                    "camera hides the only mesh while preserving global state and GPU resources") ||
            !render_frames(windows, renderer, 8, observer) ||
            !screenshot(windows, renderer, fixture.root, "camera-observer", image, observer) ||
            !expect(near_color(image.pixel(cx, cy), 188, 188, 188) &&
                        scene->active_camera() == camera && !camera.mesh_visible(hidden.front()) &&
                        gpu_counts() == single_mesh,
                    "explicit observer sees a mesh hidden from the active camera without "
                    "changing either view"))
        {
            return false;
        }
        auto invalid_camera = saved_camera;
        invalid_camera.payload.AsCameraData()->fov_degrees = 0.0F;
        if (!expect(!camera.restore_data(invalid_camera) && !camera.mesh_visible(hidden.front()),
                    "failed camera restore preserves its hidden list"))
        {
            return false;
        }
        camera.clear_hidden_meshes();
        if (!render_frames(windows, renderer, 8, camera) ||
            !screenshot(windows, renderer, fixture.root, "camera-filter-cleared", image, camera) ||
            !expect(near_color(image.pixel(cx, cy), 188, 188, 188),
                    "clearing the camera filter restores its mesh pixels") ||
            !expect(camera.set_hidden_meshes(hidden) &&
                        quad_node.remove_component(engine::MeshRenderer::component_type),
                    "remove the hidden mesh component while retaining the camera filter"))
        {
            return false;
        }
        mesh = quad_node.add_component<engine::MeshRenderer>();
        if (!expect(mesh.valid() && mesh.instance_id() != hidden.front() &&
                        camera.mesh_visible(mesh.instance_id()) &&
                        !camera.mesh_visible(hidden.front()) && mesh.set_mesh_data(data) &&
                        mesh.material().set_type("builtin://materials/default-unlit") &&
                        mesh.material().set_v4("baseColor", {1, 1, 1, 1}) &&
                        mesh.material().set_texture("baseColorTexture", "textures/gray-linear"),
                    "rebuilt component has a new identity outside the old filter") ||
            !render_frames(windows, renderer, 8, camera) ||
            !screenshot(windows, renderer, fixture.root, "camera-rebuilt-mesh", image, camera) ||
            !expect(near_color(image.pixel(cx, cy), 188, 188, 188) && gpu_counts() == single_mesh,
                    "camera renders the replacement component and releases old GPU buffers") ||
            !expect(camera.restore_data(saved_camera) && camera.mesh_visible(hidden.front()),
                    "restoring persistent camera data clears transient hidden IDs"))
        {
            return false;
        }
    }
    for (auto& vertex : data.vertices)
    {
        vertex.mutate_x(vertex.x() + 5);
    }
    if (!expect(mesh.set_mesh_data(data), "submit changed vertex positions") ||
        !render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "moved-vertices", image) ||
        !expect(image.pixel(cx, cy)[0] < 80 && gpu_counts() == single_mesh,
                "dynamic geometry changes pixels and retires replaced GPU buffers") ||
        !expect(mesh.set_mesh_data(textured_quad()), "restore quad geometry"))
    {
        return false;
    }

    const auto texture_increment = shared ? 0 : mesh.material().shader_data()->samplers.size();
    auto second_node = scene->create_node("Second texture quad");
    auto second =
        second_node ? second_node.add_component<engine::MeshRenderer>() : engine::MeshRenderer{};
    engine::TransformSettings second_transform;
    second_transform.position = {2, 0, -0.1};
    if (!expect(second && second.set_mesh_data(textured_quad()) &&
                    second_node.get_component<engine::Transform>().set_settings(second_transform) &&
                    second.material().set_type("builtin://materials/default-unlit") &&
                    second.material().set_texture("baseColorTexture", "textures/gray-linear"),
                "add another mesh referencing the same texture path") ||
        !render_frames(windows, renderer, 8) ||
        !expect(bgfx::getStats()->numDraw == single_draws + 3 &&
                    gpu_counts().textures == single_mesh.textures + texture_increment &&
                    gpu_counts().vertices == single_mesh.vertices + 1,
                "GPU uploads follow entity ownership: distinct in core, shared through graph"))
    {
        return false;
    }
    data = textured_quad();
    data.submeshes = {{0, 3, 0}, {3, 3, 1}};
    if (!expect(mesh.set_material_count(2) && mesh.material(1) &&
                    mesh.material(1).set_type("builtin://materials/default-unlit") &&
                    mesh.material(1).set_texture("baseColorTexture", "textures/gray-linear") &&
                    mesh.set_mesh_data(data),
                "assign independent material slots to two submeshes") ||
        !render_frames(windows, renderer, 8) ||
        !expect(bgfx::getStats()->numDraw == single_draws + 6 &&
                    gpu_counts().textures == single_mesh.textures + 2 * texture_increment,
                "submesh material textures obey the selected resource loading policy"))
    {
        return false;
    }
    if (!expect(mesh.material().set_type("builtin://materials/default-pbr") &&
                    mesh.material(1).set_type("builtin://materials/default-pbr"),
                "enable PBR on both submesh materials"))
    {
        return false;
    }
    const auto multi_slot = gpu_counts();
    if (!expect(mesh.set_deformation(data.vertices, data.tangents),
                "deformation supports multiple material slots") ||
        !render_frames(windows, renderer, 8) ||
        !expect(bgfx::getStats()->numDraw == single_draws + 6 && mesh.material_count() == 2 &&
                    gpu_counts().indices == multi_slot.indices,
                "deformed submeshes retain material submissions and shared topology"))
    {
        return false;
    }
    mesh.clear_deformation();
    std::vector<engine::Light> lights;
    for (const auto kind : {engine::schema::LightKind::directional,
                            engine::schema::LightKind::point, engine::schema::LightKind::spot})
    {
        auto node = scene->create_node("Light");
        auto light = node ? node.add_component<engine::Light>() : engine::Light{};
        engine::LightSettings settings;
        settings.kind = kind;
        engine::TransformSettings transform;
        transform.position = {0, 0, 2};
        if (!expect(light && light.set_settings(settings) &&
                        node.get_component<engine::Transform>().set_settings(transform),
                    "create directional, point and spot lights"))
        {
            return false;
        }
        if (!light.set_enabled(false))
        {
            std::abort();
        }
        lights.push_back(light);
    }
    const auto brightness = [](const std::array<std::uint8_t, 3>& color)
    { return static_cast<int>(color[0]) + color[1] + color[2]; };
    if (!render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "unlit-pbr", image))
    {
        return false;
    }
    const auto dark = brightness(image.pixel(cx, cy));
    int brightest_single = dark;
    for (std::size_t index = 0; index < lights.size(); ++index)
    {
        if (!lights[index].set_enabled(true))
        {
            std::abort();
        }
        const auto name = "light-" + std::to_string(index);
        if (!render_frames(windows, renderer, 8) ||
            !screenshot(windows, renderer, fixture.root, name.c_str(), image) ||
            !expect(brightness(image.pixel(cx, cy)) > dark + 20,
                    "each directional, point and spot light contributes visible PBR lighting"))
        {
            return false;
        }
        brightest_single = std::max(brightest_single, brightness(image.pixel(cx, cy)));
        if (!lights[index].set_enabled(false))
        {
            std::abort();
        }
    }
    for (auto light : lights)
    {
        if (!light.set_enabled(true))
        {
            std::abort();
        }
    }
    if (!render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "all-lights", image) ||
        !expect(brightness(image.pixel(cx, cy)) > brightest_single + 5,
                "multiple light contributions accumulate in the rendered pixel") ||
        !expect(bgfx::getStats()->numDraw == single_draws + 6,
                "multiple lights preserve mesh draw count"))
    {
        return false;
    }
    world.destroy_scene();
    renderer.release_scene();
    if (!render_frames(windows, renderer, 8) ||
        !expect(gpu_counts() == idle && renderer.gpu_resource_count() == 0,
                "stopping textured scene retires every texture, mesh, program and uniform"))
    {
        return false;
    }
    renderer.shutdown();
    engine::RenderSystem limited(windows, &world, nullptr, nullptr, shared ? &tasks : nullptr,
                                 {.texture_batch_size = 4, .texture_staging_bytes = 1});
    return expect(limited.initialize(config) && create_cube_scene(world, false) &&
                      !limited.prepare_scene() && limited.gpu_resource_count() == 0,
                  "oversized texture fails before allocating staging pixels and cleans candidates");
}

bool test_render_settings_and_capture(const engine::EngineConfig& config)
{
    engine::ResourceSystem resources;
    engine::WorldSystem world;
    engine::WindowSystem windows;
    engine::RenderSystem renderer(windows, &world);
    test_support::ResourceFixture fixture;
    if (!resources.initialize(config) || !world.initialize(config) || !windows.initialize(config) ||
        !renderer.initialize(config) || !render_frames(windows, renderer, 8))
    {
        return false;
    }
    const auto idle = gpu_counts();
    if (!expect(
            !renderer.request_screenshot("relative.png") &&
                !renderer.request_screenshot(engine::PathUtils::to_utf8(fixture.root / "bad.jpg")),
            "capture requires absolute PNG destination") ||
        !create_cube_scene(world, false))
    {
        return false;
    }
    auto* scene = world.scene();
    auto environment_node = scene->create_node("Environment");
    if (!expect(
            environment_node.valid() &&
                environment_node.add_component<engine::EnvironmentComponent>().valid() &&
                scene->active_camera().node().add_component<engine::PostProcessComponent>().valid(),
            "scene render settings are owned by environment and camera components"))
    {
        return false;
    }
    auto environment = environment_node.get_component<engine::EnvironmentComponent>();
    auto post_process = scene->active_camera().node().get_component<engine::PostProcessComponent>();
    engine::Light light = {};
    for (const auto& node : test_support::scene_nodes(*scene))
    {
        if (auto candidate = node.get_component<engine::Light>())
        {
            light = candidate;
            break;
        }
    }
    if (!expect(light.valid(), "find the scene's directional light"))
    {
        return false;
    }
    auto invalid = post_process.settings();
    invalid.exposure = std::numeric_limits<float>::quiet_NaN();
    if (!expect(!post_process.set_settings(invalid) && renderer.settings().exposure == 1.0F,
                "invalid post-process settings preserve the previous configuration"))
    {
        return false;
    }
    auto floor = scene->create_node("Shadow receiver");
    auto floor_transform =
        floor.valid() ? floor.get_component<engine::Transform>() : engine::Transform{};
    auto floor_mesh =
        floor.valid() ? floor.add_component<engine::MeshRenderer>() : engine::MeshRenderer{};
    engine::TransformSettings floor_settings;
    floor_settings.position = {0.0, -1.0, 0.0};
    floor_settings.scale = {8.0, 0.1, 8.0};
    engine::TransformSettings camera_settings;
    camera_settings.position = {5.0, 6.0, 8.0};
    camera_settings.rotation = {-32.0, 32.0, 0.0};
    if (!expect(floor_transform.valid() && floor_mesh.valid() &&
                    floor_transform.set_settings(floor_settings) &&
                    floor_mesh.material().set_type("builtin://materials/default-pbr") &&
                    floor_mesh.material().set_v4("baseColor", {0.8, 0.8, 0.8, 1.0}) &&
                    scene->active_camera().node().get_component<engine::Transform>().set_settings(
                        camera_settings),
                "create receiver and oblique scene camera"))
    {
        return false;
    }
    auto environment_settings = environment.settings();
    auto post_settings = post_process.settings();
    auto light_settings = light.settings();
    const auto apply_settings = [&]()
    {
        return environment.set_settings(environment_settings) &&
               post_process.set_settings(post_settings) && light.set_settings(light_settings);
    };
    environment_settings.ambient_intensity = 0.12F;
    Screenshot unshadowed;
    Screenshot shadowed;
    if (!apply_settings() || !render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "without-shadow", unshadowed))
    {
        return false;
    }
    const auto unshadowed_resources = renderer.gpu_resource_count();
    environment_settings.ambient_intensity = 0.6F;
    for (const auto projection :
         {engine::schema::Projection::perspective, engine::schema::Projection::orthographic})
    {
        auto camera_projection = scene->active_camera().settings();
        camera_projection.projection = projection;
        camera_projection.orthographic_size = 6.0F;
        post_settings.ao_enabled = false;
        Screenshot without_ao;
        Screenshot with_ao;
        Screenshot restored_ao;
        if (!scene->active_camera().set_settings(camera_projection) || !apply_settings() ||
            !render_frames(windows, renderer, 8) ||
            !screenshot(windows, renderer, fixture.root, "without-ao", without_ao))
        {
            return false;
        }
        post_settings.ao_enabled = true;
        if (!apply_settings() || !render_frames(windows, renderer, 8) ||
            !screenshot(windows, renderer, fixture.root, "with-ao", with_ao) ||
            !expect(renderer.gpu_resource_count() > unshadowed_resources,
                    "AO reuses scene depth and owns compute programs and intermediate textures"))
        {
            return false;
        }
        std::size_t occluded = 0;
        for (std::size_t index = 0; index < without_ao.pixels.size(); ++index)
        {
            int difference = 0;
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                difference += without_ao.pixels[index][channel] - with_ao.pixels[index][channel];
            }
            occluded += difference > 12;
        }
        post_settings.ao_enabled = false;
        if (!expect(occluded > 20 && occluded < without_ao.pixels.size() / 2,
                    "AO darkens local contacts in both camera projections") ||
            !apply_settings() || !render_frames(windows, renderer, 8) ||
            !screenshot(windows, renderer, fixture.root, "restored-ao", restored_ao) ||
            !expect(restored_ao.pixels == without_ao.pixels &&
                        renderer.gpu_resource_count() == unshadowed_resources,
                    "disabling AO restores exact pixels and releases its GPU resources"))
        {
            return false;
        }
    }
    auto perspective = scene->active_camera().settings();
    perspective.projection = engine::schema::Projection::perspective;
    environment_settings.ambient_intensity = 0.0F;
    Screenshot direct_only;
    Screenshot direct_with_ao;
    if (!scene->active_camera().set_settings(perspective) || !apply_settings() ||
        !render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "direct-only", direct_only))
    {
        return false;
    }
    post_settings.ao_enabled = true;
    if (!apply_settings() || !render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "direct-with-ao", direct_with_ao) ||
        !expect(direct_only.pixels == direct_with_ao.pixels,
                "AO does not dim direct lighting when indirect light is absent"))
    {
        return false;
    }
    int original_width = 0;
    int original_height = 0;
    const auto ao_resources = renderer.gpu_resource_count();
    if (!SDL_GetWindowSize(windows.native_window(), &original_width, &original_height) ||
        !SDL_SetWindowSize(windows.native_window(), 641, 359) ||
        !render_frames(windows, renderer, 8) ||
        !expect(renderer.gpu_resource_count() == ao_resources,
                "AO resizes safely to non-aligned window dimensions without retaining textures") ||
        !SDL_SetWindowSize(windows.native_window(), original_width, original_height) ||
        !render_frames(windows, renderer, 8) ||
        !expect(renderer.gpu_resource_count() == ao_resources,
                "AO restores the original viewport without adding GPU resources"))
    {
        return false;
    }
    post_settings.ao_enabled = false;
    environment_settings.ambient_intensity = 0.12F;
    light_settings.cast_shadows = true;
    light_settings.shadow_resolution = 1024;
    light_settings.shadow_bias = 0.0005F;
    if (!expect(apply_settings(), "enable explicit directional shadow settings") ||
        !render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "with-shadow", shadowed) ||
        !expect(renderer.gpu_resource_count() >= unshadowed_resources,
                "enabling directional shadows retains the shared atlas and frame resources") ||
        !expect(shadowed.width == unshadowed.width && shadowed.height == unshadowed.height,
                "shadow comparison retains frame dimensions"))
    {
        return false;
    }
    std::size_t shaded_pixels = 0;
    for (std::size_t index = 0; index < shadowed.pixels.size(); ++index)
    {
        int before = 0;
        int after = 0;
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            before += unshadowed.pixels[index][channel];
            after += shadowed.pixels[index][channel];
        }
        shaded_pixels += before > after + 30;
    }
    if (!expect(shaded_pixels > 100, "directional shadow darkens receiver pixels"))
    {
        return false;
    }
    const auto shadow_resources = renderer.gpu_resource_count();
    for (const std::uint8_t cascades : {1, 2, 4})
    {
        light_settings.shadow_cascades = cascades;
        if (!expect(apply_settings() && render_frames(windows, renderer, 8) &&
                        renderer.gpu_resource_count() == shadow_resources,
                    "switching cascade counts reuses a single shadow atlas"))
        {
            return false;
        }
    }
    const auto nodes = test_support::scene_nodes(*scene);
    const auto cube_node = std::find_if(nodes.begin(), nodes.end(),
                                        [](const auto& node) { return node.name() == "Cube"; });
    auto cube_mesh = cube_node != nodes.end() ? cube_node->get_component<engine::MeshRenderer>()
                                              : engine::MeshRenderer{};
    if (!expect(cube_mesh.valid(), "find shadow caster for deformation"))
    {
        return false;
    }
    const std::array hidden_caster = {cube_mesh.instance_id()};
    Screenshot hidden_with_shadow;
    Screenshot hidden_without_shadow;
    if (!expect(scene->active_camera().set_hidden_meshes(hidden_caster),
                "hide the caster from the observing camera") ||
        !render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "hidden-caster-shadow", hidden_with_shadow))
    {
        return false;
    }
    cube_mesh.set_cast_shadows(false);
    if (!render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "hidden-caster-no-shadow",
                    hidden_without_shadow))
    {
        return false;
    }
    std::size_t hidden_shadow_pixels = 0;
    for (std::size_t index = 0; index < hidden_with_shadow.pixels.size(); ++index)
    {
        int difference = 0;
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            difference += hidden_without_shadow.pixels[index][channel] -
                          hidden_with_shadow.pixels[index][channel];
        }
        hidden_shadow_pixels += difference > 30;
    }
    if (!expect(hidden_shadow_pixels > 100 && cube_mesh.visible() && cube_mesh.enabled() &&
                    renderer.gpu_resource_count() == shadow_resources,
                "a mesh hidden from the camera still casts a visible shadow and retains resources"))
    {
        return false;
    }
    cube_mesh.set_cast_shadows(true);
    const auto shadow_environment = environment_settings;
    const auto shadow_post = post_settings;
    const auto shadow_light = light_settings;
    const auto restore_shadow_settings = [&]()
    {
        environment_settings = shadow_environment;
        post_settings = shadow_post;
        light_settings = shadow_light;
        return apply_settings();
    };
    light_settings.cast_shadows = false;
    post_settings.ao_enabled = true;
    environment_settings.ambient_intensity = 0.6F;
    Screenshot camera_hidden_ao;
    Screenshot globally_hidden_ao;
    if (!apply_settings() || !render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "camera-hidden-ao", camera_hidden_ao))
    {
        return false;
    }
    if (!cube_mesh.set_visible(false))
    {
        std::abort();
    }
    scene->active_camera().clear_hidden_meshes();
    if (!render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "globally-hidden-ao", globally_hidden_ao) ||
        !expect(camera_hidden_ao.pixels == globally_hidden_ao.pixels,
                "camera-hidden geometry does not contribute depth or occlusion to AO"))
    {
        return false;
    }
    if (!cube_mesh.set_visible(true))
    {
        std::abort();
    }
    if (!restore_shadow_settings() || !render_frames(windows, renderer, 8))
    {
        return false;
    }
    auto posed = *cube_mesh.mesh_data();
    if (!expect(engine::MeshUtils::build_tangents(posed, posed.tangents),
                "prepare shadow caster tangent frame"))
    {
        return false;
    }
    auto skin_source = posed;
    skin_source.skin.assign(skin_source.vertices.size(), {0, 0, 0, 0, 1, 0, 0, 0});
    const float cosine = std::cos(0.5F);
    const float sine = std::sin(0.5F);
    for (std::size_t index = 0; index < posed.vertices.size(); ++index)
    {
        const auto original = posed.vertices[index];
        posed.vertices[index] = {cosine * original.x() - sine * original.y() + 1.5F,
                                 sine * original.x() + cosine * original.y() + 2.0F,
                                 original.z(),
                                 cosine * original.nx() - sine * original.ny(),
                                 sine * original.nx() + cosine * original.ny(),
                                 original.nz()};
        const auto tangent = posed.tangents[index];
        posed.tangents[index] = {cosine * tangent.x() - sine * tangent.y(),
                                 sine * tangent.x() + cosine * tangent.y(), tangent.z(),
                                 tangent.w()};
    }
    Screenshot deformed_shadow;
    Screenshot gpu_shadow;
    Screenshot reference_shadow;
    const std::array<float, 16> skin_transform{cosine, sine, 0, 0, -sine, cosine, 0, 0,
                                               0,      0,    1, 0, 1.5F,  2.0F,   0, 1};
    auto skin_normal = skin_transform;
    skin_normal[12] = 0;
    skin_normal[13] = 0;
    post_settings.ao_enabled = true;
    if (!apply_settings() ||
        !expect(cube_mesh.set_deformation(posed.vertices, posed.tangents),
                "deform shadow caster without changing source mesh") ||
        !render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "deformed-shadow", deformed_shadow) ||
        !expect(cube_mesh.set_mesh_data(skin_source) &&
                    cube_mesh.set_skinning_palette({&skin_transform, 1}, {&skin_normal, 1}),
                "submit the same pose through compute skinning") ||
        !render_frames(windows, renderer, 4) ||
        !screenshot(windows, renderer, fixture.root, "gpu-skinned-shadow", gpu_shadow) ||
        !expect(cube_mesh.set_mesh_data(posed), "publish equivalent static reference geometry") ||
        !render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "reference-shadow", reference_shadow) ||
        !expect(deformed_shadow.width == reference_shadow.width &&
                    deformed_shadow.height == reference_shadow.height,
                "deformed and static reference screenshots have equal dimensions"))
    {
        return false;
    }
    std::size_t pose_changes = 0;
    std::size_t reference_mismatches = 0;
    std::size_t gpu_mismatches = 0;
    for (std::size_t index = 0; index < reference_shadow.pixels.size(); ++index)
    {
        int pose_difference = 0;
        int reference_difference = 0;
        int gpu_difference = 0;
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            pose_difference += std::abs(static_cast<int>(shadowed.pixels[index][channel]) -
                                        deformed_shadow.pixels[index][channel]);
            reference_difference +=
                std::abs(static_cast<int>(reference_shadow.pixels[index][channel]) -
                         deformed_shadow.pixels[index][channel]);
            gpu_difference += std::abs(static_cast<int>(reference_shadow.pixels[index][channel]) -
                                       gpu_shadow.pixels[index][channel]);
        }
        pose_changes += pose_difference > 30;
        reference_mismatches += reference_difference > 6;
        gpu_mismatches += gpu_difference > 6;
    }
    if (!expect(pose_changes > 100 &&
                    reference_mismatches < reference_shadow.pixels.size() / 1000 + 1 &&
                    gpu_mismatches < reference_shadow.pixels.size() / 1000 + 1,
                "deformed positions, normals, tangents, shadow bounds and shadow pass match "
                "equivalent static geometry") ||
        !expect(cube_mesh.set_mesh("builtin://meshes/cube"), "restore original shadow caster") ||
        !restore_shadow_settings() || !render_frames(windows, renderer, 8))
    {
        return false;
    }
    post_settings.filmic_tonemap = true;
    post_settings.exposure = 1.25F;
    const auto capture = fixture.root / "render-capture.png";
    const auto capture_path = engine::PathUtils::to_utf8(capture);
    if (!apply_settings() || !render_frames(windows, renderer, 8) ||
        !expect(renderer.request_screenshot(capture_path) &&
                    renderer.screenshot_status() == engine::ScreenshotStatus::pending &&
                    !renderer.request_screenshot(capture_path),
                "capture is queued and concurrent request is rejected") ||
        !render_frames(windows, renderer, 8) ||
        !expect(renderer.screenshot_status() == engine::ScreenshotStatus::complete,
                "capture reports completion after callback writes PNG"))
    {
        return false;
    }
    engine::ByteBuffer png;
    constexpr std::array<unsigned char, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (!expect(engine::ResourceSystem::read_binary(capture, png) && png.size() > 24 &&
                    std::equal(signature.begin(), signature.end(), png.begin(),
                               [](unsigned char expected, std::byte value)
                               { return expected == std::to_integer<unsigned char>(value); }),
                "screenshot file contains PNG bytes instead of a renamed TGA"))
    {
        return false;
    }
    const auto blocked = fixture.root / "directory.png";
    std::error_code error;
    if (!std::filesystem::create_directory(blocked, error) || error ||
        !renderer.request_screenshot(engine::PathUtils::to_utf8(blocked)) ||
        !render_frames(windows, renderer, 8) ||
        !expect(renderer.screenshot_status() == engine::ScreenshotStatus::failed,
                "PNG write error is returned through capture receipt"))
    {
        return false;
    }
    light_settings.cast_shadows = false;
    if (!apply_settings() || !render_frames(windows, renderer, 8) ||
        !expect(renderer.gpu_resource_count() == unshadowed_resources,
                "disabling shadows releases framebuffer, textures and shaders"))
    {
        return false;
    }
    world.destroy_scene();
    renderer.release_scene();
    return render_frames(windows, renderer, 8) &&
           expect(renderer.gpu_resource_count() == 0 && gpu_counts() == idle,
                  "capture and enhanced lighting retain no stopped-scene GPU resources");
}

bool test_reflections_water_and_diffuse_capture(const engine::EngineConfig& config)
{
    engine::ResourceSystem resources;
    engine::WorldSystem world;
    engine::WindowSystem windows;
    engine::RenderSystem renderer(windows, &world);
    test_support::ResourceFixture fixture;
    if (!expect(resources.initialize(config) && world.initialize(config) &&
                    windows.initialize(config) && renderer.initialize(config),
                "initialize reflection and water GPU fixture") ||
        !render_frames(windows, renderer, 4))
    {
        return false;
    }
    if (renderer.supports_msaa_samples(4) && !renderer.set_msaa_samples(4))
    {
        return false;
    }
    const auto idle = gpu_counts();
    if (!create_cube_scene(world, false))
    {
        return false;
    }
    auto* scene = world.scene();
    auto camera = scene->active_camera();
    auto cube_mesh =
        test_support::scene_nodes(*scene).front().get_component<engine::MeshRenderer>();
    auto environment_node = scene->create_node("Environment");
    auto environment = environment_node.add_component<engine::EnvironmentComponent>();
    auto post = camera.node().add_component<engine::PostProcessComponent>();
    auto ground = scene->create_node("Opaque ground");
    auto ground_mesh = ground.add_component<engine::MeshRenderer>();
    auto water = scene->create_node("Transparent water");
    auto water_mesh = water.add_component<engine::MeshRenderer>();
    auto reflection = water.add_component<engine::PlanarReflectionComponent>();
    engine::TransformSettings camera_transform;
    camera_transform.position = {5, 5, 8};
    camera_transform.rotation = {-28, 32, 0};
    engine::TransformSettings ground_transform;
    ground_transform.position = {0, -1.2, 0};
    ground_transform.scale = {8, 0.1, 8};
    engine::TransformSettings water_transform;
    water_transform.position = {0, -1.02, 0};
    water_transform.scale = {5, 1, 5};
    engine::schema::MeshDataT plane;
    plane.path = "meshes/test-water";
    plane.vertices = {
        {-1, 0, -1, 0, 1, 0}, {-1, 0, 1, 0, 1, 0}, {1, 0, 1, 0, 1, 0}, {1, 0, -1, 0, 1, 0}};
    plane.indices = {0, 1, 2, 0, 2, 3};
    plane.texcoords = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
    if (!expect(
            environment && post && ground_mesh && water_mesh && reflection &&
                camera.node().get_component<engine::Transform>().set_settings(camera_transform) &&
                ground.get_component<engine::Transform>().set_settings(ground_transform) &&
                water.get_component<engine::Transform>().set_settings(water_transform) &&
                water_mesh.set_mesh_data(plane) &&
                water_mesh.material().set_type("builtin://materials/default-water") &&
                water_mesh.material().blend_mode() == engine::schema::BlendMode::alpha_blend &&
                water_mesh.material().set_cull_mode(engine::schema::CullMode::none) &&
                water_mesh.material().set_v2("waterFlow", {0, 0}) &&
                water_mesh.material().set_float("normalScale", 0) &&
                cube_mesh.material().set_v3("emissionColor", {3.0, 0.1, 0.03}) &&
                ground_mesh.material().set_v4("baseColor", {0.18, 0.18, 0.18, 1}) &&
                ground_mesh.material().set_float("roughness", 0.08F),
            "ordinary nodes compose reflective water, opaque receiver and existing camera"))
    {
        return false;
    }
    auto environment_settings = environment.settings();
    environment_settings.ambient_intensity = 0.2F;
    auto post_settings = post.settings();
    post_settings.ssr_enabled = false;
    post_settings.ao_enabled = true;
    post_settings.hdr_enabled = true;
    if (!expect(environment.set_settings(environment_settings) && post.set_settings(post_settings),
                "isolate planar reflection from SSR while preserving AO and HDR"))
    {
        return false;
    }
    std::vector<engine::Light> local_lights;
    for (const auto kind : {engine::schema::LightKind::point, engine::schema::LightKind::spot})
    {
        auto node = scene->create_node("Local shadow light");
        auto light = node.add_component<engine::Light>();
        engine::LightSettings settings;
        settings.kind = kind;
        settings.range = 25;
        settings.intensity = 25;
        settings.cast_shadows = true;
        settings.shadow_resolution = 512;
        engine::TransformSettings transform;
        transform.position = kind == engine::schema::LightKind::point ? engine::Vec3(3, 4, 3)
                                                                      : engine::Vec3(-3, 4, 3);
        transform.rotation = {-45, -35, 0};
        if (!expect(light && light.set_settings(settings) &&
                        node.get_component<engine::Transform>().set_settings(transform),
                    "point cube faces and projected spot shadows use the shared atlas"))
        {
            return false;
        }
        local_lights.push_back(light);
    }
    Screenshot reflected;
    Screenshot fallback;
    if (!render_frames(windows, renderer, 4) ||
        !screenshot(windows, renderer, fixture.root, "planar-water", reflected))
    {
        return false;
    }
    if (!reflection.set_enabled(false))
    {
        std::abort();
    }
    if (!render_frames(windows, renderer, 4) ||
        !screenshot(windows, renderer, fixture.root, "water-without-planar", fallback))
    {
        return false;
    }
    std::size_t planar_pixels = 0;
    // The reflected cube lies below the real cube, inside the middle third of the water.
    // Flow and normal perturbation are frozen; SSR is off in both screenshots.
    for (std::uint32_t y = reflected.height / 2; y < reflected.height * 5 / 6; ++y)
    {
        for (std::uint32_t x = reflected.width / 3; x < reflected.width * 2 / 3; ++x)
        {
            const auto first = reflected.pixel(x, y);
            const auto second = fallback.pixel(x, y);
            int difference = 0;
            for (std::size_t channel = 0; channel < first.size(); ++channel)
            {
                difference +=
                    std::abs(static_cast<int>(first[channel]) - static_cast<int>(second[channel]));
            }
            planar_pixels += difference > 6;
        }
    }
    std::fprintf(stderr, "window/render planar reflected pixels: %zu\n", planar_pixels);
    if (!expect(planar_pixels > 16,
                "static water reflects real mirrored geometry with SSR disabled"))
    {
        return false;
    }
    Screenshot straight_alpha;
    Screenshot premultiplied;
    if (!expect(water_mesh.material().set_v4("baseColor", {0.2F, 0.45F, 0.5F, 0.55F}),
                "set partial water coverage") ||
        !screenshot(windows, renderer, fixture.root, "water-straight-alpha", straight_alpha) ||
        !expect(water_mesh.material().set_blend_mode(engine::schema::BlendMode::premultiplied),
                "select premultiplied water blending") ||
        !screenshot(windows, renderer, fixture.root, "water-premultiplied", premultiplied))
    {
        return false;
    }
    int largest_alpha_difference = 0;
    for (std::size_t pixel = 0; pixel < straight_alpha.pixels.size(); ++pixel)
    {
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            largest_alpha_difference =
                std::max(largest_alpha_difference,
                         std::abs(static_cast<int>(straight_alpha.pixels[pixel][channel]) -
                                  static_cast<int>(premultiplied.pixels[pixel][channel])));
        }
    }
    if (!expect(largest_alpha_difference <= 2,
                "straight and premultiplied blending preserve the same water coverage") ||
        !water_mesh.material().set_blend_mode(engine::schema::BlendMode::alpha_blend))
    {
        return false;
    }
    Screenshot refracted;
    Screenshot straight_transmission;
    auto deep_water = water_transform;
    deep_water.position = {0, 0.3, 0};
    auto water_material = water_mesh.material();
    if (!expect(water.get_component<engine::Transform>().set_settings(deep_water) &&
                    water_material.set_float("reflectionStrength", 0) &&
                    water_material.set_float("specularHighlights", 0) &&
                    water_material.set_v3("absorptionColor", {1, 1, 1}) &&
                    water_material.set_float("waterIor", 1),
                "isolate water transmission from Fresnel, highlights and absorption") ||
        !screenshot(windows, renderer, fixture.root, "water-ior-one", straight_transmission) ||
        !water_material.set_float("waterIor", 1.5F) ||
        !screenshot(windows, renderer, fixture.root, "water-refracted", refracted))
    {
        return false;
    }
    std::size_t refracted_pixels = 0;
    for (std::size_t pixel = 0; pixel < refracted.pixels.size(); ++pixel)
    {
        int difference = 0;
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            difference += std::abs(static_cast<int>(refracted.pixels[pixel][channel]) -
                                   static_cast<int>(straight_transmission.pixels[pixel][channel]));
        }
        refracted_pixels += difference > 6;
    }
    std::fprintf(stderr, "window/render refracted pixels: %zu\n", refracted_pixels);
    if (!expect(refracted_pixels > 16, "IOR bends visible background geometry through water") ||
        !water.get_component<engine::Transform>().set_settings(water_transform) ||
        !water_material.set_float("waterIor", 1.333F) ||
        !water_material.set_float("reflectionStrength", 1) ||
        !water_material.set_float("specularHighlights", 1) ||
        !water_material.set_v3("absorptionColor", {0.45, 0.78, 0.85}))
    {
        return false;
    }
    if (!water_mesh.set_enabled(false))
    {
        std::abort();
    }
    Screenshot with_ssr;
    Screenshot without_ssr;
    post_settings.ssr_enabled = true;
    if (!post.set_settings(post_settings) ||
        !screenshot(windows, renderer, fixture.root, "opaque-with-ssr", with_ssr))
    {
        return false;
    }
    post_settings.ssr_enabled = false;
    if (!post.set_settings(post_settings) ||
        !screenshot(windows, renderer, fixture.root, "opaque-without-ssr", without_ssr))
    {
        return false;
    }
    std::size_t reflection_pixels = 0;
    for (std::size_t pixel = 0; pixel < with_ssr.pixels.size(); ++pixel)
    {
        int difference = 0;
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            difference += std::abs(static_cast<int>(with_ssr.pixels[pixel][channel]) -
                                   static_cast<int>(without_ssr.pixels[pixel][channel]));
        }
        reflection_pixels += difference > 6;
    }
    std::fprintf(stderr, "window/render SSR changed pixels: %zu\n", reflection_pixels);
    if (!expect(reflection_pixels > 16,
                "SSR adds visible on-screen geometry reflections without an environment map"))
    {
        return false;
    }
    for (auto light : local_lights)
    {
        auto settings = light.settings();
        const auto name = settings.kind == engine::schema::LightKind::point ? "point" : "spot";
        Screenshot unshadowed;
        settings.cast_shadows = false;
        if (!light.set_settings(settings) ||
            !screenshot(windows, renderer, fixture.root,
                        (std::string(name) + "-without-shadow").c_str(), unshadowed))
        {
            return false;
        }
        std::size_t shaded_pixels = 0;
        for (std::size_t pixel = 0; pixel < unshadowed.pixels.size(); ++pixel)
        {
            int difference = 0;
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                difference += static_cast<int>(unshadowed.pixels[pixel][channel]) -
                              static_cast<int>(without_ssr.pixels[pixel][channel]);
            }
            shaded_pixels += difference > 6;
        }
        std::fprintf(stderr, "window/render %s shadow pixels: %zu\n", name, shaded_pixels);
        settings.cast_shadows = true;
        if (!expect(shaded_pixels > 16, "local light shadow darkens genuinely occluded pixels") ||
            !light.set_settings(settings))
        {
            return false;
        }
    }
    post_settings.ssr_enabled = true;
    if (!post.set_settings(post_settings))
    {
        return false;
    }
    if (!water_mesh.set_enabled(true))
    {
        std::abort();
    }
    if (!reflection.set_enabled(true))
    {
        std::abort();
    }
    if (!expect(windows.set_size(643, 357), "resize combined effects to non-aligned dimensions") ||
        !render_frames(windows, renderer, 8))
    {
        return false;
    }
    engine::Vec2 viewport;
    int pixel_width = 0;
    int pixel_height = 0;
    if (!expect(
            renderer.viewport_size(viewport) &&
                SDL_GetWindowSizeInPixels(windows.native_window(), &pixel_width, &pixel_height) &&
                viewport.x() == pixel_width && viewport.y() == pixel_height &&
                (pixel_width % 16 != 0 || pixel_height % 16 != 0),
            "combined depth/AO/SSR/refraction follow actual non-aligned drawable dimensions"))
    {
        return false;
    }

    engine::SceneCaptureRequest request;
    request.width = 24;
    request.height = 24;
    request.view = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -6, 1};
    const bool homogeneous = bgfx::getCaps()->homogeneousDepth;
    constexpr float near_clip = 0.1F;
    constexpr float far_clip = 100.0F;
    request.projection[0] = request.projection[5] = 1;
    request.projection[10] = -(far_clip + (homogeneous ? near_clip : 0)) / (far_clip - near_clip);
    request.projection[11] = -1;
    request.projection[14] =
        -(homogeneous ? 2.0F : 1.0F) * near_clip * far_clip / (far_clip - near_clip);
    engine::SceneCaptureImage image;
    if (!expect(renderer.request_diffuse_capture(request) &&
                    !renderer.request_diffuse_capture(request) &&
                    !renderer.take_diffuse_capture(image),
                "diffuse capture accepts one copied request and does not expose pending pixels"))
    {
        return false;
    }
    for (int frame = 0;
         frame < 32 && renderer.diffuse_capture_status() == engine::SceneCaptureStatus::pending;
         ++frame)
    {
        if (!render_frames(windows, renderer, 1))
        {
            return false;
        }
    }
    if (!expect(renderer.diffuse_capture_status() == engine::SceneCaptureStatus::complete &&
                    renderer.take_diffuse_capture(image) && image.width == 24 &&
                    image.height == 24 && image.rgba.size() == 24 * 24 * 4 &&
                    std::all_of(image.rgba.begin(), image.rgba.end(),
                                [](float value) { return std::isfinite(value) && value >= 0; }) &&
                    image.rgba[0] > 0 &&
                    renderer.diffuse_capture_status() == engine::SceneCaptureStatus::idle,
                "GPU diffuse capture returns finite nonzero linear radiance and retires receipt"))
    {
        return false;
    }
    if (!expect(scene->set_active_camera({}) && renderer.request_diffuse_capture(request),
                "queue diffuse capture without a gameplay camera"))
    {
        return false;
    }
    for (int frame = 0;
         frame < 32 && renderer.diffuse_capture_status() == engine::SceneCaptureStatus::pending;
         ++frame)
    {
        if (!render_frames(windows, renderer, 1))
        {
            return false;
        }
    }
    if (!expect(renderer.diffuse_capture_status() == engine::SceneCaptureStatus::complete &&
                    renderer.take_diffuse_capture(image) && image.width == 24 &&
                    image.height == 24 && scene->set_active_camera(camera),
                "bake capture completes from its own matrices with no active camera") ||
        !expect(renderer.request_diffuse_capture(request), "queue capture for cancellation") ||
        !render_frames(windows, renderer, 1))
    {
        return false;
    }
    renderer.cancel_diffuse_capture();
    for (int frame = 0;
         frame < 32 && renderer.diffuse_capture_status() == engine::SceneCaptureStatus::pending;
         ++frame)
    {
        if (!render_frames(windows, renderer, 1))
        {
            return false;
        }
    }
    if (!expect(renderer.diffuse_capture_status() == engine::SceneCaptureStatus::cancelled &&
                    !renderer.take_diffuse_capture(image) && image.rgba.empty(),
                "cancelled in-flight readback never publishes pixels") ||
        !expect(renderer.request_diffuse_capture(request), "queue capture before scene retirement"))
    {
        return false;
    }
    world.destroy_scene();
    renderer.release_scene();
    return render_frames(windows, renderer, 4) &&
           expect(renderer.diffuse_capture_status() != engine::SceneCaptureStatus::pending &&
                      !renderer.take_diffuse_capture(image) && renderer.gpu_resource_count() == 0 &&
                      gpu_counts() == idle,
                  "scene retirement cancels capture and releases reflection/water/light resources");
}

bool test_async_capture(const engine::EngineConfig& config)
{
    engine::ThreadSystem threads({.worker_limit = 1});
    engine::TaskSystem tasks(threads,
                             {.name = "Capture test", .max_concurrency = 1, .max_pending = 1});
    engine::WindowSystem windows;
    engine::RenderSystem renderer(windows, nullptr, nullptr, nullptr, &tasks);
    test_support::ResourceFixture fixture;
    if (!threads.initialize(config) || !tasks.initialize(config) || !windows.initialize(config) ||
        !renderer.initialize(config) || !render_frames(windows, renderer, 8))
    {
        return false;
    }
    std::promise<void> release;
    const auto gate = release.get_future().share();
    auto busy = tasks.submit("Occupy capture admission",
                             [gate]
                             {
                                 gate.wait();
                                 return true;
                             });
    if (!busy.valid())
    {
        return false;
    }
    const auto capture = fixture.root / "async-capture.png";
    std::error_code error;
    const bool pending = renderer.request_screenshot(engine::PathUtils::to_utf8(capture)) &&
                         render_frames(windows, renderer, 8) &&
                         renderer.screenshot_status() == engine::ScreenshotStatus::pending &&
                         !std::filesystem::exists(capture, error) && !error;
    release.set_value();
    if (!expect(busy.get() && pending,
                "full task domain leaves one pending readback without blocking GPU frames"))
    {
        return false;
    }
    // 至少一次主线程提交；关闭必须等待已经接收的独立截图数据写入。
    if (!render_frames(windows, renderer, 1))
    {
        return false;
    }
    renderer.shutdown();
    engine::ByteBuffer png;
    constexpr std::array<unsigned char, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (!expect(renderer.screenshot_status() == engine::ScreenshotStatus::complete &&
                    engine::ResourceSystem::read_binary(capture, png) && png.size() > 24 &&
                    std::equal(signature.begin(), signature.end(), png.begin(),
                               [](unsigned char expected, std::byte value)
                               { return expected == std::to_integer<unsigned char>(value); }),
                "renderer shutdown waits for accepted capture and reports actual PNG output"))
    {
        return false;
    }
    renderer.shutdown();
    return true;
}

bool test_hdr_and_bloom(const engine::EngineConfig& config)
{
    engine::ResourceSystem resources;
    engine::WorldSystem world;
    engine::WindowSystem windows;
    engine::RenderSystem renderer(windows, &world);
    test_support::ResourceFixture fixture;
    if (!resources.initialize(config) || !world.initialize(config) || !windows.initialize(config) ||
        !renderer.initialize(config) || !render_frames(windows, renderer, 8))
    {
        return false;
    }
    if (renderer.supports_msaa_samples(4) && !renderer.set_msaa_samples(4))
    {
        return false;
    }
    const auto idle = gpu_counts();
    if (!create_cube_scene(world, false))
    {
        return false;
    }
    auto* scene = world.scene();
    auto environment = scene->create_node("Environment");
    if (!environment.valid() ||
        environment.add_component<engine::EnvironmentComponent>().valid() == false ||
        scene->active_camera().node().add_component<engine::PostProcessComponent>().valid() ==
            false)
    {
        return false;
    }
    for (const auto& node : test_support::scene_nodes(*scene))
    {
        if (auto mesh = node.get_component<engine::MeshRenderer>())
        {
            if (!mesh.material().set_v4("baseColor", {0, 0, 0, 1}) ||
                !mesh.material().set_v3("emissionColor", {12, 4, 1}))
            {
                return false;
            }
        }
        if (auto light = node.get_component<engine::Light>())
        {
            if (!light.set_enabled(false))
            {
                std::abort();
            }
        }
    }
    auto environment_component = environment.get_component<engine::EnvironmentComponent>();
    auto post_process = scene->active_camera().node().get_component<engine::PostProcessComponent>();
    auto environment_settings = environment_component.settings();
    environment_settings.clear_color = 0x000000ff;
    if (!environment_component.set_settings(environment_settings))
    {
        return false;
    }
    auto settings = post_process.settings();
    settings.filmic_tonemap = true;
    Screenshot sdr;
    if (!post_process.set_settings(settings) || !render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "hdr-sdr-baseline", sdr))
    {
        return false;
    }
    const auto sdr_resources = renderer.gpu_resource_count();
    settings.hdr_enabled = true;
    Screenshot hdr;
    if (!post_process.set_settings(settings) || !render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "hdr-no-bloom", hdr) ||
        !expect(renderer.gpu_resource_count() == sdr_resources,
                "HDR display settings reuse the always-linear scene target and resolve programs"))
    {
        return false;
    }
    const auto hdr_resources = renderer.gpu_resource_count();
    settings.bloom_enabled = true;
    settings.bloom_intensity = 0.7F;
    Screenshot bloom;
    if (!post_process.set_settings(settings) || !render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "hdr-bloom", bloom) ||
        !expect(renderer.gpu_resource_count() > hdr_resources,
                "Bloom owns separate bright and blur targets"))
    {
        return false;
    }
    std::size_t halo_pixels = 0;
    for (std::size_t index = 0; index < hdr.pixels.size(); ++index)
    {
        halo_pixels += hdr.pixels[index][0] < 3 && bloom.pixels[index][0] > 8;
    }
    if (!expect(halo_pixels > 100 && halo_pixels < hdr.pixels.size() / 2,
                "HDR emission produces a local Bloom halo outside its geometry"))
    {
        return false;
    }
    Screenshot zero_bloom;
    settings.bloom_intensity = 0.0F;
    if (!post_process.set_settings(settings) || !render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "hdr-bloom-zero", zero_bloom) ||
        !expect(zero_bloom.pixels == hdr.pixels,
                "zero Bloom strength preserves HDR output without applying tone mapping twice"))
    {
        return false;
    }
    settings.bloom_intensity = 0.7F;
    settings.bloom_threshold = 100.0F;
    Screenshot threshold;
    if (!post_process.set_settings(settings) || !render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "hdr-bloom-threshold", threshold) ||
        !expect(threshold.pixels == hdr.pixels,
                "linear threshold above scene radiance excludes the complete Bloom contribution") ||
        !SDL_SetWindowSize(windows.native_window(), 641, 359) ||
        !render_frames(windows, renderer, 8))
    {
        return false;
    }
    settings.bloom_enabled = false;
    if (!post_process.set_settings(settings) || !render_frames(windows, renderer, 8) ||
        !expect(renderer.gpu_resource_count() == hdr_resources,
                "disabling Bloom after resize releases its intermediate targets"))
    {
        return false;
    }
    settings.hdr_enabled = false;
    Screenshot restored;
    if (!post_process.set_settings(settings) ||
        !SDL_SetWindowSize(windows.native_window(), config.width, config.height) ||
        !render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "hdr-sdr-restored", restored) ||
        !expect(
            renderer.gpu_resource_count() == sdr_resources && restored.pixels == sdr.pixels,
            "disabling HDR restores exact SDR pixels while retaining the shared linear targets"))
    {
        return false;
    }
    world.destroy_scene();
    renderer.release_scene();
    return render_frames(windows, renderer, 8) &&
           expect(renderer.gpu_resource_count() == 0 && gpu_counts() == idle,
                  "HDR and Bloom retain no GPU resources after scene stop");
}

bool tick_engine_frames(engine::RuntimeHost& engine, int count)
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

class ObservedRenderExtension final : public engine::RenderExtension
{
public:
    ~ObservedRenderExtension() noexcept override
    {
        release_gpu();
    }

    [[nodiscard]] bool draw(const engine::RenderContext& context) override
    {
        ++draws;
        scene_id = context.scene.id();
        view = context.view;
        projection = context.projection;
        hdr = context.hdr;
        valid_context = context.view_id == 134 && context.width != 0 && context.height != 0;
        if (!bgfx::isValid(texture_))
        {
            texture_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8);
        }
        return accept_draw && valid_context && bgfx::isValid(texture_);
    }

    void release_scene() noexcept override
    {
        ++scene_releases;
    }

    void release_gpu() noexcept override
    {
        if (bgfx::isValid(texture_))
        {
            bgfx::destroy(texture_);
            texture_ = BGFX_INVALID_HANDLE;
            ++gpu_releases;
        }
    }

    std::array<float, 16> view{};
    std::array<float, 16> projection{};
    std::uint64_t scene_id = 0;
    unsigned int draws = 0;
    unsigned int scene_releases = 0;
    unsigned int gpu_releases = 0;
    bool hdr = false;
    bool valid_context = false;
    bool accept_draw = true;

private:
    bgfx::TextureHandle texture_ = BGFX_INVALID_HANDLE;
};

bool test_render_extensions(const engine::EngineConfig& config)
{
    // 扩展比渲染器活得更久：渲染器析构必须解除反向登记。
    ObservedRenderExtension extension;
    engine::ResourceSystem resources;
    engine::WorldSystem world;
    engine::WindowSystem windows;
    if (!expect(resources.initialize(config) && world.initialize(config) &&
                    windows.initialize(config),
                "initialize extension test owners"))
    {
        return false;
    }
    {
        engine::RenderSystem renderer(windows, &world);
        engine::RenderSystem other(windows);
        if (!expect(renderer.add_extension(extension) && !renderer.add_extension(extension) &&
                        !other.add_extension(extension),
                    "extension belongs to exactly one renderer") ||
            !expect(renderer.initialize(config), "initialize renderer with a borrowed extension"))
        {
            return false;
        }
        auto* scene = world.create_scene("Extension-only scene");
        auto camera_node = scene != nullptr ? scene->create_node("Camera") : engine::Node{};
        auto camera =
            camera_node.valid() ? camera_node.add_component<engine::Camera>() : engine::Camera{};
        auto post = camera_node.valid() ? camera_node.add_component<engine::PostProcessComponent>()
                                        : engine::PostProcessComponent{};
        if (!expect(scene && camera && post && scene->set_active_camera(camera),
                    "create camera-only scene without a core mesh or text component") ||
            !render_frames(windows, renderer, 1) ||
            !expect(extension.draws == 1 && extension.scene_id == scene->id() &&
                        extension.valid_context && extension.hdr,
                    "registered extension draws in a camera-only scene"))
        {
            return false;
        }
        const auto game_view = extension.view;
        auto preview_node = scene->create_node("Preview camera");
        auto preview =
            preview_node.valid() ? preview_node.add_component<engine::Camera>() : engine::Camera{};
        engine::TransformSettings preview_transform;
        preview_transform.position = {3, 2, 5};
        auto post_settings = post.settings();
        post_settings.hdr_enabled = true;
        post_settings.bloom_enabled = true;
        if (!expect(preview &&
                        preview_node.get_component<engine::Transform>().set_settings(
                            preview_transform) &&
                        post.set_settings(post_settings),
                    "configure preview camera and scene HDR") ||
            !render_frames(windows, renderer, 1, preview) ||
            !expect(extension.draws == 2 && extension.view != game_view && extension.hdr,
                    "extension receives actual preview matrices and scene HDR state"))
        {
            return false;
        }
        extension.accept_draw = false;
        if (!expect(!renderer.render(preview), "extension draw failure reaches caller"))
        {
            return false;
        }
        extension.accept_draw = true;
        const auto retained_releases = extension.scene_releases;
        if (!expect(camera_node.set_persistent(true), "retain a camera and its HDR settings"))
        {
            return false;
        }
        world.unload_scene();
        if (!render_frames(windows, renderer, 1) ||
            !expect(extension.scene_releases == retained_releases && extension.draws == 3,
                    "ordinary unload neither releases extensions nor uses the retained camera"))
        {
            return false;
        }
        auto* replacement = world.create_scene("New active camera");
        auto next_node = replacement ? replacement->create_node("Camera") : engine::Node{};
        auto next_camera = next_node ? next_node.add_component<engine::Camera>() : engine::Camera{};
        if (!expect(next_camera && replacement->set_active_camera(next_camera),
                    "select new scene camera explicitly") ||
            !render_frames(windows, renderer, 1) ||
            !expect(extension.scene_releases == retained_releases && extension.hdr &&
                        extension.scene_id == replacement->id(),
                    "new scene keeps the linear extension target and retains extension resources"))
        {
            return false;
        }
        // 先销毁节点再释放扩展，验证清理只使用扩展自身状态。
        world.destroy_scene();
        const auto releases = extension.scene_releases;
        renderer.release_scene();
        if (!expect(extension.scene_releases == releases + 1,
                    "scene retirement notifies extension after nodes are gone") ||
            !expect(renderer.remove_extension(extension) && !renderer.remove_extension(extension) &&
                        extension.gpu_releases == 1 && renderer.add_extension(extension),
                    "explicit detach releases GPU state and permits another registration"))
        {
            return false;
        }
        // 扩展先析构：其基类只撤销登记，渲染器不再调用已经销毁的对象。
        {
            ObservedRenderExtension temporary;
            if (!expect(renderer.add_extension(temporary), "register a shorter-lived extension"))
            {
                return false;
            }
        }
        if (!expect(create_cube_scene(world, true), "create replacement extension scene") ||
            !render_frames(windows, renderer, 1))
        {
            return false;
        }
        renderer.shutdown();
        if (!expect(extension.gpu_releases == 2,
                    "renderer releases extension GPU objects before shutting down bgfx"))
        {
            return false;
        }
    }
    engine::RenderSystem replacement(windows);
    return expect(replacement.add_extension(extension) && replacement.remove_extension(extension),
                  "renderer destruction clears extension reverse registration");
}

bool test_engine(const engine::EngineConfig& config)
{
    engine::RuntimeHost engine;
    engine::EngineConfig invalid_config = config;
    invalid_config.width = 0;
    if (!expect(&engine::Engine::instance() == &engine::Engine::instance(),
                "Engine must be a singleton") ||
        !expect(!engine.initialize(invalid_config), "reject invalid window configuration") ||
        !expect(engine.tick(delta_time) == engine::FrameStatus::failed,
                "tick must fail before initialization") ||
        !expect(engine.initialize(config), "initialize engine with all runtime systems") ||
        !expect(!engine.initialize(config), "reject repeated initialization while running") ||
        !expect(engine.gameplay()->script_status() == nullptr && !engine.gameplay()->start() &&
                    !engine.gameplay()->last_error().empty(),
                "default runtime has no implicit script plugin and reports unavailable gameplay") ||
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

    const auto* builtin = engine::ResourceSystem::find_shader("builtin://shaders/pbr");
    if (!expect(builtin != nullptr, "engine owns builtin definitions"))
    {
        return false;
    }
    engine::schema::ShaderDataT project_shader(*builtin);
    project_shader.path = "shaders/lifecycle";
    engine::schema::MaterialDataT project_material;
    project_material.type_path = "materials/lifecycle";
    project_material.shader_path = project_shader.path;
    test_support::ResourceFixture fixture;
    auto* resources = engine::ResourceSystem::instance();
    engine::ResourcePtr held;
    engine::ResourcePtr separate;
    if (!expect(
            resources && &resources->loader() == resources && fixture.add(project_shader) &&
                fixture.add(project_material) &&
                engine.gameplay()->set_resources(fixture.catalog, {{"content", fixture.root}}) &&
                fixture.load(project_material.type_path, held),
            "host without script plugin can submit a resource directory to the core") ||
        !expect(fixture.load(project_material.type_path, separate) && separate != held &&
                    held->material()->shader_path == project_shader.path &&
                    engine::ResourceSystem::find_shader(project_shader.path) == nullptr,
                "default host does not silently install recursive loading or shared caches"))
    {
        return false;
    }
    held.reset();
    separate.reset();
    engine.gameplay()->stop();
    engine.gameplay()->stop();
    if (!expect(engine::ResourceSystem::find_shader(project_shader.path) == nullptr &&
                    engine::ResourceSystem::find_material(project_material.type_path) == nullptr &&
                    engine::ResourceSystem::find_shader("builtin://shaders/pbr") == builtin,
                "stopped project retains no unowned project definitions and preserves builtin "
                "identity"))
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

class ObservedResourceLoader final : public engine::ResourceLoader
{
public:
    explicit ObservedResourceLoader(engine::ResourceSystem& resources) noexcept
        : resources_(resources)
    {
    }

    [[nodiscard]] bool load(std::string_view guid, engine::ResourcePtr& output) override
    {
        ++loads_;
        return resources_.load(guid, output);
    }

    [[nodiscard]] engine::ResourcePtr find(std::string_view guid) const noexcept override
    {
        return resources_.find(guid);
    }

    [[nodiscard]] const std::string& last_error() const noexcept override
    {
        return resources_.last_error();
    }

    [[nodiscard]] std::size_t loads() const noexcept
    {
        return loads_;
    }

private:
    engine::ResourceSystem& resources_;
    std::size_t loads_ = 0;
};

bool test_borrowed_resource_host(const engine::EngineConfig& config)
{
    engine::ResourceSystem resources;
    ObservedResourceLoader loader(resources);
    engine::RuntimeHost host;
    if (!expect(!host.initialize(config, &resources) &&
                    engine::Engine::instance().system_count() == 0 &&
                    engine::ResourceSystem::instance() == nullptr,
                "uninitialized borrowed resources fail before claiming the engine") ||
        !expect(resources.initialize(config), "initialize caller-owned resource core"))
    {
        return false;
    }
    resources.set_loader(&loader);
    auto invalid_config = config;
    invalid_config.width = 0;
    if (!expect(!host.initialize(invalid_config, &resources) && resources.is_initialized() &&
                    engine::ResourceSystem::instance() == &resources &&
                    &resources.loader() == &loader,
                "partial host initialization never closes borrowed resource services") ||
        !expect(host.initialize(config, &resources) && &resources.loader() == &loader,
                "runtime host preserves the caller's selected loader") ||
        !expect(host.world() && create_cube_scene(*host.world(), false),
                "scene components use the injected loader") ||
        !tick_engine_frames(host, 8) ||
        !expect(loader.loads() > 0, "material and mesh reads reached the selected loader"))
    {
        host.shutdown();
        resources.set_loader(nullptr);
        return false;
    }
    host.shutdown();
    host.shutdown();
    const bool preserved =
        expect(resources.is_initialized() && engine::ResourceSystem::instance() == &resources &&
                   &resources.loader() == &loader && engine::Engine::instance().system_count() == 0,
               "host shutdown releases consumers but never closes caller-owned resources");
    resources.set_loader(nullptr);
    resources.shutdown();
    return preserved && expect(engine::ResourceSystem::instance() == nullptr,
                               "caller closes resources after the runtime host");
}

struct ScriptLifecycle
{
    engine::WorldSystem* world = nullptr;
    engine::RenderSystem* renderer = nullptr;
    std::size_t initialized = 0;
    std::size_t ticks = 0;
    std::size_t shutdowns = 0;
    bool scene_present_when_stopped = false;
    bool shutdown_before_world = false;
    bool destroyed = false;
};

ScriptLifecycle script_lifecycle;

class ObservedScriptRuntime final : public engine::ScriptRuntime
{
public:
    explicit ObservedScriptRuntime(engine::WorldSystem& world) noexcept : world_(world)
    {
        script_lifecycle.world = &world;
    }

    ~ObservedScriptRuntime() noexcept override
    {
        shutdown();
        script_lifecycle.destroyed = true;
    }

    [[nodiscard]] bool prepare_gameplay(const engine::ResourceCatalog&, const engine::MountList&,
                                        std::string_view, std::string_view) override
    {
        return false;
    }

    [[nodiscard]] bool start_prepared_gameplay() override
    {
        return false;
    }

    void stop_gameplay() noexcept override
    {
        script_lifecycle.scene_present_when_stopped =
            script_lifecycle.scene_present_when_stopped || world_.scene() != nullptr;
    }

    [[nodiscard]] bool execute_project_script(const engine::ProjectScriptRequest&,
                                              std::string& result, std::string& error) override
    {
        result.clear();
        error = "Observed runtime has no project script VM";
        return false;
    }

    [[nodiscard]] const engine::ScriptStatus& status() const noexcept override
    {
        return status_;
    }

protected:
    [[nodiscard]] bool on_initialize(const engine::EngineConfig&) override
    {
        ++script_lifecycle.initialized;
        return parent() == &world_;
    }

    [[nodiscard]] bool on_tick(double) override
    {
        ++script_lifecycle.ticks;
        return world_.is_initialized();
    }

    void on_shutdown() noexcept override
    {
        ++script_lifecycle.shutdowns;
        script_lifecycle.shutdown_before_world =
            world_.is_initialized() && engine::ResourceSystem::instance() != nullptr;
    }

private:
    engine::WorldSystem& world_;
    engine::ScriptStatus status_;
};

bool test_script_runtime_factory(const engine::EngineConfig& config)
{
    engine::RuntimeHost host;
    const auto failed_factory =
        [](engine::WorldSystem&, engine::RenderSystem&, engine::UiSystem&, engine::PhysicsSystem&,
           engine::NavigationSystem&, engine::InputSystem&,
           engine::WindowSystem&) -> std::unique_ptr<engine::ScriptRuntime> { return {}; };
    if (!expect(!host.initialize(config, nullptr, failed_factory) &&
                    engine::Engine::instance().system_count() == 0 && host.world() == nullptr,
                "failed script factory unwinds the partial host without a fallback"))
    {
        return false;
    }
    script_lifecycle = {};
    const auto factory = [](engine::WorldSystem& world, engine::RenderSystem& renderer,
                            engine::UiSystem&, engine::PhysicsSystem&, engine::NavigationSystem&,
                            engine::InputSystem&,
                            engine::WindowSystem&) -> std::unique_ptr<engine::ScriptRuntime>
    {
        script_lifecycle.renderer = &renderer;
        return std::make_unique<ObservedScriptRuntime>(world);
    };
    if (!expect(
            host.initialize(config, nullptr, factory) && script_lifecycle.world == host.world() &&
                script_lifecycle.renderer == host.renderer() && script_lifecycle.initialized == 1 &&
                host.gameplay()->script_status() != nullptr,
            "host factory supplies an abstract script child without a concrete plugin") ||
        !expect(create_cube_scene(*host.world(), false), "create scene owned by the caller") ||
        !tick_engine_frames(host, 3) ||
        !expect(script_lifecycle.ticks == 3,
                "world ticks the injected script runtime once per frame"))
    {
        return false;
    }
    host.shutdown();
    host.shutdown();
    return expect(
        script_lifecycle.scene_present_when_stopped && script_lifecycle.shutdown_before_world &&
            script_lifecycle.shutdowns == 1 && script_lifecycle.destroyed &&
            engine::Engine::instance().system_count() == 0,
        "session stops before scene teardown; world closes and destroys script exactly once");
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
    bool textures_passed = scene_passed && test_textured_renderer(config, false);
    textures_passed = textures_passed && test_textured_renderer(config, true);
    const bool sampling_passed = textures_passed && test_scene_sampling(config);
    const bool skinning_passed = sampling_passed && test_gpu_skinning(config);
    const bool settings_passed =
        skinning_passed && test_render_settings_and_capture(config) && test_async_capture(config);
    const bool reflection_passed =
        settings_passed && test_reflections_water_and_diffuse_capture(config);
    const bool hdr_passed = reflection_passed && test_hdr_and_bloom(config);
    const bool extensions_passed = hdr_passed && test_render_extensions(config);
    const bool engine_passed = extensions_passed && test_engine(config);
    const bool borrowed_passed = engine_passed && test_borrowed_resource_host(config);
    const bool script_factory_passed = borrowed_passed && test_script_runtime_factory(config);
    if (!windows_passed || !scene_passed || !textures_passed || !settings_passed || !hdr_passed ||
        !reflection_passed || !extensions_passed || !engine_passed || !borrowed_passed ||
        !script_factory_passed)
    {
        return 1;
    }
    std::puts("Window, cube/PBR draws, scene resource retirement, and Engine integration passed.");
    return 0;
}
