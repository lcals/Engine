#include "runtime/render/render_system.hpp"

#include "runtime/performance/profiling.hpp"
#include "runtime/render/render_constants.hpp"
#include "runtime/render/render_extension.hpp"
#include "runtime/render/render_surface.hpp"
#include "runtime/render/renderer_callbacks.hpp"
#include "runtime/render/scene_gpu_resources.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/ui/ui_system.hpp"
#include "runtime/window/window_system.hpp"

#include <SDL3/SDL.h>
#include <bgfx/platform.h>

#include <algorithm>
#include <array>
#include <new>

namespace engine
{

// SDL 窗口和 bgfx 生命周期都由主线程驱动；bgfx 只支持一个活动实例。
namespace
{
RenderSystem* active_renderer = nullptr;

#if defined(ENGINE_ENABLE_TRACY)
void profile_renderer_stats()
{
    const auto* stats = bgfx::getStats();
    if (stats == nullptr)
    {
        return;
    }
    // Stats 对应已经完成的后端帧，可能滞后于提交线程。
    // cpuTimeFrame 包含玩法与限帧等待，不能作为渲染 CPU 耗时。
    if (stats->cpuTimerFreq > 0)
    {
        const double milliseconds = 1000.0 / static_cast<double>(stats->cpuTimerFreq);
        if (stats->cpuTimeFrame > 0)
        {
            ENGINE_PROFILE_PLOT("Bgfx.frameIntervalMs", stats->cpuTimeFrame * milliseconds);
        }
        if (stats->cpuTimeBegin > 0 && stats->cpuTimeEnd >= stats->cpuTimeBegin)
        {
            ENGINE_PROFILE_PLOT("Bgfx.renderThreadSubmitMs",
                                (stats->cpuTimeEnd - stats->cpuTimeBegin) * milliseconds);
        }
        if (stats->waitRender >= 0)
        {
            ENGINE_PROFILE_PLOT("Bgfx.waitRenderMs", stats->waitRender * milliseconds);
        }
        if (stats->waitSubmit >= 0)
        {
            ENGINE_PROFILE_PLOT("Bgfx.waitSubmitMs", stats->waitSubmit * milliseconds);
        }
    }
    // GPU 时间戳使用独立时钟；不支持或不可用的数据不生成采样值。
    if (stats->gpuTimerFreq > 0 && stats->gpuTimeBegin > 0 &&
        stats->gpuTimeEnd >= stats->gpuTimeBegin)
    {
        const double milliseconds = 1000.0 / static_cast<double>(stats->gpuTimerFreq);
        ENGINE_PROFILE_PLOT("Bgfx.gpuFrameMs",
                            (stats->gpuTimeEnd - stats->gpuTimeBegin) * milliseconds);
        ENGINE_PROFILE_PLOT("Bgfx.gpuFrameNumber", static_cast<std::int64_t>(stats->gpuFrameNum));
    }
    ENGINE_PROFILE_PLOT("Bgfx.drawCalls", static_cast<std::int64_t>(stats->numDraw));
    ENGINE_PROFILE_PLOT("Bgfx.computeCalls", static_cast<std::int64_t>(stats->numCompute));
    ENGINE_PROFILE_PLOT("Bgfx.blitCalls", static_cast<std::int64_t>(stats->numBlit));
    ENGINE_PROFILE_PLOT("Bgfx.triangles",
                        static_cast<std::int64_t>(stats->numPrims[bgfx::Topology::TriList]));
    ENGINE_PROFILE_PLOT("Bgfx.textures", static_cast<std::int64_t>(stats->numTextures));
    ENGINE_PROFILE_PLOT("Bgfx.vertexBuffers", static_cast<std::int64_t>(stats->numVertexBuffers));
    ENGINE_PROFILE_PLOT("Bgfx.indexBuffers", static_cast<std::int64_t>(stats->numIndexBuffers));
    ENGINE_PROFILE_PLOT("Bgfx.frameBuffers", static_cast<std::int64_t>(stats->numFrameBuffers));
    // 资源估算与驱动上报占用口径不同且有重叠，分别记录，不能相加。
    if (stats->textureMemoryUsed >= 0)
    {
        ENGINE_PROFILE_PLOT("Bgfx.textureEstimatedBytes", stats->textureMemoryUsed);
    }
    if (stats->rtMemoryUsed >= 0)
    {
        ENGINE_PROFILE_PLOT("Bgfx.renderTargetEstimatedBytes", stats->rtMemoryUsed);
    }
    if (stats->transientVbUsed >= 0)
    {
        ENGINE_PROFILE_PLOT("Bgfx.transientVertexBytes",
                            static_cast<std::int64_t>(stats->transientVbUsed));
    }
    if (stats->transientIbUsed >= 0)
    {
        ENGINE_PROFILE_PLOT("Bgfx.transientIndexBytes",
                            static_cast<std::int64_t>(stats->transientIbUsed));
    }
    if (stats->gpuMemoryUsed >= 0)
    {
        ENGINE_PROFILE_PLOT("Bgfx.gpuResidentBytes", stats->gpuMemoryUsed);
    }
    if (stats->gpuMemoryMax >= 0)
    {
        ENGINE_PROFILE_PLOT("Bgfx.gpuBudgetBytes", stats->gpuMemoryMax);
    }
}
#endif
} // namespace

RenderSystem::RenderSystem(WindowSystem& windows, WorldSystem* world, FontSystem* fonts,
                           UiSystem* ui, TaskSystem* tasks, const RenderConfig& config) noexcept
    : windows_(windows), world_(world), fonts_(fonts), ui_(ui), tasks_(tasks), config_(config)
{
}

RenderSystem::~RenderSystem() noexcept
{
    shutdown();
    for (auto* extension : extensions_)
    {
        extension->renderer_ = nullptr;
    }
    extensions_.clear();
}

bool RenderSystem::add_extension(RenderExtension& extension)
{
    if (invoking_extensions_ || extension.renderer_ != nullptr)
    {
        return false;
    }
    extensions_.push_back(&extension);
    extension.renderer_ = this;
    return true;
}

bool RenderSystem::remove_extension(RenderExtension& extension) noexcept
{
    if (invoking_extensions_ || extension.renderer_ != this)
    {
        return false;
    }
    detach_extension(extension);
    invoking_extensions_ = true;
    extension.release_scene();
    extension.release_gpu();
    invoking_extensions_ = false;
    return true;
}

void RenderSystem::detach_extension(RenderExtension& extension) noexcept
{
    std::erase(extensions_, &extension);
    extension.renderer_ = nullptr;
}

void RenderSystem::release_extensions_scene() noexcept
{
    invoking_extensions_ = true;
    for (auto* extension : extensions_)
    {
        extension->release_scene();
    }
    invoking_extensions_ = false;
}

bool RenderSystem::on_initialize(const EngineConfig&)
{
    if (config_.cluster_tile_size < 16 || config_.cluster_tile_size > 256 ||
        config_.cluster_depth_slices == 0 || config_.cluster_depth_slices > 64 ||
        config_.maximum_lights == 0 || config_.maximum_lights > 4096 ||
        config_.maximum_cluster_lights == 0 || config_.maximum_cluster_lights > 256 ||
        config_.shadow_atlas_size < 512 || config_.shadow_atlas_size > 8192 ||
        (config_.shadow_atlas_size & (config_.shadow_atlas_size - 1)) != 0 ||
        config_.local_shadow_tile_size < 64 || config_.local_shadow_tile_size > 2048 ||
        (config_.local_shadow_tile_size & (config_.local_shadow_tile_size - 1)) != 0 ||
        config_.local_shadow_tile_size > config_.shadow_atlas_size ||
        config_.maximum_local_shadow_faces > 24 || config_.maximum_planar_reflections > 2)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Renderer lighting/reflection budgets are invalid");
        return false;
    }
    if (!windows_.is_initialized() || windows_.native_window() == nullptr ||
        (tasks_ != nullptr && !tasks_->is_initialized()) || config_.texture_batch_size == 0 ||
        config_.texture_batch_size > 64 || config_.texture_staging_bytes == 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Renderer requires a ready window");
        return false;
    }
    surface_.reset(new (std::nothrow) render_detail::RenderSurface{});
    if (surface_ == nullptr ||
        !surface_->initialize(*windows_.native_window(), this, suspend_renderer))
    {
        return false;
    }
    return initialize_renderer();
}

