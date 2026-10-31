#include "runtime/render/render_system.hpp"
#include "runtime/font/font_system.hpp"
#include "runtime/render/ambient_occlusion_pass.hpp"
#include "runtime/render/hdr_post_process_pass.hpp"
#include "runtime/scene/text_component.hpp"
#include "runtime/ui/ui_system.hpp"

#include "runtime/resource/resource.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/post_process_component.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/utils/mesh_utils.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"

#include <SDL3/SDL.h>
#include <basisu/transcoder/basisu_transcoder.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bimg/bimg.h>
#include <bx/math.h>
#include <bx/readerwriter.h>

#include "fs_shadow.bin.h"
#include "vs_shadow.bin.h"

#if defined(SDL_PLATFORM_ANDROID)
#include "android/native/android_surface.hpp"

#include <android/native_window.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <limits>
#include <mutex>
#include <new>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace engine
{
namespace
{

// SDL 窗口和 bgfx 生命周期都由主线程驱动；bgfx 只支持一个活动实例。
RenderSystem* active_renderer = nullptr;
constexpr std::uint16_t shadow_view = 0;
constexpr std::uint16_t maximum_shadow_cascades = 4;
constexpr std::uint16_t ambient_occlusion_depth_view = 4;
constexpr std::uint16_t main_view = 6;
constexpr std::uint8_t shadow_sampler_stage = 15;
constexpr std::uint8_t ambient_occlusion_stage = 11;
constexpr std::uint8_t ibl_irradiance_stage = 12;
constexpr std::uint8_t ibl_prefilter_stage = 13;
constexpr std::uint8_t ibl_lut_stage = 14;
constexpr std::uint32_t reset_flags = BGFX_RESET_VSYNC;
constexpr std::size_t maximum_lights = 64;

// 仅在当前主线程调用期间借用；不缓存跨帧或跨场景的组件指针。
struct SceneRenderComponents
{
    EnvironmentComponent* environment = nullptr;
    PostProcessComponent* post_process = nullptr;
    Light* shadow_light = nullptr;
};

bool scene_render_components(Scene& scene, SceneRenderComponents& output)
{
    output = {};
    Light* first_directional = nullptr;
    for (const auto& node : scene.nodes())
    {
        auto* environment = node->get_component<EnvironmentComponent>();
        if (environment != nullptr && environment->enabled())
        {
            if (output.environment != nullptr)
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "Scene contains multiple enabled environment components");
                output = {};
                return false;
            }
            output.environment = environment;
        }
        auto* light = node->get_component<Light>();
        if (light == nullptr || !light->enabled() ||
            light->settings().kind != schema::LightKind::directional)
        {
            continue;
        }
        if (first_directional == nullptr)
        {
            first_directional = light;
        }
        if (light->settings().cast_shadows)
        {
            if (output.shadow_light != nullptr)
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "Scene contains multiple shadow-casting directional lights");
                output = {};
                return false;
            }
            output.shadow_light = light;
        }
    }
    if (output.shadow_light == nullptr)
    {
        output.shadow_light = first_directional;
    }
    // 独立观察镜头只决定观察位置；后处理始终来自世界场景的活动相机。
    if (auto* camera = scene.active_camera(); camera != nullptr && camera->enabled())
    {
        auto* post_process = camera->node().get_component<PostProcessComponent>();
        if (post_process != nullptr && post_process->enabled())
        {
            output.post_process = post_process;
        }
    }
    return true;
}

std::array<float, 3> color_channels(const Color& color)
{
    return {color.r(), color.g(), color.b()};
}

RenderSettings render_settings(const SceneRenderComponents& components)
{
    RenderSettings settings;
    if (components.environment != nullptr)
    {
        const auto& environment = components.environment->settings();
        settings.ambient_color = color_channels(environment.ambient_color);
        settings.ambient_intensity = environment.ambient_intensity;
        settings.ambient_tricolor = environment.ambient_tricolor;
        settings.ambient_sky_color = color_channels(environment.ambient_sky_color);
        settings.ambient_equator_color = color_channels(environment.ambient_equator_color);
        settings.ambient_ground_color = color_channels(environment.ambient_ground_color);
        settings.fog_mode = static_cast<FogMode>(environment.fog_mode);
        settings.fog_color = color_channels(environment.fog_color);
        settings.fog_start = environment.fog_start;
        settings.fog_end = environment.fog_end;
        settings.fog_density = environment.fog_density;
        settings.clear_color = environment.clear_color;
        settings.ibl_enabled = environment.ibl_enabled;
        settings.ibl_intensity = environment.ibl_intensity;
    }
    if (components.post_process != nullptr)
    {
        settings.exposure = components.post_process->settings().exposure;
        settings.filmic_tonemap = components.post_process->settings().filmic_tonemap;
        settings.hdr_enabled = components.post_process->settings().hdr_enabled;
        settings.bloom_enabled = components.post_process->settings().bloom_enabled;
        settings.bloom_threshold = components.post_process->settings().bloom_threshold;
        settings.bloom_intensity = components.post_process->settings().bloom_intensity;
        settings.bloom_radius = components.post_process->settings().bloom_radius;
        settings.ao_enabled = components.post_process->settings().ao_enabled;
        settings.ao_radius = components.post_process->settings().ao_radius;
        settings.ao_strength = components.post_process->settings().ao_strength;
        settings.ao_power = components.post_process->settings().ao_power;
        settings.ao_fade_start = components.post_process->settings().ao_fade_start;
        settings.ao_fade_end = components.post_process->settings().ao_fade_end;
    }
    if (components.shadow_light != nullptr)
    {
        const auto& light = components.shadow_light->settings();
        settings.directional_shadows = light.cast_shadows;
        settings.shadow_resolution = light.shadow_resolution;
        settings.shadow_bias = light.shadow_bias;
        settings.shadow_cascades = light.shadow_cascades;
        settings.shadow_distance = light.shadow_distance;
        settings.shadow_split_lambda = light.shadow_split_lambda;
        settings.shadow_cascade_blend = light.shadow_cascade_blend;
    }
    return settings;
}

void update_environment(const RenderSettings& settings, EnvironmentSettings& environment)
{
    const auto color = [](const std::array<float, 3>& channels, const Color& previous)
    { return Color{channels[0], channels[1], channels[2], previous.a()}; };
    environment.ambient_color = color(settings.ambient_color, environment.ambient_color);
    environment.ambient_intensity = settings.ambient_intensity;
    environment.ambient_tricolor = settings.ambient_tricolor;
    environment.ambient_sky_color =
        color(settings.ambient_sky_color, environment.ambient_sky_color);
    environment.ambient_equator_color =
        color(settings.ambient_equator_color, environment.ambient_equator_color);
    environment.ambient_ground_color =
        color(settings.ambient_ground_color, environment.ambient_ground_color);
    environment.fog_mode = static_cast<schema::FogMode>(settings.fog_mode);
    environment.fog_color = color(settings.fog_color, environment.fog_color);
    environment.fog_start = settings.fog_start;
    environment.fog_end = settings.fog_end;
    environment.fog_density = settings.fog_density;
    environment.clear_color = settings.clear_color;
    environment.ibl_enabled = settings.ibl_enabled;
    environment.ibl_intensity = settings.ibl_intensity;
}

bool finite_matrix(const float* matrix)
{
    return std::all_of(matrix, matrix + 16, [](float value) { return std::isfinite(value); });
}

double linear_determinant(const float* matrix)
{
    return static_cast<double>(matrix[0]) * (static_cast<double>(matrix[5]) * matrix[10] -
                                             static_cast<double>(matrix[9]) * matrix[6]) -
           static_cast<double>(matrix[4]) * (static_cast<double>(matrix[1]) * matrix[10] -
                                             static_cast<double>(matrix[9]) * matrix[2]) +
           static_cast<double>(matrix[8]) * (static_cast<double>(matrix[1]) * matrix[6] -
                                             static_cast<double>(matrix[5]) * matrix[2]);
}

std::uint64_t cull_state(schema::CullMode mode, const float* model)
{
    if (mode == schema::CullMode::none)
    {
        return 0;
    }
    const bool mirrored = linear_determinant(model) < 0.0;
    return mirrored != (mode == schema::CullMode::front) ? BGFX_STATE_CULL_CCW : BGFX_STATE_CULL_CW;
}

bool camera_view(const float* world, float* view)
{
    // 用节点 -Z 朝向和 +Y 上方向建立正交相机基；缩放大小不改变镜头投影。
    const double back_length =
        std::hypot(static_cast<double>(world[8]), static_cast<double>(world[9]),
                   static_cast<double>(world[10]));
    if (!(back_length > 0.0))
    {
        return false;
    }
    const std::array<double, 3> back = {world[8] / back_length, world[9] / back_length,
                                        world[10] / back_length};
    std::array<double, 3> right = {world[5] * back[2] - world[6] * back[1],
                                   world[6] * back[0] - world[4] * back[2],
                                   world[4] * back[1] - world[5] * back[0]};
    const double right_length = std::hypot(right[0], right[1], right[2]);
    if (!(right_length > 0.0))
    {
        return false;
    }
    for (double& value : right)
    {
        value /= right_length;
    }
    const std::array<double, 3> up = {back[1] * right[2] - back[2] * right[1],
                                      back[2] * right[0] - back[0] * right[2],
                                      back[0] * right[1] - back[1] * right[0]};
    for (std::size_t column = 0; column < 3; ++column)
    {
        view[column * 4] = static_cast<float>(right[column]);
        view[column * 4 + 1] = static_cast<float>(up[column]);
        view[column * 4 + 2] = static_cast<float>(back[column]);
        view[column * 4 + 3] = 0.0F;
    }
    view[12] =
        static_cast<float>(-(right[0] * world[12] + right[1] * world[13] + right[2] * world[14]));
    view[13] = static_cast<float>(-(up[0] * world[12] + up[1] * world[13] + up[2] * world[14]));
    view[14] =
        static_cast<float>(-(back[0] * world[12] + back[1] * world[13] + back[2] * world[14]));
    view[15] = 1.0F;
    return finite_matrix(view);
}

struct ShadowBounds
{
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};

    void reset() noexcept
    {
        minimum.fill(std::numeric_limits<float>::max());
        maximum.fill(std::numeric_limits<float>::lowest());
    }

    void include(const std::array<float, 3>& point) noexcept
    {
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            minimum[axis] = std::min(minimum[axis], point[axis]);
            maximum[axis] = std::max(maximum[axis], point[axis]);
        }
    }

    bool overlaps_xy(const ShadowBounds& other) const noexcept
    {
        return minimum[0] <= other.maximum[0] && maximum[0] >= other.minimum[0] &&
               minimum[1] <= other.maximum[1] && maximum[1] >= other.minimum[1];
    }
};

struct ShadowFrame
{
    std::array<float, 16> view{};
    std::array<std::array<float, 16>, maximum_shadow_cascades> matrices{};
    std::array<std::array<float, 4>, maximum_shadow_cascades> rectangles{};
    std::array<ShadowBounds, maximum_shadow_cascades> bounds{};
    std::array<float, 4> splits{};
    std::array<float, 4> parameters{};
    std::uint16_t tile_size = 0;
};

std::array<float, 3> transform_point(const float* matrix,
                                     const std::array<float, 3>& point) noexcept
{
    std::array<float, 3> result{};
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        result[axis] = matrix[axis] * point[0] + matrix[axis + 4] * point[1] +
                       matrix[axis + 8] * point[2] + matrix[axis + 12];
    }
    return result;
}

