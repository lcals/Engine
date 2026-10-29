#include "runtime/render/render_system.hpp"

#include "runtime/resource/resource.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"
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
#include <string>
#include <vector>

namespace engine
{
namespace
{

// SDL 窗口和 bgfx 生命周期都由主线程驱动；bgfx 只支持一个活动实例。
RenderSystem* active_renderer = nullptr;
constexpr std::uint16_t shadow_view = 0;
constexpr std::uint16_t main_view = 1;
constexpr std::uint8_t shadow_sampler_stage = 15;
constexpr std::uint32_t reset_flags = BGFX_RESET_VSYNC;
constexpr std::size_t maximum_lights = 8;

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

bool drawable_scene(const Scene& scene)
{
    const Camera* camera = scene.active_camera();
    if (camera == nullptr || !camera->enabled())
    {
        return false;
    }
    return std::any_of(scene.nodes().begin(), scene.nodes().end(),
                       [](const std::unique_ptr<Node>& node)
                       {
                           const MeshRenderer* mesh = node->get_component<MeshRenderer>();
                           return mesh != nullptr && mesh->enabled();
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
                        name == "u_shadow_transform" || name == "u_shadow_settings" ||
                        name == "u_shadow_depth" || name == "s_shadow_map" || light_array)
                    {
                        const auto type =
                            name == "u_normal_transform" || name == "u_shadow_transform"
                                ? bgfx::UniformType::Mat4
                            : name == "s_shadow_map" ? bgfx::UniformType::Sampler
                                                     : bgfx::UniformType::Vec4;
                        const auto expected_count = light_array ? maximum_lights : 1;
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
                    item->stage == shadow_sampler_stage)
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
                if (!resource || texture == textures.end())
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
        std::uint64_t node_id = 0;
        std::uint64_t revision = 0;
        bgfx::VertexBufferHandle vertices = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle indices = BGFX_INVALID_HANDLE;
        std::array<float, 3> minimum{};
        std::array<float, 3> maximum{};

        bool create(const MeshRenderer& renderer)
        {
            const auto* data = renderer.mesh_data();
            std::vector<schema::Vec4> tangents;
            if (data == nullptr || !MeshUtils::build_tangents(*data, tangents))
            {
                return false;
            }
            node_id = renderer.node().id();
            revision = renderer.mesh_revision();
            struct Vertex
            {
                float position[3];
                float normal[3];
                float tangent[4];
                float texcoord[2];
                float color[4];
            };
            static_assert(sizeof(Vertex) == 16 * sizeof(float));
            std::vector<Vertex> packed(data->vertices.size());
            minimum.fill(std::numeric_limits<float>::max());
            maximum.fill(std::numeric_limits<float>::lowest());
            for (std::size_t index = 0; index < packed.size(); ++index)
            {
                const auto& source = data->vertices[index];
                const auto& tangent = tangents[index];
                const auto uv = data->texcoords.empty() ? schema::Vec2{} : data->texcoords[index];
                const auto color =
                    data->colors.empty() ? schema::Color{1, 1, 1, 1} : data->colors[index];
                const double normal_length =
                    std::hypot(static_cast<double>(source.nx()), static_cast<double>(source.ny()),
                               static_cast<double>(source.nz()));
                packed[index] = {{source.x(), source.y(), source.z()},
                                 {static_cast<float>(source.nx() / normal_length),
                                  static_cast<float>(source.ny() / normal_length),
                                  static_cast<float>(source.nz() / normal_length)},
                                 {static_cast<float>(tangent.x()), static_cast<float>(tangent.y()),
                                  static_cast<float>(tangent.z()), static_cast<float>(tangent.w())},
                                 {static_cast<float>(uv.x()), static_cast<float>(uv.y())},
                                 {color.r(), color.g(), color.b(), color.a()}};
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    minimum[axis] = std::min(minimum[axis], packed[index].position[axis]);
                    maximum[axis] = std::max(maximum[axis], packed[index].position[axis]);
                }
            }
            const auto vertex_bytes = packed.size() * sizeof(Vertex);
            const auto index_bytes = data->indices.size() * sizeof(std::uint32_t);
            bgfx::VertexLayout layout;
            layout.begin()
                .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
                .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
                .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float)
                .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
                .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
                .end();
            vertices = bgfx::createVertexBuffer(
                bgfx::copy(packed.data(), static_cast<std::uint32_t>(vertex_bytes)), layout);
            indices = bgfx::createIndexBuffer(
                bgfx::copy(data->indices.data(), static_cast<std::uint32_t>(index_bytes)),
                BGFX_BUFFER_INDEX32);
            return bgfx::isValid(vertices) && bgfx::isValid(indices);
        }