void RenderSystem::suspend_renderer(void* context) noexcept
{
    static_cast<RenderSystem*>(context)->shutdown_renderer();
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

    bgfx::Init init{};
    bool available = false;
    if (surface_ == nullptr || !surface_->prepare(init, available))
    {
        return false;
    }
    if (!available)
    {
        // 平台表面暂不可用时保留系统；调用方恢复提交后再建立GPU设备。
        return true;
    }
    width_ = init.resolution.width;
    height_ = init.resolution.height;
    init.resolution.reset =
        render_detail::reset_flags | (vsync_enabled_ ? BGFX_RESET_VSYNC : BGFX_RESET_NONE);
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
    vsync_applied_ = vsync_enabled_;
    active_renderer = this;
    if (bgfx::getRendererType() != init.type)
    {
        SDL_LogError(
            SDL_LOG_CATEGORY_RENDER, "Required renderer %s is unavailable; bgfx selected %s",
            bgfx::getRendererName(init.type), bgfx::getRendererName(bgfx::getRendererType()));
        return false;
    }

    bgfx::setViewName(render_detail::main_view, "Main window");
    constexpr std::array shadow_names = {"Directional shadow 1", "Directional shadow 2",
                                         "Directional shadow 3", "Directional shadow 4"};
    for (std::uint16_t index = 0; index < render_detail::maximum_shadow_cascades; ++index)
    {
        bgfx::setViewName(render_detail::shadow_view + index, shadow_names[index]);
    }
    bgfx::setViewClear(render_detail::main_view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                       idle_clear_color_, 1.0F, 0);
    bgfx::setDebug(stats_visible_ ? BGFX_DEBUG_STATS : BGFX_DEBUG_NONE);
    SDL_Log("Renderer initialized: %s (%u x %u)", bgfx::getRendererName(bgfx::getRendererType()),
            width_, height_);
    return true;
}