bool prepare_shadow_frame(const CameraSettings& camera, const RenderSettings& settings,
                          float aspect, const float* camera_world, const float* camera_view_matrix,
                          const std::array<float, 4>& direction, ShadowFrame& frame)
{
    const auto* caps = bgfx::getCaps();
    const float near_clip = camera.near_clip;
    const float far_clip = std::min(camera.far_clip, settings.shadow_distance);
    const auto cascade_count = settings.shadow_cascades;
    frame.tile_size =
        static_cast<std::uint16_t>(settings.shadow_resolution / (cascade_count == 1 ? 1 : 2));
    frame.parameters = {static_cast<float>(cascade_count), settings.shadow_cascade_blend, near_clip,
                        far_clip};
    frame.splits.fill(far_clip);
    const bx::Vec3 up =
        std::abs(direction[1]) > 0.95F ? bx::Vec3{0.0F, 0.0F, 1.0F} : bx::Vec3{0.0F, 1.0F, 0.0F};
    bx::mtxLookAt(frame.view.data(), {0.0F, 0.0F, 0.0F}, {direction[0], direction[1], direction[2]},
                  up, bx::Handedness::Right);
    for (std::uint16_t index = 0; index < maximum_shadow_cascades; ++index)
    {
        bx::mtxIdentity(frame.matrices[index].data());
    }
    // bgfx examples/16-shadowmaps, commit 7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099:
    // splitFrustum blends logarithmic and uniform splits; orthographic crops are texel-stable.
    // This atlas adaptation uses a frustum sphere and explicit blend overlap.
    // Copyright 2013-2014 Dario Manesku; third_party/notices/bgfx.txt.
    for (std::uint16_t index = 0; index + 1 < cascade_count; ++index)
    {
        const float fraction = static_cast<float>(index + 1) / cascade_count;
        const float logarithmic = near_clip * std::pow(far_clip / near_clip, fraction);
        const float uniform = near_clip + (far_clip - near_clip) * fraction;
        frame.splits[index] = uniform + (logarithmic - uniform) * settings.shadow_split_lambda;
    }
    constexpr float degrees_to_radians = 0.01745329251994329577F;
    const float slope = std::tan(camera.fov_degrees * degrees_to_radians * 0.5F);
    const bool orthographic = camera.projection == schema::Projection::orthographic;
    for (std::uint16_t index = 0; index < cascade_count; ++index)
    {
        float slice_near = index == 0 ? near_clip : frame.splits[index - 1];
        const float slice_far = frame.splits[index];
        if (index > 0)
        {
            const float previous_near = index == 1 ? near_clip : frame.splits[index - 2];
            // The next cascade also covers the previous cascade's blend interval.
            slice_near -= (slice_near - previous_near) * settings.shadow_cascade_blend;
        }
        const float center_depth = (slice_near + slice_far) * 0.5F;
        std::array<float, 3> center{};
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            center[axis] =
                camera_world[12 + axis] - camera_view_matrix[axis * 4 + 2] * center_depth;
        }
        center = transform_point(frame.view.data(), center);
        auto& bounds = frame.bounds[index];
        bounds.reset();
        float radius_squared = 0.0F;
        for (std::uint16_t corner = 0; corner < 8; ++corner)
        {
            const float depth = (corner & 4) != 0 ? slice_far : slice_near;
            const float half_height = orthographic ? camera.orthographic_size : depth * slope;
            const float x = ((corner & 1) != 0 ? half_height : -half_height) * aspect;
            const float y = (corner & 2) != 0 ? half_height : -half_height;
            std::array<float, 3> point{};
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                point[axis] = camera_world[12 + axis] + camera_view_matrix[axis * 4] * x +
                              camera_view_matrix[axis * 4 + 1] * y -
                              camera_view_matrix[axis * 4 + 2] * depth;
            }
            bounds.include(transform_point(frame.view.data(), point));
            const float z = depth - center_depth;
            radius_squared = std::max(radius_squared, x * x + y * y + z * z);
        }
        // A sphere gives rotation-independent XY extent. Reserve three filter texels and
        // half a texel for center snapping so the covered frustum never leaves the tile.
        const float radius = std::ceil(std::sqrt(radius_squared) * 16.0F) / 16.0F;
        const float half_extent = radius / (1.0F - 7.0F / frame.tile_size);
        const float texel_size = (2.0F * half_extent) / frame.tile_size;
        for (std::size_t axis = 0; axis < 2; ++axis)
        {
            const float snapped = std::round(center[axis] / texel_size) * texel_size;
            bounds.minimum[axis] = snapped - half_extent;
            bounds.maximum[axis] = snapped + half_extent;
        }
        const float scale = cascade_count == 1 ? 1.0F : 0.5F;
        const float tile_x = static_cast<float>(index % 2) * scale;
        const float tile_y = static_cast<float>(index / 2) * scale;
        frame.rectangles[index] = {tile_x, caps->originBottomLeft ? 1.0F - tile_y - scale : tile_y,
                                   scale, scale};
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(bounds.minimum[axis]) || !std::isfinite(bounds.maximum[axis]) ||
                bounds.maximum[axis] <= bounds.minimum[axis])
            {
                return false;
            }
        }
    }
    return finite_matrix(frame.view.data());
}

bool normal_matrix(const float* model, float* normal)
{
    const double determinant = linear_determinant(model);
    if (determinant == 0.0)
    {
        return false;
    }
    // 直接计算逆转置的 3x3 部分；double 避免较大缩放时 float 行列式溢出。
    const double a = model[0], b = model[4], c = model[8];
    const double d = model[1], e = model[5], f = model[9];
    const double g = model[2], h = model[6], i = model[10];
    const double cofactors[] = {e * i - f * h, c * h - b * i, b * f - c * e,
                                f * g - d * i, a * i - c * g, c * d - a * f,
                                d * h - e * g, b * g - a * h, a * e - b * d};
    // 全矩阵统一缩放不改变法线方向，避免 Shader normalize 的长度平方溢出。
    double largest = 0.0;
    for (double value : cofactors)
    {
        largest = std::max(largest, std::abs(value));
    }
    if (!(largest > 0.0))
    {
        return false;
    }
    const double divisor = determinant < 0.0 ? -largest : largest;
    std::fill(normal, normal + 16, 0.0F);
    for (std::size_t column = 0; column < 3; ++column)
    {
        for (std::size_t row = 0; row < 3; ++row)
        {
            normal[column * 4 + row] = static_cast<float>(cofactors[column * 3 + row] / divisor);
        }
    }
    normal[15] = 1.0F;
    return finite_matrix(normal);
}

bool drawable_scene(const Scene& scene, const Camera* camera)
{
    camera = camera != nullptr ? camera : scene.active_camera();
    if (camera == nullptr || !camera->enabled())
    {
        return false;
    }
    return std::any_of(scene.nodes().begin(), scene.nodes().end(),
                       [](const std::unique_ptr<Node>& node)
                       {
                           const MeshRenderer* mesh = node->get_component<MeshRenderer>();
                           const auto* text = node->get_component<TextComponent>();
                           return (mesh && mesh->enabled()) || (text && text->enabled());
                       });
}

bool window_size(SDL_Window* window, std::uint32_t& width, std::uint32_t& height)
{
    int pixel_width = 0;
    int pixel_height = 0;
    if (!SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height))
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot query drawable size: %s", SDL_GetError());
        return false;
    }
    constexpr int max_size = std::numeric_limits<std::uint16_t>::max();
    if (pixel_width <= 0 || pixel_height <= 0 || pixel_width > max_size || pixel_height > max_size)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Unsupported drawable size: %d x %d", pixel_width,
                     pixel_height);
        return false;
    }
    width = static_cast<std::uint32_t>(pixel_width);
    height = static_cast<std::uint32_t>(pixel_height);
    return true;
}

bool platform_data(SDL_Window* window, void* metal_view, bgfx::PlatformData& data)
{
#if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
    (void)window;
    data.nwh = SDL_Metal_GetLayer(metal_view);
#else
    (void)metal_view;
    const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
#if defined(SDL_PLATFORM_WINDOWS)
    data.nwh = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(SDL_PLATFORM_ANDROID)
    data.nwh = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
#elif defined(SDL_PLATFORM_LINUX)
    data.ndt = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
    if (data.ndt != nullptr)
    {
        data.nwh =
            SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        data.type = bgfx::NativeWindowHandleType::Wayland;
    }
    else
    {
        data.ndt = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        const auto handle = static_cast<std::uintptr_t>(
            SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
        data.nwh = reinterpret_cast<void*>(handle);
    }
#else
    (void)properties;
#endif
#endif
    if (data.nwh == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Native window surface is unavailable");
        return false;
    }
    return true;
}

} // namespace

struct RenderSystem::RendererCallbacks final : bgfx::CallbackI
{
    struct ImageWriter final : bx::WriterI
    {
        explicit ImageWriter(SDL_IOStream& stream) noexcept : stream_(stream)
        {
        }

        std::int32_t write(const void* data, std::int32_t size, bx::Error* error) override
        {
            if (size < 0)
            {
                error->setError(bx::kErrorReaderWriterWrite, "Invalid image write size");
                return 0;
            }
            const auto written = SDL_WriteIO(&stream_, data, static_cast<std::size_t>(size));
            if (written != static_cast<std::size_t>(size))
            {
                error->setError(bx::kErrorReaderWriterWrite, "Cannot write screenshot");
            }
            return static_cast<std::int32_t>(written);
        }

    private:
        SDL_IOStream& stream_;
    };

    std::atomic<ScreenshotStatus> status{ScreenshotStatus::idle};
    std::mutex mutex;
    std::string pending_path;

    void fatal(const char* file, std::uint16_t line, bgfx::Fatal::Enum code,
               const char* message) override
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "bgfx fatal %s:%u: %s", file, line, message);
        if (code != bgfx::Fatal::DebugCheck)
        {
            // bgfx 明确将非 DebugCheck 定义为不可恢复；普通渲染失败由返回值处理。
            std::abort();
        }
    }

    void traceVargs(const char*, std::uint16_t, const char* format, va_list arguments) override
    {
        SDL_LogMessageV(SDL_LOG_CATEGORY_RENDER, SDL_LOG_PRIORITY_DEBUG, format, arguments);
    }

    void profilerBegin(const char*, std::uint32_t, const char*, std::uint16_t) override
    {
    }
    void profilerBeginLiteral(const char*, std::uint32_t, const char*, std::uint16_t) override
    {
    }
    void profilerEnd() override
    {
    }
    std::uint32_t cacheReadSize(std::uint64_t) override
    {
        return 0;
    }
    bool cacheRead(std::uint64_t, void*, std::uint32_t) override
    {
        return false;
    }
    void cacheWrite(std::uint64_t, const void*, std::uint32_t) override
    {
    }
    void captureBegin(std::uint32_t, std::uint32_t, std::uint32_t, bgfx::TextureFormat::Enum,
                      bool) override
    {
    }
    void captureEnd() override
    {
    }
    void captureFrame(const void*, std::uint32_t) override
    {
    }

    void screenShot(const char* path, std::uint32_t width, std::uint32_t height,
                    std::uint32_t pitch, const void* data, std::uint32_t size, bool yflip) override
    {
        const std::string requested = path != nullptr ? path : "";
        const bool png = requested.ends_with(".png");
        const std::string output = png ? requested : requested + ".tga";
        bool success = data != nullptr && width > 0 && height > 0 &&
                       static_cast<std::uint64_t>(width) * 4 <= pitch &&
                       static_cast<std::uint64_t>(pitch) * height <= size;
        SDL_IOStream* stream = success ? SDL_IOFromFile(output.c_str(), "wb") : nullptr;
        success = stream != nullptr;
        if (stream != nullptr)
        {
            ImageWriter writer(*stream);
            bx::Error error;
            const auto written =
                png ? bimg::imageWritePng(&writer, width, height, pitch, data,
                                          bimg::TextureFormat::BGRA8, yflip, &error)
                    : bimg::imageWriteTga(&writer, width, height, pitch, data, false, yflip,
                                          &error);
            const bool closed = SDL_CloseIO(stream);
            success = written > 0 && error.isOk() && closed;
        }
        if (!success)
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Screenshot write failed: %s", output.c_str());
            if (stream != nullptr)
            {
                (void)SDL_RemovePath(output.c_str());
            }
        }
        std::lock_guard lock(mutex);
        if (status.load() == ScreenshotStatus::pending && pending_path == requested)
        {
            status.store(success ? ScreenshotStatus::complete : ScreenshotStatus::failed);
        }
    }
};

struct RenderSystem::SceneGpuResources
{
    struct Texture
    {
        ResourcePtr resource;
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;

