#include "resource_fixture.hpp"

#if defined(ENGINE_TEST_RESOURCE_GRAPH)
#include "plugins/runtime/resource_graph/resource_graph_system.hpp"
#endif

#include "runtime/app/runtime_host.hpp"
#include "runtime/engine.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/gameplay/script_runtime.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
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
    std::uint16_t textures;

    bool operator==(const GpuCounts&) const = default;
};

GpuCounts gpu_counts()
{
    const bgfx::Stats& stats = *bgfx::getStats();
    return {stats.numVertexBuffers, stats.numIndexBuffers, stats.numShaders,
            stats.numPrograms,      stats.numUniforms,     stats.numTextures};
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
    engine::TransformSettings cube_transform;
    cube_transform.rotation = {20.0, 35.0, 0.0};
    cube_transform.scale = {1.0, 0.8, 1.2};
    engine::TransformSettings camera_transform;
    camera_transform.position.mutate_z(6.0);
    camera_transform.scale = {2.0, 0.5, 3.0};
    engine::TransformSettings light_transform;
    light_transform.rotation = {-20.0, 30.0, 0.0};
    if (!expect(cube->get_component<engine::Transform>()->set_settings(cube_transform),
                "set nonuniform cube transform") ||
        !expect(camera_node->get_component<engine::Transform>()->set_settings(camera_transform),
                "place camera node") ||
        !expect(light_node->get_component<engine::Transform>()->set_settings(light_transform),
                "aim light node"))
    {
        return false;
    }
    engine::MeshRenderer* mesh = cube->add_component<engine::MeshRenderer>();
    engine::Camera* camera = camera_node->add_component<engine::Camera>();
    engine::Light* light = light_node->add_component<engine::Light>();
    if (!expect(mesh != nullptr && camera != nullptr && light != nullptr,
                "attach mesh, camera, and light components"))
    {
        return false;
    }
    auto& material = mesh->material();
    return expect(material.set_type(unlit ? "builtin/materials/default-unlit"
                                          : "builtin/materials/default-pbr"),
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
    engine::schema::ShaderDataT custom(*engine::ResourceSystem::find_shader("builtin/shaders/pbr"));
    custom.path = "project/shaders/tests/custom";
    custom.parameters.front()->name = "tint";
    engine::schema::ShaderDataT incomplete(custom);
    incomplete.path = "project/shaders/tests/incomplete";
    incomplete.parameters.erase(incomplete.parameters.begin());
    engine::schema::ShaderDataT unknown(custom);
    unknown.path = "project/shaders/tests/unknown-uniform";
    unknown.parameters.front()->uniform_name = "u_missing_parameter";
    engine::schema::MaterialDataT custom_material;
    custom_material.type_path = "project/materials/tests/custom";
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
        if (!create_cube_scene(world, cycle == 0) ||
            !expect(renderer.prepare_scene(), "prepare embedded cube resources") ||
            !expect(renderer.scene_prepared() && renderer.gpu_resource_count() > 0,
                    "prepared scene owns GPU handles") ||
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

        auto* mesh = world.scene()->nodes().front()->get_component<engine::MeshRenderer>();
        if (!expect(mesh->material().set_type(custom_material.type_path) &&
                        mesh->material().set_v4("tint", {0.2, 0.7, 0.3, 1.0}) &&
                        !mesh->material().set_v4("baseColor", {0.2, 0.7, 0.3, 1.0}),
                    "custom material resolves names from its shader") ||
            !render_frames(windows, renderer, 8) ||
            !expect(renderer.gpu_resource_count() > 0 &&
                        bgfx::getStats()->numDraw == idle_draws + 1,
                    "shader switching retires previous program and renders custom definition"))
        {
            return false;
        }
        for (const auto* invalid_path :
             {"project/shaders/tests/incomplete", "project/shaders/tests/unknown-uniform"})
        {
            if (!expect(mesh->material().set_shader(invalid_path), "select schema-valid shader") ||
                !expect(
                    !renderer.prepare_scene() && renderer.gpu_resource_count() == 0,
                    "shader reflection rejects missing and unknown uniforms with full cleanup") ||
                !expect(mesh->material().set_shader(custom.path), "restore valid shader") ||
                !render_frames(windows, renderer, 8) ||
                !expect(renderer.gpu_resource_count() > 0,
                        "renderer recovers without retaining failed program handles"))
            {
                return false;
            }
        }
        mesh->set_enabled(false);
        if (!render_frames(windows, renderer, 8) ||
            !expect(renderer.gpu_resource_count() == 0 && gpu_counts() == idle,
                    "disabling the last drawable mesh releases all scene GPU resources"))
        {
            return false;
        }
        mesh->set_enabled(true);
        if (!render_frames(windows, renderer, 8))
        {
            return false;
        }

        // 热重载替换场景时不提前通知渲染器，render 按场景编号重新准备资源。
        world.destroy_scene();
        if (!create_cube_scene(world, false) || !render_frames(windows, renderer, 8) ||
            !expect(renderer.gpu_resource_count() > 0 &&
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
                const std::filesystem::path& root, const char* name, Screenshot& output)
{
    const auto path = root / name;
    const auto native_path = engine::PathUtils::to_utf8(path);
    bgfx::requestScreenShot(BGFX_INVALID_HANDLE, native_path.c_str());
    if (!render_frames(windows, renderer, 8))
    {
        return false;
    }
    // bgfx 的默认回调保存 TGA；读取实际 GPU 输出，不给正式接口添加测试入口。
    auto image_path = path;
    image_path += ".tga";
    return expect(read_screenshot(image_path, output), "read screenshot of real GPU output");
}

engine::schema::MeshDataT textured_quad()
{
    engine::schema::MeshDataT data;
    data.path = "project/meshes/textured-quad";
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

bool test_textured_renderer(const engine::EngineConfig& config, bool shared)
{
    engine::ResourceSystem resources;
#if defined(ENGINE_TEST_RESOURCE_GRAPH)
    engine::ResourceGraphSystem graph(resources);
#endif
    engine::WorldSystem world;
    engine::WindowSystem windows;
    engine::RenderSystem renderer(windows, &world);
    test_support::ResourceFixture fixture;
    if (!expect(resources.initialize(config) && world.initialize(config) &&
                    windows.initialize(config) && renderer.initialize(config),
                "initialize texture rendering systems") ||
        !expect(fixture.add(test_texture("project/textures/blocks", true, true)) &&
                    fixture.add(test_texture("project/textures/gray-srgb", true, false)) &&
                    fixture.add(test_texture("project/textures/gray-linear", false, false)) &&
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
    const auto idle_draws = bgfx::getStats()->numDraw;
    auto* scene = world.create_scene("Texture GPU test");
    auto* quad_node = scene ? scene->create_node("Quad") : nullptr;
    auto* camera_node = scene ? scene->create_node("Camera") : nullptr;
    auto* mesh = quad_node ? quad_node->add_component<engine::MeshRenderer>() : nullptr;
    auto* camera = camera_node ? camera_node->add_component<engine::Camera>() : nullptr;
    engine::TransformSettings camera_transform;
    camera_transform.position = {0, 0, 3};
    engine::CameraSettings camera_settings;
    camera_settings.projection = engine::schema::Projection::orthographic;
    camera_settings.orthographic_size = 2;
    auto data = textured_quad();
    if (!expect(
            mesh && camera && camera->set_settings(camera_settings) &&
                camera_node->get_component<engine::Transform>()->set_settings(camera_transform) &&
                scene->set_active_camera(camera) && mesh->set_mesh_data(data) &&
                mesh->material().set_type("builtin/materials/default-unlit") &&
                mesh->material().set_v4("baseColor", {1, 1, 1, 1}) &&
                mesh->material().set_texture("baseColorTexture", "project/textures/blocks"),
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
    if (!expect(mesh->material().set_texture("baseColorTexture", "project/textures/gray-srgb"),
                "select sRGB gray texture") ||
        !render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "gray-srgb", image) ||
        !expect(near_color(image.pixel(cx, cy), 128, 128, 128),
                "sRGB sampling and display encoding preserve middle gray") ||
        !expect(mesh->material().set_texture("baseColorTexture", "project/textures/gray-linear"),
                "select linear gray texture") ||
        !render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "gray-linear", image) ||
        !expect(near_color(image.pixel(cx, cy), 188, 188, 188),
                "linear data receives display encoding exactly once"))
    {
        return false;
    }

    const auto single_mesh = gpu_counts();
    for (auto& vertex : data.vertices)
    {
        vertex.mutate_x(vertex.x() + 5);
    }
    if (!expect(mesh->set_mesh_data(data), "submit changed vertex positions") ||
        !render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "moved-vertices", image) ||
        !expect(image.pixel(cx, cy)[0] < 80 && gpu_counts() == single_mesh,
                "dynamic geometry changes pixels and retires replaced GPU buffers") ||
        !expect(mesh->set_mesh_data(textured_quad()), "restore quad geometry"))
    {
        return false;
    }

    const auto texture_increment = shared ? 0 : mesh->material().shader_data()->samplers.size();
    auto* second_node = scene->create_node("Second texture quad");
    auto* second = second_node ? second_node->add_component<engine::MeshRenderer>() : nullptr;
    engine::TransformSettings second_transform;
    second_transform.position = {2, 0, -0.1};
    if (!expect(
            second && second->set_mesh_data(textured_quad()) &&
                second_node->get_component<engine::Transform>()->set_settings(second_transform) &&
                second->material().set_type("builtin/materials/default-unlit") &&
                second->material().set_texture("baseColorTexture", "project/textures/gray-linear"),
            "add another mesh referencing the same texture path") ||
        !render_frames(windows, renderer, 8) ||
        !expect(bgfx::getStats()->numDraw == idle_draws + 2 &&
                    gpu_counts().textures == single_mesh.textures + texture_increment &&
                    gpu_counts().vertices == single_mesh.vertices + 1,
                "GPU uploads follow entity ownership: distinct in core, shared through graph"))
    {
        return false;
    }
    data = textured_quad();
    data.submeshes = {{0, 3, 0}, {3, 3, 1}};
    if (!expect(mesh->set_material_count(2) && mesh->material(1) &&
                    mesh->material(1)->set_type("builtin/materials/default-unlit") &&
                    mesh->material(1)->set_texture("baseColorTexture",
                                                   "project/textures/gray-linear") &&
                    mesh->set_mesh_data(data),
                "assign independent material slots to two submeshes") ||
        !render_frames(windows, renderer, 8) ||
        !expect(bgfx::getStats()->numDraw == idle_draws + 3 &&
                    gpu_counts().textures == single_mesh.textures + 2 * texture_increment,
                "submesh material textures obey the selected resource loading policy"))
    {
        return false;
    }
    if (!expect(mesh->material().set_type("builtin/materials/default-pbr") &&
                    mesh->material(1)->set_type("builtin/materials/default-pbr"),
                "enable PBR on both submesh materials"))
    {
        return false;
    }
    std::vector<engine::Light*> lights;
    for (const auto kind : {engine::schema::LightKind::directional,
                            engine::schema::LightKind::point, engine::schema::LightKind::spot})
    {
        auto* node = scene->create_node("Light");
        auto* light = node ? node->add_component<engine::Light>() : nullptr;
        engine::LightSettings settings;
        settings.kind = kind;
        engine::TransformSettings transform;
        transform.position = {0, 0, 2};
        if (!expect(light && light->set_settings(settings) &&
                        node->get_component<engine::Transform>()->set_settings(transform),
                    "create directional, point and spot lights"))
        {
            return false;
        }
        light->set_enabled(false);
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
        lights[index]->set_enabled(true);
        const auto name = "light-" + std::to_string(index);
        if (!render_frames(windows, renderer, 8) ||
            !screenshot(windows, renderer, fixture.root, name.c_str(), image) ||
            !expect(brightness(image.pixel(cx, cy)) > dark + 20,
                    "each directional, point and spot light contributes visible PBR lighting"))
        {
            return false;
        }
        brightest_single = std::max(brightest_single, brightness(image.pixel(cx, cy)));
        lights[index]->set_enabled(false);
    }
    for (auto* light : lights)
    {
        light->set_enabled(true);
    }
    if (!render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "all-lights", image) ||
        !expect(brightness(image.pixel(cx, cy)) > brightest_single + 5,
                "multiple light contributions accumulate in the rendered pixel") ||
        !expect(bgfx::getStats()->numDraw == idle_draws + 3,
                "multiple lights preserve mesh draw count"))
    {
        return false;
    }
    world.destroy_scene();
    renderer.release_scene();
    return render_frames(windows, renderer, 8) &&
           expect(gpu_counts() == idle && renderer.gpu_resource_count() == 0,
                  "stopping textured scene retires every texture, mesh, program and uniform");
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
    auto invalid = renderer.settings();
    invalid.exposure = std::numeric_limits<float>::quiet_NaN();
    if (!expect(!renderer.set_settings(invalid) && renderer.settings().exposure == 1.0F,
                "invalid render settings preserve the previous configuration") ||
        !expect(
            !renderer.request_screenshot("relative.png") &&
                !renderer.request_screenshot(engine::PathUtils::to_utf8(fixture.root / "bad.jpg")),
            "capture requires absolute PNG destination") ||
        !create_cube_scene(world, false))
    {
        return false;
    }
    auto* scene = world.scene();
    auto* floor = scene->create_node("Shadow receiver");
    auto* floor_transform = floor != nullptr ? floor->get_component<engine::Transform>() : nullptr;
    auto* floor_mesh = floor != nullptr ? floor->add_component<engine::MeshRenderer>() : nullptr;
    engine::TransformSettings floor_settings;
    floor_settings.position = {0.0, -1.0, 0.0};
    floor_settings.scale = {8.0, 0.1, 8.0};
    engine::TransformSettings camera_settings;
    camera_settings.position = {5.0, 6.0, 8.0};
    camera_settings.rotation = {-32.0, 32.0, 0.0};
    if (!expect(floor_transform != nullptr && floor_mesh != nullptr &&
                    floor_transform->set_settings(floor_settings) &&
                    floor_mesh->material().set_type("builtin/materials/default-pbr") &&
                    floor_mesh->material().set_v4("baseColor", {0.8, 0.8, 0.8, 1.0}) &&
                    scene->active_camera()->node().get_component<engine::Transform>()->set_settings(
                        camera_settings),
                "create receiver and oblique scene camera"))
    {
        return false;
    }
    engine::RenderSettings settings;
    settings.ambient_intensity = 0.12F;
    Screenshot unshadowed;
    Screenshot shadowed;
    if (!renderer.set_settings(settings) || !render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "without-shadow", unshadowed))
    {
        return false;
    }
    const auto unshadowed_resources = renderer.gpu_resource_count();
    settings.directional_shadows = true;
    settings.shadow_resolution = 1024;
    settings.shadow_bias = 0.0005F;
    if (!expect(renderer.set_settings(settings), "enable explicit directional shadow settings") ||
        !render_frames(windows, renderer, 8) ||
        !screenshot(windows, renderer, fixture.root, "with-shadow", shadowed) ||
        !expect(renderer.gpu_resource_count() > unshadowed_resources,
                "shadow pass owns additional GPU resources") ||
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
    settings.filmic_tonemap = true;
    settings.exposure = 1.25F;
    const auto capture = fixture.root / "render-capture.png";
    const auto capture_path = engine::PathUtils::to_utf8(capture);
    if (!renderer.set_settings(settings) || !render_frames(windows, renderer, 8) ||
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
    settings.directional_shadows = false;
    if (!renderer.set_settings(settings) || !render_frames(windows, renderer, 8) ||
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

    const auto* builtin = engine::ResourceSystem::find_shader("builtin/shaders/pbr");
    if (!expect(builtin != nullptr, "engine owns builtin definitions"))
    {
        return false;
    }
    engine::schema::ShaderDataT project_shader(*builtin);
    project_shader.path = "project/shaders/lifecycle";
    engine::schema::MaterialDataT project_material;
    project_material.type_path = "project/materials/lifecycle";
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
                    engine::ResourceSystem::find_shader("builtin/shaders/pbr") == builtin,
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

    void set_hot_reload(bool enabled) override
    {
        status_.hot_reload = enabled;
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
    const auto failed_factory = [](engine::WorldSystem&) -> std::unique_ptr<engine::ScriptRuntime>
    { return {}; };
    if (!expect(!host.initialize(config, nullptr, failed_factory) &&
                    engine::Engine::instance().system_count() == 0 && host.world() == nullptr,
                "failed script factory unwinds the partial host without a fallback"))
    {
        return false;
    }
    script_lifecycle = {};
    const auto factory = [](engine::WorldSystem& world) -> std::unique_ptr<engine::ScriptRuntime>
    { return std::make_unique<ObservedScriptRuntime>(world); };
    if (!expect(host.initialize(config, nullptr, factory) &&
                    script_lifecycle.world == host.world() && script_lifecycle.initialized == 1 &&
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
#if defined(ENGINE_TEST_RESOURCE_GRAPH)
    textures_passed = textures_passed && test_textured_renderer(config, true);
#endif
    const bool settings_passed = textures_passed && test_render_settings_and_capture(config);
    const bool engine_passed = settings_passed && test_engine(config);
    const bool borrowed_passed = engine_passed && test_borrowed_resource_host(config);
    const bool script_factory_passed = borrowed_passed && test_script_runtime_factory(config);
    if (!windows_passed || !scene_passed || !textures_passed || !settings_passed ||
        !engine_passed || !borrowed_passed || !script_factory_passed)
    {
        return 1;
    }
    std::puts("Window, cube/PBR draws, scene resource retirement, and Engine integration passed.");
    return 0;
}