bool RenderSystem::gpu_ready() const noexcept
{
    return renderer_initialized_;
}

std::uint32_t RenderSystem::frame_number() const noexcept
{
    return frame_number_;
}

bool RenderSystem::viewport_size(Vec2& output) const noexcept
{
    output = {};
    if (!is_initialized() || !renderer_initialized_ || width_ == 0 || height_ == 0)
    {
        return false;
    }
    output = {static_cast<double>(width_), static_cast<double>(height_)};
    return true;
}

void RenderSystem::set_stats_visible(bool visible)
{
    stats_visible_ = visible;
    if (renderer_initialized_)
    {
        bgfx::setDebug(stats_visible_ ? BGFX_DEBUG_STATS : BGFX_DEBUG_NONE);
    }
}

bool RenderSystem::set_vsync(bool enabled)
{
    if (!is_initialized())
    {
        return false;
    }
    vsync_enabled_ = enabled;
    return true;
}

bool RenderSystem::vsync() const noexcept
{
    return vsync_enabled_;
}

bool RenderSystem::stats_visible() const noexcept
{
    return stats_visible_;
}

bool RenderSystem::render(const Camera* camera)
{
    ENGINE_PROFILE_ZONE("Render.frame");
    if (callbacks_ != nullptr)
    {
        callbacks_->process(tasks_);
    }
    if (!is_initialized() || !windows_.can_render())
    {
        return false;
    }

    if (!renderer_initialized_)
    {
        if (!initialize_renderer() || !renderer_initialized_)
        {
            return false;
        }
    }

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bgfx::PlatformData data{};
    if (surface_ == nullptr)
    {
        return false;
    }
    const auto change = surface_->poll(data, width, height);
    if (change == render_detail::SurfaceChange::error)
    {
        return false;
    }
    const bool surface_changed = change != render_detail::SurfaceChange::unchanged;
    if (change == render_detail::SurfaceChange::recreate)
    {
        shutdown_renderer();
        if (!initialize_renderer() || !renderer_initialized_)
        {
            return false;
        }
    }
    else if (change == render_detail::SurfaceChange::rebind)
    {
        bgfx::setPlatformData(data);
    }
    if (surface_changed || width != width_ || height != height_ || vsync_enabled_ != vsync_applied_)
    {
        bgfx::reset(width, height,
                    render_detail::reset_flags |
                        (vsync_enabled_ ? BGFX_RESET_VSYNC : BGFX_RESET_NONE));
        vsync_applied_ = vsync_enabled_;
        width_ = width;
        height_ = height;
    }

    RenderSettings current_settings;
    if (!resolve_settings(current_settings))
    {
        return false;
    }
    bgfx::setViewFrameBuffer(render_detail::main_view, BGFX_INVALID_HANDLE);
    bgfx::setViewClear(render_detail::main_view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                       current_settings.clear_color, 1.0F, 0);
    bgfx::setViewRect(render_detail::main_view, 0, 0, static_cast<std::uint16_t>(width_),
                      static_cast<std::uint16_t>(height_));
    bgfx::touch(render_detail::main_view);
    const bool drawn = draw_scene(camera, current_settings) &&
                       draw_diffuse_capture(current_settings) &&
                       (ui_ == nullptr || ui_->render(width_, height_));
    {
        ENGINE_PROFILE_ZONE("Render.frameSubmit");
        frame_number_ = bgfx::frame();
    }
    poll_diffuse_capture();
    ENGINE_PROFILE_FRAME();
#if defined(ENGINE_ENABLE_TRACY)
    profile_renderer_stats();
#endif
    return drawn;
}

void RenderSystem::on_shutdown() noexcept
{
    shutdown_renderer();
    if (surface_ != nullptr)
    {
        surface_->shutdown();
        surface_.reset();
    }
}

void RenderSystem::shutdown_renderer() noexcept
{
    release_scene();
    invoking_extensions_ = true;
    for (auto* extension : extensions_)
    {
        extension->release_gpu();
    }
    invoking_extensions_ = false;
    if (renderer_initialized_)
    {
        bgfx::shutdown();
        renderer_initialized_ = false;
        active_renderer = nullptr;
        frame_number_ = 0;
    }
    if (callbacks_ != nullptr)
    {
        callbacks_->finish();
    }
    // GPU后台线程退出后，平台才可释放原生表面并完成暂停握手。
    if (surface_ != nullptr)
    {
        surface_->release();
    }
    width_ = 0;
    height_ = 0;
}

} // namespace engine