        bool create(const ResourcePtr& source)
        {
            const auto* data = source ? source->texture() : nullptr;
            const auto* caps = bgfx::getCaps();
            if (data == nullptr || data->width == 0 || data->height == 0 ||
                data->width > caps->limits.maxTextureSize ||
                data->height > caps->limits.maxTextureSize ||
                data->width > std::numeric_limits<std::uint16_t>::max() ||
                data->height > std::numeric_limits<std::uint16_t>::max() ||
                data->data.size() > std::numeric_limits<std::uint32_t>::max())
            {
                return false;
            }
            std::uint32_t full_levels = 1;
            for (auto size = std::max(data->width, data->height); size > 1; size >>= 1)
            {
                ++full_levels;
            }
            if (data->mip_count != 1 && data->mip_count != full_levels)
            {
                return false;
            }
            const auto size = static_cast<std::uint32_t>(data->data.size());
            if (data->encoding == schema::TextureEncoding::ktx)
            {
                const auto format = data->format == schema::TextureFormat::rgba16f
                                        ? bgfx::TextureFormat::RGBA16F
                                        : bgfx::TextureFormat::RG16F;
                const auto required =
                    data->cube_map ? BGFX_CAPS_FORMAT_TEXTURE_CUBE : BGFX_CAPS_FORMAT_TEXTURE_2D;
                if (data->srgb ||
                    (data->format != schema::TextureFormat::rgba16f &&
                     data->format != schema::TextureFormat::rg16f) ||
                    (caps->formats[format] & required) == 0 ||
                    !bgfx::isTextureValid(0, data->cube_map, 1, format, 0))
                {
                    return false;
                }
                bgfx::TextureInfo info{};
                handle = bgfx::createTexture(
                    bgfx::copy(data->data.data(), size),
                    BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP, 0, &info);
                if (!bgfx::isValid(handle) || info.width != data->width ||
                    info.height != data->height || info.cubeMap != data->cube_map ||
                    info.numLayers != 1 || info.numMips != data->mip_count || info.format != format)
                {
                    if (bgfx::isValid(handle))
                    {
                        bgfx::destroy(handle);
                        handle = BGFX_INVALID_HANDLE;
                    }
                    return false;
                }
                resource = source;
                return true;
            }
            basist::basisu_transcoder decoder;
            if (data->encoding == schema::TextureEncoding::basis &&
                (!decoder.validate_header(data->data.data(), size) ||
                 decoder.get_total_images(data->data.data(), size) != 1 ||
                 decoder.get_total_image_levels(data->data.data(), size, 0) != data->mip_count ||
                 !decoder.start_transcoding(data->data.data(), size)))
            {
                return false;
            }
            if (data->encoding != schema::TextureEncoding::basis &&
                data->encoding != schema::TextureEncoding::rgba8)
            {
                return false;
            }
            using Format = bgfx::TextureFormat;
            using Transcode = basist::transcoder_texture_format;
            Format::Enum format = Format::RGBA8;
            Transcode transcode = Transcode::cTFRGBA32;
            const auto supported = [caps, data, &decoder, size](Format::Enum candidate)
            {
                const auto required =
                    data->srgb ? BGFX_CAPS_FORMAT_TEXTURE_2D_SRGB : BGFX_CAPS_FORMAT_TEXTURE_2D;
                if ((caps->formats[candidate] & required) == 0)
                {
                    return false;
                }
                if (data->encoding != schema::TextureEncoding::basis)
                {
                    return candidate == Format::RGBA8;
                }
                const auto target = candidate == Format::BC7       ? Transcode::cTFBC7_RGBA
                                    : candidate == Format::ASTC4x4 ? Transcode::cTFASTC_4x4_RGBA
                                    : candidate == Format::ETC2A   ? Transcode::cTFETC2_RGBA
                                                                   : Transcode::cTFRGBA32;
                return basist::basis_is_format_supported(
                    target, decoder.get_tex_format(data->data.data(), size));
            };
            auto requested = data->format;
            if (data->encoding == schema::TextureEncoding::rgba8)
            {
                if (requested != schema::TextureFormat::auto_format &&
                    requested != schema::TextureFormat::rgba8)
                {
                    return false;
                }
                requested = schema::TextureFormat::rgba8;
            }
            if (requested == schema::TextureFormat::auto_format)
            {
                requested = supported(Format::BC7)       ? schema::TextureFormat::bc7
                            : supported(Format::ASTC4x4) ? schema::TextureFormat::astc4x4
                            : supported(Format::ETC2A)   ? schema::TextureFormat::etc2
                                                         : schema::TextureFormat::rgba8;
            }
            switch (requested)
            {
            case schema::TextureFormat::bc7:
                format = Format::BC7;
                transcode = Transcode::cTFBC7_RGBA;
                break;
            case schema::TextureFormat::astc4x4:
                format = Format::ASTC4x4;
                transcode = Transcode::cTFASTC_4x4_RGBA;
                break;
            case schema::TextureFormat::etc2:
                format = Format::ETC2A;
                transcode = Transcode::cTFETC2_RGBA;
                break;
            case schema::TextureFormat::rgba8:
                break;
            default:
                return false;
            }
            const std::uint64_t flags = data->srgb ? BGFX_TEXTURE_SRGB : BGFX_TEXTURE_NONE;
            if (!supported(format) || !bgfx::isTextureValid(0, false, 1, format, flags))
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Unsupported GPU texture format for %s",
                             data->path.c_str());
                return false;
            }
            std::vector<std::vector<std::uint8_t>> levels;
            std::uint32_t width = data->width;
            std::uint32_t height = data->height;
            std::size_t raw_offset = 0;
            std::uint64_t total_bytes = 0;
            for (std::uint32_t mip = 0; mip < data->mip_count; ++mip)
            {
                const bool raw = format == Format::RGBA8;
                const std::uint64_t units =
                    raw ? static_cast<std::uint64_t>(width) * height
                        : static_cast<std::uint64_t>((width + 3) / 4) * ((height + 3) / 4);
                const std::uint64_t bytes = units * (raw ? 4 : 16);
                total_bytes += bytes;
                if (bytes > std::numeric_limits<std::uint32_t>::max() ||
                    total_bytes > 512ULL * 1024 * 1024)
                {
                    return false;
                }
                levels.emplace_back(static_cast<std::size_t>(bytes));
                auto& output = levels.back();
                if (data->encoding == schema::TextureEncoding::basis)
                {
                    basist::basisu_image_level_info info{};
                    if (!decoder.get_image_level_info(data->data.data(), size, info, 0, mip) ||
                        info.m_orig_width != width || info.m_orig_height != height ||
                        !decoder.transcode_image_level(
                            data->data.data(), size, 0, mip, output.data(),
                            static_cast<std::uint32_t>(units), transcode))
                    {
                        return false;
                    }
                }
                else
                {
                    if (raw_offset > data->data.size() ||
                        output.size() > data->data.size() - raw_offset)
                    {
                        return false;
                    }
                    std::copy_n(data->data.data() + raw_offset, output.size(), output.data());
                    raw_offset += output.size();
                }
                width = std::max(1U, width >> 1);
                height = std::max(1U, height >> 1);
            }
            if (data->encoding == schema::TextureEncoding::rgba8 && raw_offset != data->data.size())
            {
                return false;
            }
            handle = bgfx::createTexture2D(static_cast<std::uint16_t>(data->width),
                                           static_cast<std::uint16_t>(data->height),
                                           data->mip_count > 1, 1, format, flags);
            if (!bgfx::isValid(handle))
            {
                return false;
            }
            width = data->width;
            height = data->height;
            for (std::uint32_t mip = 0; mip < data->mip_count; ++mip)
            {
                const auto& bytes = levels[mip];
                bgfx::updateTexture2D(
                    handle, 0, static_cast<std::uint8_t>(mip), 0, 0,
                    static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height),
                    bgfx::copy(bytes.data(), static_cast<std::uint32_t>(bytes.size())));
                width = std::max(1U, width >> 1);
                height = std::max(1U, height >> 1);
            }
            resource = source;
            return true;
        }

        void release() noexcept
        {
            if (bgfx::isValid(handle))
            {
                bgfx::destroy(handle);
            }
            handle = BGFX_INVALID_HANDLE;
            resource.reset();
        }
    };

    struct Uniform
    {
        std::string name;
        bgfx::UniformHandle handle = BGFX_INVALID_HANDLE;
        std::array<float, 4> values{};
    };

    struct Sampler
    {
        std::string name;
        std::string uniform_name;
        std::uint8_t stage = 0;
        bgfx::UniformHandle handle = BGFX_INVALID_HANDLE;
    };

    struct Program
    {
        std::string path;
        schema::ShaderDataT definition;
        bgfx::ShaderHandle vertex_shader = BGFX_INVALID_HANDLE;
        bgfx::ShaderHandle fragment_shader = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
        std::vector<Uniform> uniforms;
        std::vector<Sampler> samplers;

        bool create(const schema::ShaderDataT& shader)
        {
            path = shader.path;
            definition = shader;
            vertex_shader = bgfx::createShader(bgfx::copy(
                shader.vertex_code.data(), static_cast<std::uint32_t>(shader.vertex_code.size())));
            fragment_shader = bgfx::createShader(
                bgfx::copy(shader.fragment_code.data(),
                           static_cast<std::uint32_t>(shader.fragment_code.size())));
            if (!bgfx::isValid(vertex_shader) || !bgfx::isValid(fragment_shader))
            {
                return false;
            }
            program = bgfx::createProgram(vertex_shader, fragment_shader);
            if (!bgfx::isValid(program))
            {
                return false;
            }
            // 反射与定义双向匹配，避免遗漏参数继承上一对象的 uniform 值。
            std::vector<bgfx::UniformInfo> reflected;
            for (auto stage : {vertex_shader, fragment_shader})
            {
                const auto count = bgfx::getShaderUniforms(stage);
                std::vector<bgfx::UniformHandle> handles(count);
                bgfx::getShaderUniforms(stage, handles.data(), count);
                for (auto handle : handles)
                {
                    bgfx::UniformInfo info{};
                    bgfx::getUniformInfo(handle, info);
                    const std::string_view name(info.name);
                    const bool light_array =
                        name == "u_light_position_range" || name == "u_light_direction_outer" ||
                        name == "u_light_color_intensity" || name == "u_light_kind_inner";
                    if (name == "u_normal_transform" || name == "u_camera_position" ||
                        name == "u_light_direction" || name == "u_light_color" ||
                        name == "u_light_count" || name == "u_ambient" || name == "u_tonemap" ||
                        name == "u_surface" || name == "u_ambient_sky" ||
                        name == "u_ambient_equator" || name == "u_ambient_ground" ||
                        name == "u_fog_color" || name == "u_fog_range" ||
                        name == "u_shadow_matrices" || name == "u_shadow_rects" ||
                        name == "u_shadow_splits" || name == "u_shadow_params" ||
                        name == "u_shadow_settings" || name == "u_shadow_depth" ||
                        name == "s_shadow_map" || name == "u_ibl_params" ||
                        name == "s_ibl_irradiance" || name == "s_ibl_prefilter" ||
                        name == "s_ibl_lut" || name == "s_ambient_occlusion" ||
                        name == "u_ambient_occlusion" || light_array)
                    {
                        const auto type =
                            name == "u_normal_transform" || name == "u_shadow_matrices"
                                ? bgfx::UniformType::Mat4
                            : name == "s_shadow_map" || name == "s_ibl_irradiance" ||
                                    name == "s_ibl_prefilter" || name == "s_ibl_lut" ||
                                    name == "s_ambient_occlusion"
                                ? bgfx::UniformType::Sampler
                                : bgfx::UniformType::Vec4;
                        const auto expected_count =
                            light_array ? maximum_lights
                            : name == "u_shadow_matrices" || name == "u_shadow_rects"
                                ? maximum_shadow_cascades
                                : 1;
                        if (info.type != type || info.num != expected_count)
                        {
                            return false;
                        }
                        continue;
                    }
                    const bool sampler = std::any_of(shader.samplers.begin(), shader.samplers.end(),
                                                     [&name](const auto& item)
                                                     { return item->uniform_name == name; });
                    const bool parameter = std::any_of(
                        shader.parameters.begin(), shader.parameters.end(),
                        [&name](const auto& item) { return item->uniform_name == name; });
                    if (info.num != 1 || (sampler && parameter) ||
                        (sampler ? info.type != bgfx::UniformType::Sampler
                                 : !parameter || info.type != bgfx::UniformType::Vec4))
                    {
                        return false;
                    }
                    reflected.push_back(info);
                }
            }
            const auto declared = [&reflected](const std::string& name)
            {
                return std::any_of(reflected.begin(), reflected.end(),
                                   [&name](const auto& info) { return name == info.name; });
            };
            for (const auto& parameter : shader.parameters)
            {
                if (std::any_of(uniforms.begin(), uniforms.end(), [&parameter](const auto& uniform)
                                { return uniform.name == parameter->uniform_name; }))
                {
                    continue;
                }
                if (!declared(parameter->uniform_name))
                {
                    return false;
                }
                Uniform uniform;
                uniform.name = parameter->uniform_name;
                uniform.handle = bgfx::createUniform(uniform.name.c_str(), bgfx::UniformType::Vec4);
                uniforms.push_back(std::move(uniform));
                if (!bgfx::isValid(uniforms.back().handle))
                {
                    return false;
                }
            }
            for (const auto& item : shader.samplers)
            {
                if (!declared(item->uniform_name) ||
                    item->stage >= bgfx::getCaps()->limits.maxTextureSamplers ||
                    item->stage >= ambient_occlusion_stage)
                {
                    return false;
                }
                Sampler sampler;
                sampler.name = item->name;
                sampler.uniform_name = item->uniform_name;
                sampler.stage = item->stage;
                sampler.handle =
                    bgfx::createUniform(item->uniform_name.c_str(), bgfx::UniformType::Sampler);
                samplers.push_back(std::move(sampler));
                if (!bgfx::isValid(samplers.back().handle))
                {
                    return false;
                }
            }
            return true;
        }

        void release() noexcept
        {
            if (bgfx::isValid(program))
            {
                bgfx::destroy(program);
            }
            if (bgfx::isValid(vertex_shader))
            {
                bgfx::destroy(vertex_shader);
            }
            if (bgfx::isValid(fragment_shader))
            {
                bgfx::destroy(fragment_shader);
            }
            for (const auto& uniform : uniforms)
            {
                if (bgfx::isValid(uniform.handle))
                {
                    bgfx::destroy(uniform.handle);
                }
            }
            program = BGFX_INVALID_HANDLE;
            vertex_shader = BGFX_INVALID_HANDLE;
            fragment_shader = BGFX_INVALID_HANDLE;
            uniforms.clear();
            for (const auto& sampler : samplers)
            {
                if (bgfx::isValid(sampler.handle))
                {
                    bgfx::destroy(sampler.handle);
                }
            }
            samplers.clear();
        }

        bool bind_material(const Material& material, const std::vector<Texture>& textures)
        {
            const auto* shader = &definition;
            for (auto& uniform : uniforms)
            {
                uniform.values.fill(0.0F);
            }
            for (const auto& parameter : shader->parameters)
            {
                std::array<double, 4> values{};
                std::size_t count = 0;
                const auto& value = parameter->default_value;
                switch (value.type)
                {
                case schema::MaterialValue::FloatValue:
                {
                    float scalar = value.AsFloatValue()->value;
                    if (parameter->exposed && !material.get_float(parameter->name, scalar))
                    {
                        return false;
                    }
                    values[0] = scalar;
                    count = 1;
                    break;
                }
                case schema::MaterialValue::Vec2Value:
                {
                    Vec2 vector = value.AsVec2Value()->value;
                    if (parameter->exposed && !material.get_v2(parameter->name, vector))
                    {
                        return false;
                    }
                    values = {vector.x(), vector.y(), 0.0, 0.0};
                    count = 2;
                    break;
                }
                case schema::MaterialValue::Vec3Value:
                {
                    Vec3 vector = value.AsVec3Value()->value;
                    if (parameter->exposed && !material.get_v3(parameter->name, vector))
                    {
                        return false;
                    }
                    values = {vector.x(), vector.y(), vector.z(), 0.0};
                    count = 3;
                    break;
                }
                case schema::MaterialValue::Vec4Value:
                {
                    Vec4 vector = value.AsVec4Value()->value;
                    if (parameter->exposed && !material.get_v4(parameter->name, vector))
                    {
                        return false;
                    }
                    values = {vector.x(), vector.y(), vector.z(), vector.w()};
                    count = 4;
                    break;
                }
                default:
                    return false;
                }
                auto uniform =
                    std::find_if(uniforms.begin(), uniforms.end(), [&parameter](const auto& item)
                                 { return item.name == parameter->uniform_name; });
                if (uniform == uniforms.end() || parameter->uniform_offset + count > 4)
                {
                    return false;
                }
                for (std::size_t index = 0; index < count; ++index)
                {
                    uniform->values[parameter->uniform_offset + index] =
                        static_cast<float>(values[index]);
                }
            }
            for (const auto& uniform : uniforms)
            {
                bgfx::setUniform(uniform.handle, uniform.values.data());
            }
            for (const auto& sampler : samplers)
            {
                const auto& resource = material.texture(sampler.name);
                const auto texture =
                    std::find_if(textures.begin(), textures.end(), [&resource](const auto& item)
                                 { return item.resource == resource; });
                if (!resource || resource->texture() == nullptr || resource->texture()->cube_map ||
                    texture == textures.end())
                {
                    return false;
                }
                bgfx::setTexture(sampler.stage, sampler.handle, texture->handle);
            }
            return true;
        }
    };

    struct Mesh
    {
        struct Vertex
        {
            float position[3];
            float normal[3];
            float tangent[4];
            float texcoord[2];
            float color[4];
        };
        static_assert(sizeof(Vertex) == 16 * sizeof(float));

        std::uint64_t node_id = 0;
        std::uint64_t instance_id = 0;
        std::uint64_t revision = 0;
        std::uint64_t deformation_revision = 0;
        bgfx::VertexBufferHandle vertices = BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle deformed_vertices = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle indices = BGFX_INVALID_HANDLE;
        std::array<float, 3> minimum{};
        std::array<float, 3> maximum{};
        std::array<float, 3> source_minimum{};
        std::array<float, 3> source_maximum{};

        static bgfx::VertexLayout vertex_layout()
        {
            bgfx::VertexLayout layout;
            layout.begin()
                .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
                .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
                .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float)
                .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
                .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
                .end();
            return layout;
        }

        bool pack_vertices(const schema::MeshDataT& data,
                           std::span<const schema::MeshVertex> positions,
                           std::span<const Vec4> tangents, std::vector<Vertex>& packed)
        {
            if (positions.size() != data.vertices.size() || tangents.size() != positions.size())
            {
                return false;
            }
            packed.resize(positions.size());
            minimum.fill(std::numeric_limits<float>::max());
            maximum.fill(std::numeric_limits<float>::lowest());
            for (std::size_t index = 0; index < packed.size(); ++index)
            {
                const auto& source = positions[index];
                const auto& tangent = tangents[index];
                const auto uv = data.texcoords.empty() ? schema::Vec2{} : data.texcoords[index];
                const auto color =
                    data.colors.empty() ? schema::Color{1, 1, 1, 1} : data.colors[index];
                const double normal_length =
                    std::hypot(static_cast<double>(source.nx()), static_cast<double>(source.ny()),
                               static_cast<double>(source.nz()));
                if (!(normal_length > 0.0))
                {
                    return false;
                }
                const std::array<double, 3> normal = {source.nx() / normal_length,
                                                      source.ny() / normal_length,
                                                      source.nz() / normal_length};
                const std::array<double, 3> bitangent = {
                    normal[1] * tangent.z() - normal[2] * tangent.y(),
                    normal[2] * tangent.x() - normal[0] * tangent.z(),
                    normal[0] * tangent.y() - normal[1] * tangent.x()};
                const std::array<double, 3> orthogonal = {
                    bitangent[1] * normal[2] - bitangent[2] * normal[1],
                    bitangent[2] * normal[0] - bitangent[0] * normal[2],
                    bitangent[0] * normal[1] - bitangent[1] * normal[0]};
                const double tangent_length =
                    std::hypot(orthogonal[0], orthogonal[1], orthogonal[2]);
                if (!(tangent_length > 0.0))
                {
                    return false;
                }
                packed[index] = {{source.x(), source.y(), source.z()},
                                 {static_cast<float>(normal[0]), static_cast<float>(normal[1]),
                                  static_cast<float>(normal[2])},
                                 {static_cast<float>(orthogonal[0] / tangent_length),
                                  static_cast<float>(orthogonal[1] / tangent_length),
                                  static_cast<float>(orthogonal[2] / tangent_length),
                                  static_cast<float>(tangent.w())},
                                 {static_cast<float>(uv.x()), static_cast<float>(uv.y())},
                                 {color.r(), color.g(), color.b(), color.a()}};
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    minimum[axis] = std::min(minimum[axis], packed[index].position[axis]);
                    maximum[axis] = std::max(maximum[axis], packed[index].position[axis]);
                }
            }
            return true;
        }

        bool create(const MeshRenderer& renderer)
        {
            const auto* data = renderer.mesh_data();
            std::vector<schema::Vec4> tangents;
            std::vector<Vertex> packed;
            if (data == nullptr || !MeshUtils::build_tangents(*data, tangents) ||
                !pack_vertices(*data, data->vertices, tangents, packed))
            {
                return false;
            }
            node_id = renderer.node().id();
            instance_id = renderer.mesh_instance_id();
            revision = renderer.mesh_revision();
            source_minimum = minimum;
            source_maximum = maximum;
            const auto vertex_bytes = packed.size() * sizeof(Vertex);
            const auto index_bytes = data->indices.size() * sizeof(std::uint32_t);
            vertices = bgfx::createVertexBuffer(
                bgfx::copy(packed.data(), static_cast<std::uint32_t>(vertex_bytes)),
                vertex_layout());
            indices = bgfx::createIndexBuffer(
                bgfx::copy(data->indices.data(), static_cast<std::uint32_t>(index_bytes)),
                BGFX_BUFFER_INDEX32);
            return bgfx::isValid(vertices) && bgfx::isValid(indices);
        }

        bool update_deformation(const MeshRenderer& renderer)
        {
            const auto positions = renderer.deformed_vertices();
            if (deformation_revision == renderer.deformation_revision() &&
                bgfx::isValid(deformed_vertices) == !positions.empty())
            {
                return true;
            }
            if (positions.empty())
            {
                if (bgfx::isValid(deformed_vertices))
                {
                    bgfx::destroy(deformed_vertices);
                    deformed_vertices = BGFX_INVALID_HANDLE;
                }
                minimum = source_minimum;
                maximum = source_maximum;
            }
            else
            {
                const auto* data = renderer.mesh_data();
                std::vector<Vertex> packed;
                if (data == nullptr ||
                    !pack_vertices(*data, positions, renderer.deformed_tangents(), packed))
                {
                    return false;
                }
                // bgfx examples/35-dynamic: keep topology and update only the vertex stream.
                // Copyright 2011-2025 Branimir Karadzic; third_party/notices/bgfx.txt.
                const auto* memory = bgfx::copy(
                    packed.data(), static_cast<std::uint32_t>(packed.size() * sizeof(Vertex)));
                if (bgfx::isValid(deformed_vertices))
                {
                    bgfx::update(deformed_vertices, 0, memory);
                }
                else
                {
                    deformed_vertices = bgfx::createDynamicVertexBuffer(memory, vertex_layout());
                    if (!bgfx::isValid(deformed_vertices))
                    {
                        return false;
                    }
                }
            }
            deformation_revision = renderer.deformation_revision();
            return true;
        }

        void bind_vertices() const
        {
            if (bgfx::isValid(deformed_vertices))
            {
                bgfx::setVertexBuffer(0, deformed_vertices);
            }
            else
            {
                bgfx::setVertexBuffer(0, vertices);
            }
        }

        void release() noexcept
        {
            if (bgfx::isValid(vertices))
            {
                bgfx::destroy(vertices);
            }
            if (bgfx::isValid(deformed_vertices))
            {
                bgfx::destroy(deformed_vertices);
            }
            if (bgfx::isValid(indices))
            {
                bgfx::destroy(indices);
            }
            vertices = BGFX_INVALID_HANDLE;
            deformed_vertices = BGFX_INVALID_HANDLE;
            indices = BGFX_INVALID_HANDLE;
            deformation_revision = 0;
        }
    };

    struct Shadow
    {
        bgfx::TextureHandle color = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle depth = BGFX_INVALID_HANDLE;
        bgfx::FrameBufferHandle framebuffer = BGFX_INVALID_HANDLE;
        bgfx::ShaderHandle vertex = BGFX_INVALID_HANDLE;
        bgfx::ShaderHandle fragment = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle alpha = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle alpha_sampler = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle uv_transform = BGFX_INVALID_HANDLE;
        std::uint16_t resolution = 0;

        void release() noexcept
        {
            if (bgfx::isValid(framebuffer))
            {
                for (std::uint16_t index = 0; index < maximum_shadow_cascades; ++index)
                {
                    bgfx::setViewFrameBuffer(shadow_view + index, BGFX_INVALID_HANDLE);
                }
                bgfx::destroy(framebuffer);
            }
            if (bgfx::isValid(color))
            {
                bgfx::destroy(color);
            }
            if (bgfx::isValid(depth))
            {
                bgfx::destroy(depth);
            }
            if (bgfx::isValid(program))
            {
                bgfx::destroy(program);
            }
            if (bgfx::isValid(vertex))
            {
                bgfx::destroy(vertex);
            }
            if (bgfx::isValid(fragment))
            {
                bgfx::destroy(fragment);
            }
            if (bgfx::isValid(alpha))
            {
                bgfx::destroy(alpha);
            }
            if (bgfx::isValid(alpha_sampler))
            {
                bgfx::destroy(alpha_sampler);
            }
            if (bgfx::isValid(uv_transform))
            {
                bgfx::destroy(uv_transform);
            }
            *this = {};
        }

        bool prepare(std::uint16_t size)
        {
            if (resolution == size && bgfx::isValid(framebuffer) && bgfx::isValid(program))
            {
                return true;
            }
            release();
            constexpr auto color_flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
                                         BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT |
                                         BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
            constexpr auto depth_flags = BGFX_TEXTURE_RT_WRITE_ONLY;
            if (size > bgfx::getCaps()->limits.maxTextureSize ||
                !bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::RGBA8, color_flags) ||
                !bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::D16, depth_flags))
            {
                return false;
            }
            color = bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::RGBA8,
                                          color_flags);
            depth =
                bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::D16, depth_flags);
            if (!bgfx::isValid(color) || !bgfx::isValid(depth))
            {
                release();
                return false;
            }
            const bgfx::TextureHandle attachments[] = {color, depth};
            framebuffer = bgfx::createFrameBuffer(2, attachments, false);
            vertex = bgfx::createShader(bgfx::copy(vs_shadow, sizeof(vs_shadow)));
            fragment = bgfx::createShader(bgfx::copy(fs_shadow, sizeof(fs_shadow)));
            if (bgfx::isValid(vertex) && bgfx::isValid(fragment))
            {
                program = bgfx::createProgram(vertex, fragment);
            }
            alpha = bgfx::createUniform("u_shadow_alpha", bgfx::UniformType::Vec4);
            alpha_sampler = bgfx::createUniform("s_shadow_alpha", bgfx::UniformType::Sampler);
            uv_transform = bgfx::createUniform("u_shadow_uv", bgfx::UniformType::Vec4);
            if (!bgfx::isValid(framebuffer) || !bgfx::isValid(program) || !bgfx::isValid(alpha) ||
                !bgfx::isValid(alpha_sampler) || !bgfx::isValid(uv_transform))
            {
                release();
                return false;
            }
            resolution = size;
            return true;
        }

        std::uint32_t resource_count() const noexcept
        {
            return static_cast<std::uint32_t>(bgfx::isValid(color)) +
                   static_cast<std::uint32_t>(bgfx::isValid(depth)) +
                   static_cast<std::uint32_t>(bgfx::isValid(framebuffer)) +
                   static_cast<std::uint32_t>(bgfx::isValid(vertex)) +
                   static_cast<std::uint32_t>(bgfx::isValid(fragment)) +
                   static_cast<std::uint32_t>(bgfx::isValid(program)) +
                   static_cast<std::uint32_t>(bgfx::isValid(alpha)) +
                   static_cast<std::uint32_t>(bgfx::isValid(alpha_sampler)) +
                   static_cast<std::uint32_t>(bgfx::isValid(uv_transform));
        }
    };

    std::vector<Mesh> meshes;
    std::vector<Texture> textures;
    bgfx::UniformHandle normal_transform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle camera_position = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_direction = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_color = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_count = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_position_range = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_direction_outer = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_color_intensity = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_kind_inner = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ambient = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ambient_sky = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ambient_equator = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ambient_ground = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle fog_color = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle fog_range = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle tonemap = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle surface = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadow_matrices = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadow_rects = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadow_splits = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadow_params = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadow_settings = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadow_depth = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadow_sampler = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shadow_fallback = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ibl_params = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ibl_irradiance_sampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ibl_prefilter_sampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ibl_lut_sampler = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle ibl_cube_fallback = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle ibl_lut_fallback = BGFX_INVALID_HANDLE;
    std::array<Texture, 3> ibl_textures;
    std::uint64_t ibl_generation = 0;
    Shadow shadow;
    AmbientOcclusionPass ambient_occlusion;
    HdrPostProcessPass hdr_post_process;
    bgfx::UniformHandle ao_settings = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ao_sampler = BGFX_INVALID_HANDLE;
    std::vector<Program> programs;

    bool prepare_ibl(const Scene& scene)
    {
        const EnvironmentSettings* environment = nullptr;
        for (const auto& node : scene.nodes())
        {
            const auto* component = node->get_component<EnvironmentComponent>();
            if (component != nullptr && component->enabled())
            {
                environment = &component->settings();
                break;
            }
        }
        if (environment == nullptr || environment->ibl_irradiance_guid.empty())
        {
            for (auto& texture : ibl_textures)
            {
                texture.release();
            }
            ibl_generation = 0;
            return true;
        }
        auto* resources = ResourceSystem::instance();
        if (resources == nullptr)
        {
            return false;
        }
        const std::array guids = {&environment->ibl_irradiance_guid,
                                  &environment->ibl_prefilter_guid, &environment->ibl_lut_guid};
        bool changed = ibl_generation != resources->generation();
        for (std::size_t index = 0; index < guids.size(); ++index)
        {
            changed = changed || !ibl_textures[index].resource ||
                      ibl_textures[index].resource->info().guid != *guids[index];
        }
        if (!changed)
        {
            return true;
        }
        std::array<Texture, 3> candidate;
        for (std::size_t index = 0; index < guids.size(); ++index)
        {
            ResourcePtr source;
            const bool loaded = resources->loader().load(*guids[index], source);
            const auto* texture = source ? source->texture() : nullptr;
            const bool cube = index < 2;
            const auto format =
                cube ? schema::TextureFormat::rgba16f : schema::TextureFormat::rg16f;
            if (!loaded || texture == nullptr || texture->cube_map != cube || texture->srgb ||
                texture->format != format || !candidate[index].create(source))
            {
                for (auto& item : candidate)
                {
                    item.release();
                }
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Invalid IBL texture resource: %s",
                             guids[index]->c_str());
                return false;
            }
        }
        for (std::size_t index = 0; index < candidate.size(); ++index)
        {
            ibl_textures[index].release();
            ibl_textures[index] = std::move(candidate[index]);
        }
        ibl_generation = resources->generation();
        return true;
    }

    bool bind_shadow_material(const Material& material)
    {
        const auto* shader = material.shader_data();
        if (shader == nullptr)
        {
            return false;
        }
        const auto parameter = [&shader](std::string_view name)
        {
            const auto found =
                std::find_if(shader->parameters.begin(), shader->parameters.end(),
                             [name](const auto& item) { return item->name == name; });
            return found != shader->parameters.end() ? found->get() : nullptr;
        };
        float cutoff = 0.0F;
        if (const auto* definition = parameter("alphaCutoff"))
        {
            const auto* value = definition->default_value.AsFloatValue();
            if (value == nullptr)
            {
                return false;
            }
            cutoff = value->value;
            if ((definition->exposed && !material.get_float("alphaCutoff", cutoff)) ||
                !std::isfinite(cutoff) || cutoff < 0.0F || cutoff > 1.0F)
            {
                return false;
            }
        }
        float base_alpha = 1.0F;
        Vec4 uv_transform(1.0, 1.0, 0.0, 0.0);
        bgfx::TextureHandle texture = shadow_fallback;
        if (cutoff > 0.0F)
        {
            const auto* color = parameter("baseColor");
            const auto* value = color != nullptr ? color->default_value.AsVec4Value() : nullptr;
            if (value == nullptr)
            {
                return false;
            }
            Vec4 tint = value->value;
            if (color->exposed && !material.get_v4("baseColor", tint))
            {
                return false;
            }
            base_alpha = static_cast<float>(tint.w());
            const auto& resource = material.texture("baseColorTexture");
            const auto found =
                std::find_if(textures.begin(), textures.end(),
                             [&resource](const auto& item) { return item.resource == resource; });
            if (!resource || found == textures.end())
            {
                return false;
            }
            texture = found->handle;
            if (const auto* uv = parameter("uvTransform"))
            {
                const auto* transform = uv->default_value.AsVec4Value();
                if (transform == nullptr)
                {
                    return false;
                }
                uv_transform = transform->value;
                if (uv->exposed && !material.get_v4("uvTransform", uv_transform))
                {
                    return false;
                }
            }
        }
        const float alpha[] = {cutoff, base_alpha, 0.0F, 0.0F};
        bgfx::setUniform(shadow.alpha, alpha);
        const float uv[] = {
            static_cast<float>(uv_transform.x()), static_cast<float>(uv_transform.y()),
            static_cast<float>(uv_transform.z()), static_cast<float>(uv_transform.w())};
        bgfx::setUniform(shadow.uv_transform, uv);
        bgfx::setTexture(0, shadow.alpha_sampler, texture);
        return true;
    }

    bool draw_shadow(const Scene& scene, const RenderSettings& settings, ShadowFrame& frame)
    {
        struct Caster
        {
            const Mesh* mesh = nullptr;
            const MeshRenderer* renderer = nullptr;
            std::array<float, 16> model{};
            ShadowBounds bounds;
        };
        std::vector<Caster> casters;
        casters.reserve(meshes.size());
        for (const auto& mesh : meshes)
        {
            const Node* node = scene.find_node(mesh.node_id);
            const auto* renderer = node != nullptr ? node->get_component<MeshRenderer>() : nullptr;
            const auto* transform = node != nullptr ? node->get_component<Transform>() : nullptr;
            if (renderer != nullptr && !renderer->cast_shadows())
            {
                continue;
            }
            Caster caster;
            if (renderer == nullptr || renderer->mesh_data() == nullptr || transform == nullptr ||
                !transform->world_matrix(caster.model.data()))
            {
                return false;
            }
            caster.mesh = &mesh;
            caster.renderer = renderer;
            caster.bounds.reset();
            float combined[16]{};
            bx::mtxMul(combined, caster.model.data(), frame.view.data());
            for (std::uint16_t corner = 0; corner < 8; ++corner)
            {
                const std::array<float, 3> point = {
                    (corner & 1) != 0 ? mesh.maximum[0] : mesh.minimum[0],
                    (corner & 2) != 0 ? mesh.maximum[1] : mesh.minimum[1],
                    (corner & 4) != 0 ? mesh.maximum[2] : mesh.minimum[2]};
                caster.bounds.include(transform_point(combined, point));
            }
            casters.push_back(caster);
        }
        const auto* caps = bgfx::getCaps();
        const float crop[] = {0.5F,
                              0.0F,
                              0.0F,
                              0.0F,
                              0.0F,
                              caps->originBottomLeft ? 0.5F : -0.5F,
                              0.0F,
                              0.0F,
                              0.0F,
                              0.0F,
                              caps->homogeneousDepth ? 0.5F : 1.0F,
                              0.0F,
                              0.5F,
                              0.5F,
                              caps->homogeneousDepth ? 0.5F : 0.0F,
                              1.0F};
        const float depth_scale[] = {caps->homogeneousDepth ? 0.5F : 1.0F,
                                     caps->homogeneousDepth ? 0.5F : 0.0F, 0.0F, 0.0F};
        for (std::uint16_t index = 0; index < settings.shadow_cascades; ++index)
        {
            auto bounds = frame.bounds[index];
            for (const auto& caster : casters)
            {
                if (bounds.overlaps_xy(caster.bounds))
                {
                    // Include off-screen occluders whose light-space footprint reaches the
                    // receiver slice. Camera-frustum culling alone would lose their shadows.
                    bounds.minimum[2] = std::min(bounds.minimum[2], caster.bounds.minimum[2]);
                    bounds.maximum[2] = std::max(bounds.maximum[2], caster.bounds.maximum[2]);
                }
            }
            const float margin = std::max(0.1F, (bounds.maximum[2] - bounds.minimum[2]) * 0.01F);
            bounds.minimum[2] -= margin;
            bounds.maximum[2] += margin;
            float projection[16]{};
            bx::mtxOrtho(projection, bounds.minimum[0], bounds.maximum[0], bounds.minimum[1],
                         bounds.maximum[1], -bounds.maximum[2], -bounds.minimum[2], 0.0F,
                         caps->homogeneousDepth, bx::Handedness::Right);
            float projected[16]{};
            bx::mtxMul(projected, projection, crop);
            bx::mtxMul(frame.matrices[index].data(), frame.view.data(), projected);
            if (!finite_matrix(frame.matrices[index].data()))
            {
                return false;
            }
            const auto view = static_cast<std::uint16_t>(shadow_view + index);
            const auto tile_x = static_cast<std::uint16_t>((index % 2) * frame.tile_size);
            const auto tile_y = static_cast<std::uint16_t>((index / 2) * frame.tile_size);
            bgfx::setViewFrameBuffer(view, shadow.framebuffer);
            bgfx::setViewRect(view, tile_x, tile_y, frame.tile_size, frame.tile_size);
            bgfx::setViewClear(view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0xffffffff, 1.0F, 0);
            bgfx::setViewTransform(view, frame.view.data(), projection);
            bgfx::touch(view);
            for (const auto& caster : casters)
            {
                if (!bounds.overlaps_xy(caster.bounds))
                {
                    continue;
                }
                const auto& mesh = *caster.mesh;
                const auto* data = caster.renderer->mesh_data();
                const auto submit = [&](std::uint32_t first, std::uint32_t count, std::size_t slot)
                {
                    const auto* material = caster.renderer->material(slot);
                    if (material == nullptr || !bind_shadow_material(*material))
                    {
                        return false;
                    }
                    bgfx::setTransform(caster.model.data());
                    bgfx::setUniform(shadow_depth, depth_scale);
                    mesh.bind_vertices();
                    bgfx::setIndexBuffer(mesh.indices, first, count);
                    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                                   BGFX_STATE_DEPTH_TEST_LESS |
                                   cull_state(material->cull_mode(), caster.model.data()));
                    bgfx::submit(view, shadow.program);
                    return true;
                };
                if (data->submeshes.empty() &&
                    !submit(0, static_cast<std::uint32_t>(data->indices.size()), 0))
                {
                    return false;
                }
                for (const auto& submesh : data->submeshes)
                {
                    if (!submit(submesh.first_index(), submesh.index_count(),
                                submesh.material_slot()))
                    {
                        return false;
                    }
                }
            }
        }
        return true;
    }
};