        void release() noexcept
        {
            if (bgfx::isValid(vertices))
            {
                bgfx::destroy(vertices);
            }
            if (bgfx::isValid(indices))
            {
                bgfx::destroy(indices);
            }
            vertices = BGFX_INVALID_HANDLE;
            indices = BGFX_INVALID_HANDLE;
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
        std::uint16_t resolution = 0;

        void release() noexcept
        {
            if (bgfx::isValid(framebuffer))
            {
                bgfx::setViewFrameBuffer(shadow_view, BGFX_INVALID_HANDLE);
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
            if (!bgfx::isValid(framebuffer) || !bgfx::isValid(program))
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
                   static_cast<std::uint32_t>(bgfx::isValid(program));
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
    bgfx::UniformHandle tonemap = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadow_transform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadow_settings = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadow_depth = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadow_sampler = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shadow_fallback = BGFX_INVALID_HANDLE;
    Shadow shadow;
    std::vector<Program> programs;

    bool draw_shadow(const Scene& scene, const std::array<float, 4>& direction, float* matrix)
    {
        // bgfx 15-shadowmaps-simple 的投影与 crop；边界由当前组件几何计算。
        float view[16]{};
        const bx::Vec3 up = std::abs(direction[1]) > 0.95F ? bx::Vec3{0.0F, 0.0F, 1.0F}
                                                           : bx::Vec3{0.0F, 1.0F, 0.0F};
        bx::mtxLookAt(view, {0.0F, 0.0F, 0.0F}, {direction[0], direction[1], direction[2]}, up,
                      bx::Handedness::Right);
        std::array<float, 3> minimum{};
        std::array<float, 3> maximum{};
        minimum.fill(std::numeric_limits<float>::max());
        maximum.fill(std::numeric_limits<float>::lowest());
        for (const auto& mesh : meshes)
        {
            const Node* node = scene.find_node(mesh.node_id);
            const auto* transform = node != nullptr ? node->get_component<Transform>() : nullptr;
            float model[16]{};
            float combined[16]{};
            if (transform == nullptr || !transform->world_matrix(model))
            {
                return false;
            }
            bx::mtxMul(combined, model, view);
            for (std::uint32_t corner = 0; corner < 8; ++corner)
            {
                const float coordinates[] = {(corner & 1) ? mesh.maximum[0] : mesh.minimum[0],
                                             (corner & 2) ? mesh.maximum[1] : mesh.minimum[1],
                                             (corner & 4) ? mesh.maximum[2] : mesh.minimum[2]};
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    const float value = combined[axis] * coordinates[0] +
                                        combined[axis + 4] * coordinates[1] +
                                        combined[axis + 8] * coordinates[2] + combined[axis + 12];
                    minimum[axis] = std::min(minimum[axis], value);
                    maximum[axis] = std::max(maximum[axis], value);
                }
            }
        }
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            const float margin = std::max(0.1F, (maximum[axis] - minimum[axis]) * 0.01F);
            minimum[axis] -= margin;
            maximum[axis] += margin;
        }
        const auto* caps = bgfx::getCaps();
        float projection[16]{};
        bx::mtxOrtho(projection, minimum[0], maximum[0], minimum[1], maximum[1], -maximum[2],
                     -minimum[2], 0.0F, caps->homogeneousDepth, bx::Handedness::Right);
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
        float projected[16]{};
        bx::mtxMul(projected, projection, crop);
        bx::mtxMul(matrix, view, projected);
        if (!finite_matrix(matrix))
        {
            return false;
        }
        bgfx::setViewFrameBuffer(shadow_view, shadow.framebuffer);
        bgfx::setViewRect(shadow_view, 0, 0, shadow.resolution, shadow.resolution);
        bgfx::setViewClear(shadow_view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0xffffffff, 1.0F, 0);
        bgfx::setViewTransform(shadow_view, view, projection);
        bgfx::touch(shadow_view);
        const float depth_scale[] = {caps->homogeneousDepth ? 0.5F : 1.0F,
                                     caps->homogeneousDepth ? 0.5F : 0.0F, 0.0F, 0.0F};
        for (const auto& mesh : meshes)
        {
            const Node* node = scene.find_node(mesh.node_id);
            const auto* transform = node != nullptr ? node->get_component<Transform>() : nullptr;
            float model[16]{};
            if (transform == nullptr || !transform->world_matrix(model))
            {
                return false;
            }
            bgfx::setTransform(model);
            bgfx::setUniform(shadow_depth, depth_scale);
            bgfx::setVertexBuffer(0, mesh.vertices);
            bgfx::setIndexBuffer(mesh.indices);
            const auto cull =
                linear_determinant(model) < 0.0 ? BGFX_STATE_CULL_CCW : BGFX_STATE_CULL_CW;
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                           BGFX_STATE_DEPTH_TEST_LESS | cull);
            bgfx::submit(shadow_view, shadow.program);
        }
        return true;
    }
};

RenderSystem::RenderSystem(WindowSystem& windows, WorldSystem* world) noexcept
    : windows_(windows), world_(world)
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
    bgfx::setViewName(shadow_view, "Directional shadow");
    bgfx::setViewClear(main_view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, settings_.clear_color, 1.0F,
                       0);
    bgfx::setDebug(BGFX_DEBUG_NONE);
    SDL_Log("Renderer initialized: %s (%u x %u)", bgfx::getRendererName(bgfx::getRendererType()),
            width_, height_);
    return true;
}

