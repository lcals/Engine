#include "runtime/render/render_system.hpp"

#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>

#if defined(SDL_PLATFORM_ANDROID)
#include "android/native/android_surface.hpp"

#include <android/native_window.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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
constexpr std::uint16_t main_view = 0;
constexpr std::uint32_t reset_flags = BGFX_RESET_VSYNC;

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

struct RenderSystem::SceneGpuResources
{
    struct Uniform
    {
        std::string name;
        bgfx::UniformHandle handle = BGFX_INVALID_HANDLE;
        std::array<float, 4> values{};
    };

    struct Program
    {
        std::string path;
        bgfx::ShaderHandle vertex_shader = BGFX_INVALID_HANDLE;
        bgfx::ShaderHandle fragment_shader = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
        std::vector<Uniform> uniforms;

        bool create(const schema::ShaderDataT& shader)
        {
            path = shader.path;
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
                    if (name == "u_normal_transform" || name == "u_camera_position" ||
                        name == "u_light_direction" || name == "u_light_color")
                    {
                        const auto type = name == "u_normal_transform" ? bgfx::UniformType::Mat4
                                                                       : bgfx::UniformType::Vec4;
                        if (info.type != type || info.num != 1)
                        {
                            return false;
                        }
                        continue;
                    }
                    if (info.type != bgfx::UniformType::Vec4 || info.num != 1 ||
                        std::none_of(shader.parameters.begin(), shader.parameters.end(),
                                     [&name](const auto& parameter)
                                     { return parameter->uniform_name == name; }))
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
        }

        bool bind_material(const Material& material)
        {
            const auto* shader = ResourceSystem::find_shader(path);
            if (shader == nullptr)
            {
                return false;
            }
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
            return true;
        }
    };

    struct Mesh
    {
        std::string path;
        bgfx::VertexBufferHandle vertices = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle indices = BGFX_INVALID_HANDLE;