RenderSystem::RenderSystem(WindowSystem& windows, WorldSystem* world, FontSystem* fonts,
                           UiSystem* ui) noexcept
    : windows_(windows), world_(world), fonts_(fonts), ui_(ui)
{
}

RenderSystem::~RenderSystem() noexcept
{
    shutdown();
}

bool RenderSystem::on_initialize(const EngineConfig&)
{
#if defined(SDL_PLATFORM_ANDROID)
    if (!SDL_AddEventWatch(watch_surface_lifecycle, this))
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Renderer event watch failed: %s", SDL_GetError());
        return false;
    }
    watching_events_ = true;
#endif
    return initialize_renderer();
}

bool RenderSystem::watch_surface_lifecycle(void* userdata, SDL_Event* event)
{
#if defined(SDL_PLATFORM_ANDROID)
    // 原生后台事件和平台暂停握手均在 SDL 主线程处理。
    const bool suspend =
        event->type == SDL_EVENT_WILL_ENTER_BACKGROUND ||
        (event->type == SDL_EVENT_USER && event->user.code == android::suspend_rendering_event);
    if (suspend && SDL_IsMainThread())
    {
        static_cast<RenderSystem*>(userdata)->shutdown_renderer();
    }
#else
    (void)userdata;
    (void)event;
#endif
    return true;
}