bool RenderSystem::prepare_scene()
{
    if (!is_initialized())
    {
        return false;
    }
    const Scene* scene = world_ != nullptr ? world_->scene() : nullptr;
    if (scene == nullptr || scene->id() != scene_id_)
    {
        release_scene();
    }
    if (scene == nullptr || !drawable_scene(*scene))
    {
        release_scene();
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
        resources.tonemap = bgfx::createUniform("u_tonemap", bgfx::UniformType::Vec4);
        resources.shadow_transform =
            bgfx::createUniform("u_shadow_transform", bgfx::UniformType::Mat4);
        resources.shadow_settings =
            bgfx::createUniform("u_shadow_settings", bgfx::UniformType::Vec4);
        resources.shadow_depth = bgfx::createUniform("u_shadow_depth", bgfx::UniformType::Vec4);
        resources.shadow_sampler = bgfx::createUniform("s_shadow_map", bgfx::UniformType::Sampler);
        for (auto uniform :
             {resources.normal_transform, resources.camera_position, resources.light_direction,
              resources.light_color, resources.light_count, resources.light_position_range,
              resources.light_direction_outer, resources.light_color_intensity,
              resources.light_kind_inner, resources.ambient, resources.tonemap,
              resources.shadow_transform, resources.shadow_settings, resources.shadow_depth,
              resources.shadow_sampler})
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
        if (!bgfx::isValid(resources.shadow_fallback) ||
            bgfx::getCaps()->limits.maxTextureSamplers <= shadow_sampler_stage)
        {
            release_scene();
            return false;
        }
        scene_id_ = scene->id();
    }
    auto& resources = *scene_resources_;
    auto& programs = resources.programs;
    auto& meshes = resources.meshes;
    auto& textures = resources.textures;
    std::set<std::string> needed_programs;
    std::set<std::uint64_t> needed_meshes;
    std::set<const Resource*> needed_textures;
    for (const auto& node : scene->nodes())
    {
        const auto* renderer = node->get_component<MeshRenderer>();
        if (renderer == nullptr || !renderer->enabled())
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
        if (mesh->revision != renderer->mesh_revision())
        {
            mesh->release();
            if (!mesh->create(*renderer))
            {
                release_scene();
                return false;
            }
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
        resources.shadow.release();
        if (bgfx::isValid(resources.shadow_fallback))
        {
            bgfx::destroy(resources.shadow_fallback);
        }
        for (auto uniform :
             {resources.normal_transform, resources.camera_position, resources.light_direction,
              resources.light_color, resources.light_count, resources.light_position_range,
              resources.light_direction_outer, resources.light_color_intensity,
              resources.light_kind_inner, resources.ambient, resources.tonemap,
              resources.shadow_transform, resources.shadow_settings, resources.shadow_depth,
              resources.shadow_sampler})
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

bool RenderSystem::set_settings(const RenderSettings& settings)
{
    if (!std::all_of(settings.ambient_color.begin(), settings.ambient_color.end(), [](float value)
                     { return std::isfinite(value) && value >= 0.0F && value <= 1.0F; }) ||
        !std::isfinite(settings.ambient_intensity) || settings.ambient_intensity < 0.0F ||
        settings.ambient_intensity > 1000.0F || !std::isfinite(settings.exposure) ||
        settings.exposure <= 0.0F || settings.exposure > 100.0F ||
        !std::isfinite(settings.shadow_bias) || settings.shadow_bias < 0.0F ||
        settings.shadow_bias > 0.1F || settings.shadow_resolution < 128 ||
        settings.shadow_resolution > 8192 ||
        (settings.shadow_resolution & (settings.shadow_resolution - 1)) != 0 ||
        (renderer_initialized_ && settings.directional_shadows &&
         settings.shadow_resolution > bgfx::getCaps()->limits.maxTextureSize))
    {
        return false;
    }
    settings_ = settings;
    return true;
}

const RenderSettings& RenderSystem::settings() const noexcept
{
    return settings_;
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
        static_cast<std::uint32_t>(bgfx::isValid(resources.tonemap)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.shadow_transform)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.shadow_settings)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.shadow_depth)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.shadow_sampler)) +
        static_cast<std::uint32_t>(bgfx::isValid(resources.shadow_fallback)) +
        resources.shadow.resource_count();
    for (const auto& mesh : resources.meshes)
    {
        count += static_cast<std::uint32_t>(bgfx::isValid(mesh.vertices)) +
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

bool RenderSystem::draw_scene()
{
    if (!prepare_scene())
    {
        return false;
    }
    const Scene* scene = world_ != nullptr ? world_->scene() : nullptr;
    if (scene == nullptr || scene_resources_ == nullptr || !drawable_scene(*scene))
    {
        return true;
    }
    const Camera& camera = *scene->active_camera();
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
    std::size_t shadow_light = light_count;
    if (settings_.directional_shadows)
    {
        for (std::size_t index = 0; index < light_count; ++index)
        {
            if (light_kind_inner[index][0] == static_cast<float>(schema::LightKind::directional))
            {
                shadow_light = index;
                break;
            }
        }
    }
    float shadow_transform[16]{};
    bx::mtxIdentity(shadow_transform);
    const bool shadow_enabled = shadow_light < light_count;
    if (shadow_enabled)
    {
        if (!resources.shadow.prepare(settings_.shadow_resolution) ||
            !resources.draw_shadow(*scene, light_direction_outer[shadow_light], shadow_transform))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot render directional shadow map");
            return false;
        }
    }
    else
    {
        resources.shadow.release();
    }
    const float shadow_settings[] = {settings_.shadow_bias, 1.0F / settings_.shadow_resolution,
                                     static_cast<float>(shadow_light),
                                     shadow_enabled ? 1.0F : 0.0F};
    const float ambient[] = {settings_.ambient_color[0], settings_.ambient_color[1],
                             settings_.ambient_color[2], settings_.ambient_intensity};
    const float tonemap[] = {settings_.exposure, settings_.filmic_tonemap ? 1.0F : 0.0F, 0.0F,
                             0.0F};
    for (const auto& node : scene->nodes())
    {
        const MeshRenderer* renderer = node->get_component<MeshRenderer>();
        if (renderer == nullptr || !renderer->enabled())
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
            bgfx::setUniform(resources.light_kind_inner, light_kind_inner.data(), maximum_lights);
            bgfx::setUniform(resources.ambient, ambient);
            bgfx::setUniform(resources.tonemap, tonemap);
            bgfx::setUniform(resources.shadow_transform, shadow_transform);
            bgfx::setUniform(resources.shadow_settings, shadow_settings);
            bgfx::setTexture(shadow_sampler_stage, resources.shadow_sampler,
                             shadow_enabled ? resources.shadow.color : resources.shadow_fallback);
            bgfx::setTransform(model);
            bgfx::setVertexBuffer(0, mesh->vertices);
            bgfx::setIndexBuffer(mesh->indices, first, count);
            const std::uint64_t cull =
                linear_determinant(model) < 0.0 ? BGFX_STATE_CULL_CCW : BGFX_STATE_CULL_CW;
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                           BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA | cull);
            bgfx::submit(main_view, program->program);
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
}

bool RenderSystem::render()
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

    bgfx::setViewClear(main_view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, settings_.clear_color, 1.0F,
                       0);
    bgfx::setViewRect(main_view, 0, 0, static_cast<std::uint16_t>(width_),
                      static_cast<std::uint16_t>(height_));
    bgfx::touch(main_view);
    const bool drawn = draw_scene();
    bgfx::frame();
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