        bool create(const schema::MeshDataT& data)
        {
            path = data.path;
            const auto vertex_bytes = data.vertices.size() * sizeof(schema::MeshVertex);
            const auto index_bytes = data.indices.size() * sizeof(std::uint32_t);
            if (vertex_bytes > std::numeric_limits<std::uint32_t>::max() ||
                index_bytes > std::numeric_limits<std::uint32_t>::max())
            {
                return false;
            }
            bgfx::VertexLayout layout;
            layout.begin()
                .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
                .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
                .end();
            static_assert(sizeof(schema::MeshVertex) == 6 * sizeof(float));
            vertices = bgfx::createVertexBuffer(
                bgfx::copy(data.vertices.data(), static_cast<std::uint32_t>(vertex_bytes)), layout);
            indices = bgfx::createIndexBuffer(
                bgfx::copy(data.indices.data(), static_cast<std::uint32_t>(index_bytes)),
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
    std::vector<Mesh> meshes;
    bgfx::UniformHandle normal_transform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle camera_position = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_direction = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_color = BGFX_INVALID_HANDLE;
    std::vector<Program> programs;
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
    bgfx::setViewClear(main_view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x181818ff, 1.0F, 0);
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
        if (!bgfx::isValid(resources.normal_transform) ||
            !bgfx::isValid(resources.camera_position) ||
            !bgfx::isValid(resources.light_direction) || !bgfx::isValid(resources.light_color))
        {
            release_scene();
            return false;
        }
        scene_id_ = scene->id();
    }
    auto& programs = scene_resources_->programs;
    std::set<std::string> needed;
    std::set<std::string> needed_meshes;
    auto& meshes = scene_resources_->meshes;
    for (const auto& node : scene->nodes())
    {
        const auto* mesh = node->get_component<MeshRenderer>();
        if (mesh == nullptr || !mesh->enabled())
        {
            continue;
        }
        const std::string mesh_path(mesh->mesh_path());
        if (needed_meshes.insert(mesh_path).second &&
            std::none_of(meshes.begin(), meshes.end(),
                         [&mesh_path](const auto& item) { return item.path == mesh_path; }))
        {
            const auto* data = ResourceSystem::find_mesh(mesh_path);
            if (data == nullptr)
            {
                release_scene();
                return false;
            }
            meshes.emplace_back();
            if (!meshes.back().create(*data))
            {
                release_scene();
                return false;
            }
        }
        const std::string path(mesh->material().shader_path());
        if (!needed.insert(path).second)
        {
            continue;
        }
        const auto* shader = ResourceSystem::find_shader(path);
        if (shader == nullptr)
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Unknown shader resource: %s", path.c_str());
            release_scene();
            return false;
        }
        if (std::none_of(programs.begin(), programs.end(),
                         [&path](const auto& program) { return program.path == path; }))
        {
            programs.emplace_back();
            if (!programs.back().create(*shader))
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot prepare shader resource: %s",
                             path.c_str());
                release_scene();
                return false;
            }
        }
    }
    for (auto iterator = meshes.begin(); iterator != meshes.end();)
    {
        if (!needed_meshes.contains(iterator->path))
        {
            iterator->release();
            iterator = meshes.erase(iterator);
        }
        else
        {
            ++iterator;
        }
    }
    // 材质切换 shader 后退役不再使用的程序，保持项目GPU资源有界。
    for (auto iterator = programs.begin(); iterator != programs.end();)
    {
        if (!needed.contains(iterator->path))
        {
            iterator->release();
            iterator = programs.erase(iterator);
        }
        else
        {
            ++iterator;
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
        for (auto uniform : {resources.normal_transform, resources.camera_position,
                             resources.light_direction, resources.light_color})
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

std::uint32_t RenderSystem::gpu_resource_count() const noexcept
{
    if (scene_resources_ == nullptr)
    {
        return 0;
    }
    const SceneGpuResources& resources = *scene_resources_;
    std::uint32_t count = static_cast<std::uint32_t>(bgfx::isValid(resources.normal_transform)) +
                          static_cast<std::uint32_t>(bgfx::isValid(resources.camera_position)) +
                          static_cast<std::uint32_t>(bgfx::isValid(resources.light_direction)) +
                          static_cast<std::uint32_t>(bgfx::isValid(resources.light_color));
    for (const auto& mesh : resources.meshes)
    {
        count += static_cast<std::uint32_t>(bgfx::isValid(mesh.vertices)) +
                 static_cast<std::uint32_t>(bgfx::isValid(mesh.indices));
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
    bx::mtxProj(projection, settings.fov_degrees, static_cast<float>(width_) / height_,
                settings.near_clip, settings.far_clip, bgfx::getCaps()->homogeneousDepth,
                bx::Handedness::Right);
    if (!camera_view(camera_world, view) || !finite_matrix(projection))
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Camera view or projection matrix is invalid");
        return false;
    }
    bgfx::setViewTransform(main_view, view, projection);
    const float camera_position[] = {camera_world[12], camera_world[13], camera_world[14], 1.0F};
    std::array<float, 4> light_direction{0.0F, 0.0F, -1.0F, 0.0F};
    std::array<float, 4> light_color{};
    for (const auto& node : scene->nodes())
    {
        const Light* light = node->get_component<Light>();
        if (light == nullptr || !light->enabled())
        {
            continue;
        }
        float light_world[16]{};
        const auto* transform = node->get_component<Transform>();
        if (transform == nullptr || !transform->world_matrix(light_world))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Light world matrix is invalid");
            return false;
        }
        const double length =
            std::hypot(static_cast<double>(light_world[8]), static_cast<double>(light_world[9]),
                       static_cast<double>(light_world[10]));
        if (!(length > 0.0))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Light direction is invalid");
            return false;
        }
        light_direction = {static_cast<float>(-light_world[8] / length),
                           static_cast<float>(-light_world[9] / length),
                           static_cast<float>(-light_world[10] / length), 0.0F};
        const LightSettings& light_settings = light->settings();
        light_color = {light_settings.color.r() * light_settings.intensity,
                       light_settings.color.g() * light_settings.intensity,
                       light_settings.color.b() * light_settings.intensity, 0.0F};
        break; // 按节点顺序选取第一个启用的方向光。
    }
    SceneGpuResources& resources = *scene_resources_;
    for (const auto& node : scene->nodes())
    {
        const MeshRenderer* mesh = node->get_component<MeshRenderer>();
        if (mesh == nullptr || !mesh->enabled())
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
        const Material& material = mesh->material();
        auto program = std::find_if(resources.programs.begin(), resources.programs.end(),
                                    [&material](const auto& item)
                                    { return item.path == material.shader_path(); });
        if (program == resources.programs.end() || !program->bind_material(material))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Material parameters do not match its shader");
            return false;
        }
        bgfx::setUniform(resources.normal_transform, normal_transform);
        bgfx::setUniform(resources.camera_position, camera_position);
        bgfx::setUniform(resources.light_direction, light_direction.data());
        bgfx::setUniform(resources.light_color, light_color.data());
        bgfx::setTransform(model);
        const auto mesh_resource =
            std::find_if(resources.meshes.begin(), resources.meshes.end(),
                         [mesh](const auto& item) { return item.path == mesh->mesh_path(); });
        if (mesh_resource == resources.meshes.end())
        {
            return false;
        }
        bgfx::setVertexBuffer(0, mesh_resource->vertices);
        bgfx::setIndexBuffer(mesh_resource->indices);
        const std::uint64_t cull =
            linear_determinant(model) < 0.0F ? BGFX_STATE_CULL_CCW : BGFX_STATE_CULL_CW;
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                       BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA | cull);
        bgfx::submit(main_view, program->program);
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