bool RenderSystem::initialize_renderer()
{
    if (active_renderer != nullptr || !windows_.is_initialized() ||
        windows_.native_window() == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                     "Renderer requires a ready window and no active bgfx");
        return false;
    }

#if defined(SDL_PLATFORM_ANDROID)
    if (!android::begin_rendering())
    {
        // 宿主在初始化途中进入后台；保留系统，等恢复后再创建 GPU 资源。
        return true;
    }
    surface_registered_ = true;
#endif

    SDL_Window* window = windows_.native_window();
    if (!window_size(window, width_, height_))
    {
        return false;
    }

    bgfx::Init init{};
#if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
    metal_view_ = SDL_Metal_CreateView(window);
    if (metal_view_ == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Metal view creation failed: %s", SDL_GetError());
        return false;
    }
    init.type = bgfx::RendererType::Metal;
#elif defined(SDL_PLATFORM_WINDOWS)
    init.type = bgfx::RendererType::Direct3D12;
#else
    init.type = bgfx::RendererType::Vulkan;
#endif
    if (!platform_data(window, metal_view_, init.platformData))
    {
        return false;
    }
    native_window_ = init.platformData.nwh;
    native_display_ = init.platformData.ndt;
#if defined(SDL_PLATFORM_ANDROID)
    // SDL 可以先释放自己的引用；bgfx 后台线程退出后才释放渲染器持有的引用。
    ANativeWindow_acquire(static_cast<ANativeWindow*>(native_window_));
#endif
    init.resolution.width = width_;
    init.resolution.height = height_;
    init.resolution.reset = reset_flags;
    if (callbacks_ == nullptr)
    {
        callbacks_.reset(new (std::nothrow) RendererCallbacks{});
        if (callbacks_ == nullptr)
        {
            return false;
        }
    }
    init.callback = callbacks_.get();
    if (!bgfx::init(init))
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "bgfx initialization failed");
        return false;
    }
    renderer_initialized_ = true;
    active_renderer = this;
    if (bgfx::getRendererType() != init.type)
    {
        SDL_LogError(
            SDL_LOG_CATEGORY_RENDER, "Required renderer %s is unavailable; bgfx selected %s",
            bgfx::getRendererName(init.type), bgfx::getRendererName(bgfx::getRendererType()));
        return false;
    }

    bgfx::setViewName(main_view, "Main window");
    constexpr std::array shadow_names = {"Directional shadow 1", "Directional shadow 2",
                                         "Directional shadow 3", "Directional shadow 4"};
    for (std::uint16_t index = 0; index < maximum_shadow_cascades; ++index)
    {
        bgfx::setViewName(shadow_view + index, shadow_names[index]);
    }
    bgfx::setViewClear(main_view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, idle_settings_.clear_color,
                       1.0F, 0);
    bgfx::setDebug(stats_visible_ ? BGFX_DEBUG_STATS : BGFX_DEBUG_NONE);
    SDL_Log("Renderer initialized: %s (%u x %u)", bgfx::getRendererName(bgfx::getRendererType()),
            width_, height_);
    return true;
}

