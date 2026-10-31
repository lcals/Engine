#include "runtime/render/render_system.hpp"

#include "runtime/render/render_constants.hpp"
#include "runtime/render/render_surface.hpp"
#include "runtime/render/renderer_callbacks.hpp"
#include "runtime/render/scene_gpu_resources.hpp"
#include "runtime/ui/ui_system.hpp"
#include "runtime/window/window_system.hpp"

#include <SDL3/SDL.h>
#include <bgfx/platform.h>

#include <array>
#include <new>

namespace engine
{

// SDL 窗口和 bgfx 生命周期都由主线程驱动；bgfx 只支持一个活动实例。
namespace
{
RenderSystem* active_renderer = nullptr;
}

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
    if (!windows_.is_initialized() || windows_.native_window() == nullptr)
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
    init.resolution.reset = render_detail::reset_flags;
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

    bgfx::setViewName(render_detail::main_view, "Main window");
    constexpr std::array shadow_names = {"Directional shadow 1", "Directional shadow 2",
                                         "Directional shadow 3", "Directional shadow 4"};
    for (std::uint16_t index = 0; index < render_detail::maximum_shadow_cascades; ++index)
    {
        bgfx::setViewName(render_detail::shadow_view + index, shadow_names[index]);
    }
    bgfx::setViewClear(render_detail::main_view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                       idle_settings_.clear_color, 1.0F, 0);
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

bool RenderSystem::render(const Camera* camera)
{
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
    if (surface_changed || width != width_ || height != height_)
    {
        bgfx::reset(width, height, render_detail::reset_flags);
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
    const bool drawn =
        draw_scene(camera, current_settings) && (ui_ == nullptr || ui_->render(width_, height_));
    frame_number_ = bgfx::frame();
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
    if (renderer_initialized_)
    {
        bgfx::shutdown();
        renderer_initialized_ = false;
        active_renderer = nullptr;
        frame_number_ = 0;
    }
    if (callbacks_ != nullptr && callbacks_->status.load() == ScreenshotStatus::pending)
    {
        callbacks_->status.store(ScreenshotStatus::failed);
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