bool RenderSystem::prepare_scene(const Camera* camera)
{
    if (!is_initialized())
    {
        return false;
    }
    RenderSettings current_settings;
    if (!resolve_settings(current_settings))
    {
        return false;
    }
    const Scene* scene = world_ != nullptr ? world_->scene() : nullptr;
    if (scene == nullptr || scene->id() != scene_id_)
    {
        // UI 可能已在玩法初始化中准备，切换绘制资源不销毁新的 View。
        release_draw_resources();
        if (fonts_)
        {
            fonts_->release_scene();
        }
        if (ui_ && !ui_->synchronize())
        {
            return false;
        }
    }
    if (scene == nullptr)
    {
        return true;
    }
    if (!drawable_scene(*scene, camera))
    {
        if (scene_resources_)
        {
            release_draw_resources();
        }
        scene_id_ = scene->id();
        return true;
    }
    const auto lights = std::count_if(scene->nodes().begin(), scene->nodes().end(),
                                      [](const auto& node)
                                      {
                                          const auto* light = node->template get_component<Light>();
                                          return light != nullptr && light->enabled();
                                      });
    if (lights > static_cast<std::ptrdiff_t>(maximum_lights))
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Scene exceeds the limit of %zu active lights",
                     maximum_lights);
        release_scene();
        return false;
    }
    if (!renderer_initialized_)
    {
        // Android 暂停时保留 CPU 场景，恢复 Surface 后再创建 GPU 对象。
        return true;
    }
    if (scene_resources_ == nullptr)
    {
        scene_resources_.reset(new (std::nothrow) SceneGpuResources{});
        if (scene_resources_ == nullptr)
        {
            return false;
        }
        auto& resources = *scene_resources_;
        resources.normal_transform =
            bgfx::createUniform("u_normal_transform", bgfx::UniformType::Mat4);
        resources.camera_position =
            bgfx::createUniform("u_camera_position", bgfx::UniformType::Vec4);
        resources.light_direction =
            bgfx::createUniform("u_light_direction", bgfx::UniformType::Vec4);
        resources.light_color = bgfx::createUniform("u_light_color", bgfx::UniformType::Vec4);
        resources.light_count = bgfx::createUniform("u_light_count", bgfx::UniformType::Vec4);
        resources.light_position_range =
            bgfx::createUniform("u_light_position_range", bgfx::UniformType::Vec4, maximum_lights);
        resources.light_direction_outer =
            bgfx::createUniform("u_light_direction_outer", bgfx::UniformType::Vec4, maximum_lights);
        resources.light_color_intensity =
            bgfx::createUniform("u_light_color_intensity", bgfx::UniformType::Vec4, maximum_lights);
        resources.light_kind_inner =
            bgfx::createUniform("u_light_kind_inner", bgfx::UniformType::Vec4, maximum_lights);
        resources.ambient = bgfx::createUniform("u_ambient", bgfx::UniformType::Vec4);
        resources.ambient_sky = bgfx::createUniform("u_ambient_sky", bgfx::UniformType::Vec4);
        resources.ambient_equator =
            bgfx::createUniform("u_ambient_equator", bgfx::UniformType::Vec4);
        resources.ambient_ground = bgfx::createUniform("u_ambient_ground", bgfx::UniformType::Vec4);
        resources.fog_color = bgfx::createUniform("u_fog_color", bgfx::UniformType::Vec4);
        resources.fog_range = bgfx::createUniform("u_fog_range", bgfx::UniformType::Vec4);
        resources.tonemap = bgfx::createUniform("u_tonemap", bgfx::UniformType::Vec4);
        resources.surface = bgfx::createUniform("u_surface", bgfx::UniformType::Vec4);
        resources.shadow_matrices = bgfx::createUniform(
            "u_shadow_matrices", bgfx::UniformType::Mat4, maximum_shadow_cascades);
        resources.shadow_rects =
            bgfx::createUniform("u_shadow_rects", bgfx::UniformType::Vec4, maximum_shadow_cascades);
        resources.shadow_splits = bgfx::createUniform("u_shadow_splits", bgfx::UniformType::Vec4);
        resources.shadow_params = bgfx::createUniform("u_shadow_params", bgfx::UniformType::Vec4);
        resources.shadow_settings =
            bgfx::createUniform("u_shadow_settings", bgfx::UniformType::Vec4);
        resources.shadow_depth = bgfx::createUniform("u_shadow_depth", bgfx::UniformType::Vec4);
        resources.shadow_sampler = bgfx::createUniform("s_shadow_map", bgfx::UniformType::Sampler);
        resources.ibl_params = bgfx::createUniform("u_ibl_params", bgfx::UniformType::Vec4);
        resources.ibl_irradiance_sampler =
            bgfx::createUniform("s_ibl_irradiance", bgfx::UniformType::Sampler);
        resources.ibl_prefilter_sampler =
            bgfx::createUniform("s_ibl_prefilter", bgfx::UniformType::Sampler);
        resources.ibl_lut_sampler = bgfx::createUniform("s_ibl_lut", bgfx::UniformType::Sampler);
        resources.ao_settings = bgfx::createUniform("u_ambient_occlusion", bgfx::UniformType::Vec4);
        resources.ao_sampler =
            bgfx::createUniform("s_ambient_occlusion", bgfx::UniformType::Sampler);
        for (auto uniform : {resources.normal_transform,
                             resources.camera_position,
                             resources.light_direction,
                             resources.light_color,
                             resources.light_count,
                             resources.light_position_range,
                             resources.light_direction_outer,
                             resources.light_color_intensity,
                             resources.light_kind_inner,
                             resources.ambient,
                             resources.tonemap,
                             resources.surface,
                             resources.ambient_sky,
                             resources.ambient_equator,
                             resources.ambient_ground,
                             resources.fog_color,
                             resources.fog_range,
                             resources.shadow_matrices,
                             resources.shadow_rects,
                             resources.shadow_splits,
                             resources.shadow_params,
                             resources.shadow_settings,
                             resources.shadow_depth,
                             resources.shadow_sampler,
                             resources.ibl_params,
                             resources.ibl_irradiance_sampler,
                             resources.ibl_prefilter_sampler,
                             resources.ibl_lut_sampler,
                             resources.ao_settings,
                             resources.ao_sampler})
        {
            if (!bgfx::isValid(uniform))
            {
                release_scene();
                return false;
            }
        }
        constexpr std::uint32_t white = 0xffffffff;
        resources.shadow_fallback = bgfx::createTexture2D(
            1, 1, false, 1, bgfx::TextureFormat::RGBA8,
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT |
                BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT,
            bgfx::copy(&white, sizeof(white)));
        constexpr std::array<std::uint32_t, 6> black_cube{};
        constexpr std::uint64_t ibl_flags =
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;
        resources.ibl_cube_fallback =
            bgfx::createTextureCube(1, false, 1, bgfx::TextureFormat::RGBA8, ibl_flags,
                                    bgfx::copy(black_cube.data(), sizeof(black_cube)));
        resources.ibl_lut_fallback =
            bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, ibl_flags,
                                  bgfx::copy(black_cube.data(), sizeof(std::uint32_t)));
        if (!bgfx::isValid(resources.shadow_fallback) ||
            !bgfx::isValid(resources.ibl_cube_fallback) ||
            !bgfx::isValid(resources.ibl_lut_fallback) ||
            bgfx::getCaps()->limits.maxTextureSamplers <= shadow_sampler_stage)
        {
            release_scene();
            return false;
        }
        scene_id_ = scene->id();
    }
    auto& resources = *scene_resources_;
    if (!resources.prepare_ibl(*scene))
    {
        release_scene();
        return false;
    }
    auto& programs = resources.programs;
    auto& meshes = resources.meshes;
    auto& textures = resources.textures;
    std::set<std::string> needed_programs;
    std::set<std::uint64_t> needed_meshes;
    std::set<const Resource*> needed_textures;
    for (const auto& node : scene->nodes())
    {
        const auto* renderer = node->get_component<MeshRenderer>();
        if (renderer == nullptr || !renderer->enabled() || !renderer->visible())
        {
            continue;
        }
        const auto* data = renderer->mesh_data();
        if (data == nullptr)
        {
            release_scene();
            return false;
        }
        needed_meshes.insert(node->id());
        auto mesh = std::find_if(meshes.begin(), meshes.end(),
                                 [&node](const auto& item) { return item.node_id == node->id(); });
        if (mesh == meshes.end())
        {
            meshes.emplace_back();
            mesh = std::prev(meshes.end());
        }
        if (mesh->instance_id != renderer->mesh_instance_id() ||
            mesh->revision != renderer->mesh_revision())
        {
            mesh->release();
            if (!mesh->create(*renderer))
            {
                release_scene();
                return false;
            }
        }
        if (!mesh->update_deformation(*renderer))
        {
            release_scene();
            return false;
        }
        std::set<std::size_t> slots;
        if (data->submeshes.empty())
        {
            slots.insert(0);
        }
        for (const auto& submesh : data->submeshes)
        {
            slots.insert(submesh.material_slot());
        }
        for (auto slot : slots)
        {
            const auto* material = renderer->material(slot);
            if (material == nullptr)
            {
                release_scene();
                return false;
            }
            const std::string path(material->shader_path());
            const auto* shader = material->shader_data();
            if (shader == nullptr)
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Unknown shader resource: %s", path.c_str());
                release_scene();
                return false;
            }
            if (needed_programs.insert(path).second)
            {
                auto program =
                    std::find_if(programs.begin(), programs.end(),
                                 [&path](const auto& item) { return item.path == path; });
                if (program == programs.end())
                {
                    programs.emplace_back();
                    program = std::prev(programs.end());
                }
                if (program->definition != *shader || !bgfx::isValid(program->program))
                {
                    program->release();
                    if (!program->create(*shader))
                    {
                        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot prepare shader resource: %s",
                                     path.c_str());
                        release_scene();
                        return false;
                    }
                }
            }
            for (const auto& sampler : shader->samplers)
            {
                const auto& source = material->texture(sampler->name);
                if (!source || source->texture() == nullptr)
                {
                    release_scene();
                    return false;
                }
                if (needed_textures.insert(source.get()).second &&
                    std::none_of(textures.begin(), textures.end(),
                                 [&source](const auto& item) { return item.resource == source; }))
                {
                    textures.emplace_back();
                    if (!textures.back().create(source))
                    {
                        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot prepare texture resource: %s",
                                     source->info().path.c_str());
                        release_scene();
                        return false;
                    }
                }
            }
        }
    }
    for (auto item = meshes.begin(); item != meshes.end();)
    {
        if (!needed_meshes.contains(item->node_id))
        {
            item->release();
            item = meshes.erase(item);
        }
        else
        {
            ++item;
        }
    }
    for (auto item = programs.begin(); item != programs.end();)
    {
        if (!needed_programs.contains(item->path))
        {
            item->release();
            item = programs.erase(item);
        }
        else
        {
            ++item;
        }
    }
    for (auto item = textures.begin(); item != textures.end();)
    {
        if (!needed_textures.contains(item->resource.get()))
        {
            item->release();
            item = textures.erase(item);
        }
        else
        {
            ++item;
        }
    }
    return true;
}

void RenderSystem::release_scene() noexcept
{
    if (ui_)
    {
        ui_->release_scene();
    }
    if (fonts_)
    {
        fonts_->release_scene();
    }
    release_draw_resources();
}

void RenderSystem::release_draw_resources() noexcept
{
    if (scene_resources_ != nullptr && renderer_initialized_)
    {
        auto& resources = *scene_resources_;
        for (auto& program : resources.programs)
        {
            program.release();
        }
        for (auto& mesh : resources.meshes)
        {
            mesh.release();
        }
        for (auto& texture : resources.textures)
        {
            texture.release();
        }
        for (auto& texture : resources.ibl_textures)
        {
            texture.release();
        }
        resources.shadow.release();
        resources.ambient_occlusion.release();
        resources.hdr_post_process.release();
        if (bgfx::isValid(resources.shadow_fallback))
        {
            bgfx::destroy(resources.shadow_fallback);
        }
        for (auto fallback : {resources.ibl_cube_fallback, resources.ibl_lut_fallback})
        {
            if (bgfx::isValid(fallback))
            {
                bgfx::destroy(fallback);
            }
        }
        for (auto uniform : {resources.normal_transform,
                             resources.camera_position,
                             resources.light_direction,
                             resources.light_color,
                             resources.light_count,
                             resources.light_position_range,
                             resources.light_direction_outer,
                             resources.light_color_intensity,
                             resources.light_kind_inner,
                             resources.ambient,
                             resources.tonemap,
                             resources.surface,
                             resources.ambient_sky,
                             resources.ambient_equator,
                             resources.ambient_ground,
                             resources.fog_color,
                             resources.fog_range,
                             resources.shadow_matrices,
                             resources.shadow_rects,
                             resources.shadow_splits,
                             resources.shadow_params,
                             resources.shadow_settings,
                             resources.shadow_depth,
                             resources.shadow_sampler,
                             resources.ibl_params,
                             resources.ibl_irradiance_sampler,
                             resources.ibl_prefilter_sampler,
                             resources.ibl_lut_sampler,
                             resources.ao_settings,
                             resources.ao_sampler})
        {
            if (bgfx::isValid(uniform))
            {
                bgfx::destroy(uniform);
            }
        }
    }
    scene_resources_.reset();
    scene_id_ = 0;
}

bool RenderSystem::scene_prepared() const noexcept
{
    return scene_resources_ != nullptr;
}

bool RenderSystem::gpu_ready() const noexcept
{
    return renderer_initialized_;
}

std::uint32_t RenderSystem::frame_number() const noexcept
{
    return frame_number_;
}

bool RenderSystem::set_settings(const RenderSettings& settings)
{
    const auto valid_color = [](const std::array<float, 3>& color)
    {
        return std::all_of(color.begin(), color.end(), [](float value)
                           { return std::isfinite(value) && value >= 0.0F && value <= 1.0F; });
    };
    if (!valid_color(settings.ambient_color) || !valid_color(settings.ambient_sky_color) ||
        !valid_color(settings.ambient_equator_color) ||
        !valid_color(settings.ambient_ground_color) || !valid_color(settings.fog_color) ||
        !std::isfinite(settings.fog_start) || !std::isfinite(settings.fog_end) ||
        settings.fog_start < 0.0F || settings.fog_end <= settings.fog_start ||
        !std::isfinite(settings.fog_density) || settings.fog_density < 0.0F ||
        (settings.fog_mode != FogMode::none && settings.fog_mode != FogMode::linear &&
         settings.fog_mode != FogMode::exponential &&
         settings.fog_mode != FogMode::exponential_squared) ||
        !std::isfinite(settings.ambient_intensity) || settings.ambient_intensity < 0.0F ||
        settings.ambient_intensity > 1000.0F || !std::isfinite(settings.exposure) ||
        settings.exposure <= 0.0F || settings.exposure > 100.0F ||
        (settings.bloom_enabled && !settings.hdr_enabled) ||
        !std::isfinite(settings.bloom_threshold) || settings.bloom_threshold < 0.0F ||
        settings.bloom_threshold > 100.0F || !std::isfinite(settings.bloom_intensity) ||
        settings.bloom_intensity < 0.0F || settings.bloom_intensity > 10.0F ||
        !std::isfinite(settings.bloom_radius) || settings.bloom_radius < 0.25F ||
        settings.bloom_radius > 4.0F || !std::isfinite(settings.ao_radius) ||
        settings.ao_radius <= 0.0F || settings.ao_radius > 100.0F ||
        !std::isfinite(settings.ao_strength) || settings.ao_strength < 0.0F ||
        settings.ao_strength > 2.0F || !std::isfinite(settings.ao_power) ||
        settings.ao_power < 0.1F || settings.ao_power > 8.0F ||
        !std::isfinite(settings.ao_fade_start) || !std::isfinite(settings.ao_fade_end) ||
        settings.ao_fade_start < 0.0F || settings.ao_fade_end <= settings.ao_fade_start ||
        settings.ao_fade_end > 100000.0F || !std::isfinite(settings.ibl_intensity) ||
        settings.ibl_intensity < 0.0F || settings.ibl_intensity > 1000.0F ||
        !std::isfinite(settings.shadow_bias) || settings.shadow_bias < 0.0F ||
        settings.shadow_bias > 0.1F || settings.shadow_resolution < 128 ||
        settings.shadow_resolution > 8192 ||
        (settings.shadow_resolution & (settings.shadow_resolution - 1)) != 0 ||
        (settings.shadow_cascades != 1 && settings.shadow_cascades != 2 &&
         settings.shadow_cascades != 4) ||
        !std::isfinite(settings.shadow_distance) || settings.shadow_distance <= 0.0F ||
        settings.shadow_distance > 100000.0F || !std::isfinite(settings.shadow_split_lambda) ||
        settings.shadow_split_lambda < 0.0F || settings.shadow_split_lambda > 1.0F ||
        !std::isfinite(settings.shadow_cascade_blend) || settings.shadow_cascade_blend < 0.0F ||
        settings.shadow_cascade_blend > 0.3F ||
        (renderer_initialized_ && settings.directional_shadows &&
         settings.shadow_resolution > bgfx::getCaps()->limits.maxTextureSize))
    {
        return false;
    }
    Scene* scene = world_ != nullptr ? world_->scene() : nullptr;
    if (scene == nullptr)
    {
        idle_settings_ = settings;
        return true;
    }
    SceneRenderComponents components;
    if (!scene_render_components(*scene, components))
    {
        return false;
    }

    const EnvironmentSettings before_environment = components.environment != nullptr
                                                       ? components.environment->settings()
                                                       : EnvironmentSettings{};
    auto after_environment = before_environment;
    update_environment(settings, after_environment);
    const PostProcessSettings before_post_process = components.post_process != nullptr
                                                        ? components.post_process->settings()
                                                        : PostProcessSettings{};
    auto after_post_process = before_post_process;
    after_post_process.exposure = settings.exposure;
    after_post_process.filmic_tonemap = settings.filmic_tonemap;
    after_post_process.hdr_enabled = settings.hdr_enabled;
    after_post_process.bloom_enabled = settings.bloom_enabled;
    after_post_process.bloom_threshold = settings.bloom_threshold;
    after_post_process.bloom_intensity = settings.bloom_intensity;
    after_post_process.bloom_radius = settings.bloom_radius;
    after_post_process.ao_enabled = settings.ao_enabled;
    after_post_process.ao_radius = settings.ao_radius;
    after_post_process.ao_strength = settings.ao_strength;
    after_post_process.ao_power = settings.ao_power;
    after_post_process.ao_fade_start = settings.ao_fade_start;
    after_post_process.ao_fade_end = settings.ao_fade_end;
    const LightSettings before_light =
        components.shadow_light != nullptr ? components.shadow_light->settings() : LightSettings{};
    auto after_light = before_light;
    after_light.cast_shadows = settings.directional_shadows;
    after_light.shadow_resolution = settings.shadow_resolution;
    after_light.shadow_bias = settings.shadow_bias;
    after_light.shadow_cascades = settings.shadow_cascades;
    after_light.shadow_distance = settings.shadow_distance;
    after_light.shadow_split_lambda = settings.shadow_split_lambda;
    after_light.shadow_cascade_blend = settings.shadow_cascade_blend;

    const bool environment_changed = before_environment != after_environment;
    const bool post_process_changed = before_post_process != after_post_process;
    const bool light_changed = before_light != after_light;
    if ((environment_changed && components.environment == nullptr) ||
        (post_process_changed && components.post_process == nullptr) ||
        (light_changed && components.shadow_light == nullptr))
    {
        return false;
    }
    if (environment_changed && !components.environment->set_settings(after_environment))
    {
        return false;
    }
    if (post_process_changed && !components.post_process->set_settings(after_post_process))
    {
        if (environment_changed && !components.environment->set_settings(before_environment))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot restore previous environment settings");
        }
        return false;
    }
    if (light_changed && !components.shadow_light->set_settings(after_light))
    {
        if (post_process_changed && !components.post_process->set_settings(before_post_process))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot restore previous post-process settings");
        }
        if (environment_changed && !components.environment->set_settings(before_environment))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot restore previous environment settings");
        }
        return false;
    }
    return true;
}

bool RenderSystem::resolve_settings(RenderSettings& settings) const noexcept
{
    settings = {};
    Scene* scene = world_ != nullptr ? world_->scene() : nullptr;
    if (scene == nullptr)
    {
        settings = idle_settings_;
        return true;
    }
    SceneRenderComponents components;
    if (!scene_render_components(*scene, components))
    {
        return false;
    }
    settings = render_settings(components);
    if (renderer_initialized_ && settings.directional_shadows &&
        settings.shadow_resolution > bgfx::getCaps()->limits.maxTextureSize)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Directional shadow resolution exceeds device limit");
        settings = {};
        return false;
    }
    return true;
}

RenderSettings RenderSystem::settings() const noexcept
{
    RenderSettings current;
    if (!resolve_settings(current))
    {
        return {};
    }
    return current;
}

void RenderSystem::set_stats_visible(bool visible)
{
    stats_visible_ = visible;
    if (renderer_initialized_)
    {
        bgfx::setDebug(stats_visible_ ? BGFX_DEBUG_STATS : BGFX_DEBUG_NONE);
    }
}

bool RenderSystem::stats_visible() const noexcept
{
    return stats_visible_;
}

bool RenderSystem::request_screenshot(std::string_view utf8_png_path)
{
    if (!renderer_initialized_ || callbacks_ == nullptr || !SDL_IsMainThread() ||
        utf8_png_path.empty() || !utf8_png_path.ends_with(".png") ||
        utf8_png_path.find('\0') != std::string_view::npos)
    {
        return false;
    }
    const auto path = PathUtils::from_utf8(utf8_png_path);
    std::error_code error;
    if (!path.is_absolute() || !std::filesystem::is_directory(path.parent_path(), error) || error)
    {
        return false;
    }
    std::lock_guard lock(callbacks_->mutex);
    if (callbacks_->status.load() == ScreenshotStatus::pending)
    {
        return false;
    }
    callbacks_->pending_path = utf8_png_path;
    callbacks_->status.store(ScreenshotStatus::pending);
    bgfx::requestScreenShot(BGFX_INVALID_HANDLE, callbacks_->pending_path.c_str());
    return true;
}

ScreenshotStatus RenderSystem::screenshot_status() const noexcept
{
    return callbacks_ != nullptr ? callbacks_->status.load() : ScreenshotStatus::idle;
}

std::uint32_t RenderSystem::gpu_resource_count() const noexcept
{
    if (scene_resources_ == nullptr)
    {
        return 0;
    }
    const SceneGpuResources& resources = *scene_resources_;
    std::uint32_t count =
        static_cast<std::uint32_t>(bgfx::isValid(resources.normal_transform)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.camera_position)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.light_direction)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.light_color)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.light_count)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.light_position_range)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.light_direction_outer)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.light_color_intensity)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.light_kind_inner)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.ambient)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.ambient_sky)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.ambient_equator)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.ambient_ground)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.fog_color)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.fog_range)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.tonemap)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.surface)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.shadow_matrices)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.shadow_rects)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.shadow_splits)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.shadow_params)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.shadow_settings)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.shadow_depth)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.shadow_sampler)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.shadow_fallback)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.ibl_params)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.ibl_irradiance_sampler)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.ibl_prefilter_sampler)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.ibl_lut_sampler)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.ibl_cube_fallback)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.ibl_lut_fallback)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.ao_settings)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.ao_sampler)) +
        resources.shadow.resource_count() + resources.ambient_occlusion.resource_count() +
        resources.hdr_post_process.resource_count();
    for (const auto& texture : resources.ibl_textures)
    {
        count += static_cast<std::uint32_t>(bgfx::isValid(texture.handle));
    }
    for (const auto& mesh : resources.meshes)
    {
        count += static_cast<std::uint32_t>(bgfx::isValid(mesh.vertices)) +
                 static_cast<std::uint32_t>(bgfx::isValid(mesh.deformed_vertices)) +
                 static_cast<std::uint32_t>(bgfx::isValid(mesh.indices));
    }
    for (const auto& texture : resources.textures)
    {
        count += static_cast<std::uint32_t>(bgfx::isValid(texture.handle));
    }
    for (const auto& program : resources.programs)
    {
        count += static_cast<std::uint32_t>(bgfx::isValid(program.vertex_shader)) +
                 static_cast<std::uint32_t>(bgfx::isValid(program.fragment_shader)) +
                 static_cast<std::uint32_t>(bgfx::isValid(program.program));
        for (const auto& uniform : program.uniforms)
        {
            count += static_cast<std::uint32_t>(bgfx::isValid(uniform.handle));
        }
        for (const auto& sampler : program.samplers)
        {
            count += static_cast<std::uint32_t>(bgfx::isValid(sampler.handle));
        }
    }
    return count;
}

bool RenderSystem::draw_scene(const Camera* view_camera, const RenderSettings& rendering)
{
    if (!prepare_scene(view_camera))
    {
        return false;
    }
    const Scene* scene = world_ != nullptr ? world_->scene() : nullptr;
    if (scene == nullptr || scene_resources_ == nullptr || !drawable_scene(*scene, view_camera))
    {
        return true;
    }
    const Camera& camera = view_camera != nullptr ? *view_camera : *scene->active_camera();
    float camera_world[16]{};
    const auto* camera_transform = camera.node().get_component<Transform>();
    if (camera_transform == nullptr || !camera_transform->world_matrix(camera_world))
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Camera world matrix is invalid");
        return false;
    }
    float view[16]{};
    float projection[16]{};
    const CameraSettings& settings = camera.settings();
    const float aspect = static_cast<float>(width_) / height_;
    if (settings.projection == schema::Projection::orthographic)
    {
        const float horizontal = settings.orthographic_size * aspect;
        bx::mtxOrtho(projection, -horizontal, horizontal, -settings.orthographic_size,
                     settings.orthographic_size, settings.near_clip, settings.far_clip, 0.0F,
                     bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
    }
    else
    {
        bx::mtxProj(projection, settings.fov_degrees, aspect, settings.near_clip, settings.far_clip,
                    bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
    }
    if (!camera_view(camera_world, view) || !finite_matrix(projection))
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Camera view or projection matrix is invalid");
        return false;
    }
    bgfx::setViewTransform(main_view, view, projection);
    const float camera_position[] = {camera_world[12], camera_world[13], camera_world[14], 1.0F};
    std::array<float, 4> light_direction{0.0F, 0.0F, -1.0F, 0.0F};
    std::array<float, 4> light_color{};
    std::array<std::array<float, 4>, maximum_lights> light_position_range{};
    std::array<std::array<float, 4>, maximum_lights> light_direction_outer{};
    std::array<std::array<float, 4>, maximum_lights> light_color_intensity{};
    std::array<std::array<float, 4>, maximum_lights> light_kind_inner{};
    std::size_t light_count = 0;
    std::size_t shadow_light = maximum_lights;
    for (const auto& node : scene->nodes())
    {
        const Light* light = node->get_component<Light>();
        if (light == nullptr || !light->enabled())
        {
            continue;
        }
        if (light_count == maximum_lights)
        {
            return false;
        }
        float world[16]{};
        const auto* transform = node->get_component<Transform>();
        if (transform == nullptr || !transform->world_matrix(world))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Light world matrix is invalid");
            return false;
        }
        const double length =
            std::hypot(static_cast<double>(world[8]), static_cast<double>(world[9]),
                       static_cast<double>(world[10]));
        if (!(length > 0.0))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Light direction is invalid");
            return false;
        }
        constexpr float degrees_to_radians = 0.01745329251994329577F;
        const LightSettings& lighting = light->settings();
        if (lighting.kind == schema::LightKind::directional && lighting.cast_shadows)
        {
            shadow_light = light_count;
        }
        light_position_range[light_count] = {world[12], world[13], world[14], lighting.range};
        light_direction_outer[light_count] = {static_cast<float>(-world[8] / length),
                                              static_cast<float>(-world[9] / length),
                                              static_cast<float>(-world[10] / length),
                                              std::cos(lighting.outer_angle * degrees_to_radians)};
        light_color_intensity[light_count] = {lighting.color.r(), lighting.color.g(),
                                              lighting.color.b(), lighting.intensity};
        light_kind_inner[light_count] = {static_cast<float>(lighting.kind),
                                         std::cos(lighting.inner_angle * degrees_to_radians), 0.0F,
                                         0.0F};
        if (light_count == 0)
        {
            light_direction = light_direction_outer[0];
            light_direction[3] = 0.0F;
            light_color = {lighting.color.r() * lighting.intensity,
                           lighting.color.g() * lighting.intensity,
                           lighting.color.b() * lighting.intensity, 0.0F};
        }
        ++light_count;
    }
    const float count_value[] = {static_cast<float>(light_count), 0.0F, 0.0F, 0.0F};
    SceneGpuResources& resources = *scene_resources_;
    ShadowFrame shadow_frame;
    for (auto& matrix : shadow_frame.matrices)
    {
        bx::mtxIdentity(matrix.data());
    }
    const bool shadow_enabled =
        rendering.directional_shadows && shadow_light < light_count &&
        std::min(settings.far_clip, rendering.shadow_distance) > settings.near_clip;
    if (shadow_enabled)
    {
        if (!resources.shadow.prepare(rendering.shadow_resolution) ||
            !prepare_shadow_frame(settings, rendering, aspect, camera_world, view,
                                  light_direction_outer[shadow_light], shadow_frame) ||
            !resources.draw_shadow(*scene, rendering, shadow_frame))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot render directional shadow cascades");
            return false;
        }
    }
    else
    {
        resources.shadow.release();
    }
    const float shadow_settings[] = {rendering.shadow_bias, 1.0F / rendering.shadow_resolution,
                                     static_cast<float>(shadow_light),
                                     shadow_enabled ? 1.0F : 0.0F};
    const float ambient[] = {rendering.ambient_color[0], rendering.ambient_color[1],
                             rendering.ambient_color[2], rendering.ambient_intensity};
    const float ambient_sky[] = {rendering.ambient_sky_color[0], rendering.ambient_sky_color[1],
                                 rendering.ambient_sky_color[2],
                                 rendering.ambient_tricolor ? 1.0F : 0.0F};
    const float ambient_equator[] = {rendering.ambient_equator_color[0],
                                     rendering.ambient_equator_color[1],
                                     rendering.ambient_equator_color[2], 0.0F};
    const float ambient_ground[] = {rendering.ambient_ground_color[0],
                                    rendering.ambient_ground_color[1],
                                    rendering.ambient_ground_color[2], 0.0F};
    const float fog_color[] = {rendering.fog_color[0], rendering.fog_color[1],
                               rendering.fog_color[2], static_cast<float>(rendering.fog_mode)};
    const float fog_range[] = {rendering.fog_start, rendering.fog_end, rendering.fog_density,
                               settings.near_clip};
    const float tonemap[] = {rendering.exposure, rendering.filmic_tonemap ? 1.0F : 0.0F,
                             rendering.hdr_enabled ? 1.0F : 0.0F, 0.0F};
    const bool ibl_ready =
        rendering.ibl_enabled &&
        std::all_of(resources.ibl_textures.begin(), resources.ibl_textures.end(),
                    [](const auto& texture) { return bgfx::isValid(texture.handle); });
    const float ibl_params[] = {
        ibl_ready ? 1.0F : 0.0F, rendering.ibl_intensity,
        ibl_ready ? static_cast<float>(resources.ibl_textures[1].resource->texture()->mip_count - 1)
                  : 0.0F,
        0.0F};
    // 复用完整材质保留顶点变形、裁剪透明与自定义天空的深度语义。
    const auto draw_meshes = [&](std::uint16_t view_id, bool depth_only)
    {
        for (const auto& node : scene->nodes())
        {
            const MeshRenderer* renderer = node->get_component<MeshRenderer>();
            if (renderer == nullptr || !renderer->enabled() || !renderer->visible())
            {
                continue;
            }
            float model[16]{};
            const auto* transform = node->get_component<Transform>();
            if (transform == nullptr || !transform->world_matrix(model))
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Mesh world matrix is invalid");
                return false;
            }
            float normal_transform[16]{};
            if (!normal_matrix(model, normal_transform))
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Mesh normal transform is invalid");
                return false;
            }
            const auto mesh =
                std::find_if(resources.meshes.begin(), resources.meshes.end(),
                             [&node](const auto& item) { return item.node_id == node->id(); });
            const auto* data = renderer->mesh_data();
            if (mesh == resources.meshes.end() || data == nullptr)
            {
                return false;
            }
            const auto submit = [&](std::uint32_t first, std::uint32_t count, std::size_t slot)
            {
                const Material* material = renderer->material(slot);
                if (material == nullptr)
                {
                    return false;
                }
                auto program = std::find_if(resources.programs.begin(), resources.programs.end(),
                                            [material](const auto& item)
                                            { return item.path == material->shader_path(); });
                if (program == resources.programs.end() ||
                    !program->bind_material(*material, resources.textures))
                {
                    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                                 "Material parameters do not match its shader");
                    return false;
                }
                bgfx::setUniform(resources.normal_transform, normal_transform);
                bgfx::setUniform(resources.camera_position, camera_position);
                bgfx::setUniform(resources.light_direction, light_direction.data());
                bgfx::setUniform(resources.light_color, light_color.data());
                bgfx::setUniform(resources.light_count, count_value);
                bgfx::setUniform(resources.light_position_range, light_position_range.data(),
                                 maximum_lights);
                bgfx::setUniform(resources.light_direction_outer, light_direction_outer.data(),
                                 maximum_lights);
                bgfx::setUniform(resources.light_color_intensity, light_color_intensity.data(),
                                 maximum_lights);
                bgfx::setUniform(resources.light_kind_inner, light_kind_inner.data(),
                                 maximum_lights);
                bgfx::setUniform(resources.ambient, ambient);
                bgfx::setUniform(resources.ambient_sky, ambient_sky);
                bgfx::setUniform(resources.ambient_equator, ambient_equator);
                bgfx::setUniform(resources.ambient_ground, ambient_ground);
                bgfx::setUniform(resources.fog_color, fog_color);
                bgfx::setUniform(resources.fog_range, fog_range);
                bgfx::setUniform(resources.tonemap, tonemap);
                bgfx::setUniform(resources.ibl_params, ibl_params);
                const bool ao_enabled = rendering.ao_enabled && !depth_only;
                const float ao_settings[] = {1.0F / width_, 1.0F / height_,
                                             ao_enabled ? 1.0F : 0.0F, 0.0F};
                bgfx::setUniform(resources.ao_settings, ao_settings);
                bgfx::setTexture(ambient_occlusion_stage, resources.ao_sampler,
                                 ao_enabled ? resources.ambient_occlusion.texture()
                                            : resources.shadow_fallback);
                bgfx::setTexture(ibl_irradiance_stage, resources.ibl_irradiance_sampler,
                                 ibl_ready ? resources.ibl_textures[0].handle
                                           : resources.ibl_cube_fallback);
                bgfx::setTexture(ibl_prefilter_stage, resources.ibl_prefilter_sampler,
                                 ibl_ready ? resources.ibl_textures[1].handle
                                           : resources.ibl_cube_fallback);
                bgfx::setTexture(ibl_lut_stage, resources.ibl_lut_sampler,
                                 ibl_ready ? resources.ibl_textures[2].handle
                                           : resources.ibl_lut_fallback);
                const float surface[] = {material->cull_mode() != schema::CullMode::back ? 1.0F
                                                                                         : 0.0F,
                                         0.0F, 0.0F, 0.0F};
                bgfx::setUniform(resources.surface, surface);
                bgfx::setUniform(resources.shadow_matrices, shadow_frame.matrices.data(),
                                 maximum_shadow_cascades);
                bgfx::setUniform(resources.shadow_rects, shadow_frame.rectangles.data(),
                                 maximum_shadow_cascades);
                bgfx::setUniform(resources.shadow_splits, shadow_frame.splits.data());
                bgfx::setUniform(resources.shadow_params, shadow_frame.parameters.data());
                bgfx::setUniform(resources.shadow_settings, shadow_settings);
                bgfx::setTexture(shadow_sampler_stage, resources.shadow_sampler,
                                 shadow_enabled ? resources.shadow.color
                                                : resources.shadow_fallback);
                bgfx::setTransform(model);
                mesh->bind_vertices();
                bgfx::setIndexBuffer(mesh->indices, first, count);
                const auto state = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                                   cull_state(material->cull_mode(), model);
                bgfx::setState(depth_only ? state
                                          : state | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                                                BGFX_STATE_MSAA);
                bgfx::submit(view_id, program->program);
                return true;
            };
            if (data->submeshes.empty())
            {
                if (!submit(0, static_cast<std::uint32_t>(data->indices.size()), 0))
                {
                    return false;
                }
            }
            for (const auto& submesh : data->submeshes)
            {
                if (!submit(submesh.first_index(), submesh.index_count(), submesh.material_slot()))
                {
                    return false;
                }
            }
        }
        return true;
    };
    if (rendering.ao_enabled)
    {
        if (!resources.ambient_occlusion.prepare(static_cast<std::uint16_t>(width_),
                                                 static_cast<std::uint16_t>(height_)))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot prepare ambient occlusion resources");
            return false;
        }
        resources.ambient_occlusion.begin_depth(view, projection);
        if (!draw_meshes(ambient_occlusion_depth_view, true) ||
            !resources.ambient_occlusion.compute(settings, rendering, projection))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot render ambient occlusion");
            return false;
        }
    }
    else
    {
        resources.ambient_occlusion.release();
    }
    if (rendering.hdr_enabled)
    {
        if (!resources.hdr_post_process.prepare(static_cast<std::uint16_t>(width_),
                                                static_cast<std::uint16_t>(height_),
                                                rendering.bloom_enabled))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot prepare HDR post-processing");
            return false;
        }
        resources.hdr_post_process.begin_scene(rendering.clear_color);
    }
    else
    {
        resources.hdr_post_process.release();
    }
    if (!draw_meshes(main_view, false) ||
        (fonts_ != nullptr && !fonts_->render_scene(*scene, main_view, rendering.hdr_enabled)))
    {
        return false;
    }
    return !rendering.hdr_enabled || resources.hdr_post_process.resolve(rendering);
}

bool RenderSystem::render(const Camera* camera)
{
    if (!is_initialized() || !windows_.can_render())
    {
        return false;
    }

#if defined(SDL_PLATFORM_ANDROID)
    if (!renderer_initialized_)
    {
        if (!initialize_renderer() || !renderer_initialized_)
        {
            return false;
        }
    }
#endif

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    SDL_Window* window = windows_.native_window();
    bgfx::PlatformData data{};
    if (!window_size(window, width, height) || !platform_data(window, metal_view_, data))
    {
        return false;
    }
    if (data.ndt != native_display_)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Native display changed; renderer restart required");
        return false;
    }

    const bool surface_changed = data.nwh != native_window_;
    if (surface_changed)
    {
#if defined(SDL_PLATFORM_ANDROID)
        shutdown_renderer();
        if (!initialize_renderer() || !renderer_initialized_)
        {
            return false;
        }
#else
        // bgfx 允许初始化后替换原生窗口句柄；显示连接和图形设备保持不变。
        bgfx::setPlatformData(data);
        native_window_ = data.nwh;
#endif
    }
    if (surface_changed || width != width_ || height != height_)
    {
        bgfx::reset(width, height, reset_flags);
        width_ = width;
        height_ = height;
    }

    RenderSettings current_settings;
    if (!resolve_settings(current_settings))
    {
        return false;
    }
    bgfx::setViewFrameBuffer(main_view, BGFX_INVALID_HANDLE);
    bgfx::setViewClear(main_view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, current_settings.clear_color,
                       1.0F, 0);
    bgfx::setViewRect(main_view, 0, 0, static_cast<std::uint16_t>(width_),
                      static_cast<std::uint16_t>(height_));
    bgfx::touch(main_view);
    const bool drawn =
        draw_scene(camera, current_settings) && (ui_ == nullptr || ui_->render(width_, height_));
    frame_number_ = bgfx::frame();
    return drawn;
}

void RenderSystem::on_shutdown() noexcept
{
    if (watching_events_)
    {
#if defined(SDL_PLATFORM_ANDROID)
        SDL_RemoveEventWatch(watch_surface_lifecycle, this);
#endif
        watching_events_ = false;
    }
    shutdown_renderer();
}

void RenderSystem::shutdown_renderer() noexcept
{
    release_scene();
    if (renderer_initialized_)
    {
        bgfx::shutdown();
        renderer_initialized_ = false;
        frame_number_ = 0;
        active_renderer = nullptr;
    }
    if (callbacks_ != nullptr && callbacks_->status.load() == ScreenshotStatus::pending)
    {
        callbacks_->status.store(ScreenshotStatus::failed);
    }
#if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
    if (metal_view_ != nullptr)
    {
        SDL_Metal_DestroyView(metal_view_);
        metal_view_ = nullptr;
    }
#endif
#if defined(SDL_PLATFORM_ANDROID)
    if (native_window_ != nullptr)
    {
        ANativeWindow_release(static_cast<ANativeWindow*>(native_window_));
    }
#endif
    native_window_ = nullptr;
    native_display_ = nullptr;
    width_ = 0;
    height_ = 0;
    if (surface_registered_)
    {
#if defined(SDL_PLATFORM_ANDROID)
        android::end_rendering();
#endif
        surface_registered_ = false;
    }
}

} // namespace engine
