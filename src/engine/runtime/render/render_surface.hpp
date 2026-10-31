#pragma once

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>

#include <cstdint>

namespace engine::render_detail
{

enum class SurfaceChange
{
    unchanged,
    rebind,
    recreate,
    error
};

// RenderSystem 私有平台适配。窗口、context 必须存活到 shutdown；所有调用在 SDL 主线程。
class RenderSurface final
{
public:
    using SuspendCallback = void (*)(void*) noexcept;

    RenderSurface() = default;
    ~RenderSurface() noexcept;
    RenderSurface(const RenderSurface&) = delete;
    RenderSurface& operator=(const RenderSurface&) = delete;
    RenderSurface(RenderSurface&&) = delete;
    RenderSurface& operator=(RenderSurface&&) = delete;

    [[nodiscard]] bool initialize(SDL_Window& window, void* context, SuspendCallback suspend);

    // 写入后端、平台句柄及像素尺寸；保留调用方 Init 的其他配置。
    // true + available=false 表示设备暂不可用，调用方保留系统并在恢复后重试。
    [[nodiscard]] bool prepare(bgfx::Init& init, bool& available);

    // 不执行 bgfx 操作。rebind 后调用方 setPlatformData；recreate 后先关闭 GPU 再 prepare。
    // 失败清空全部输出；无原生句柄变化也会返回最新尺寸。
    [[nodiscard]] SurfaceChange poll(bgfx::PlatformData& data, std::uint32_t& width,
                                     std::uint32_t& height);

    // 必须先关闭 bgfx；释放平台句柄，保留事件观察器与借用窗口以便恢复。
    void release() noexcept;

    // 先撤销回调，再释放平台句柄；支持部分初始化和重复调用，不触发 suspend 回调。
    void shutdown() noexcept;

private:
    [[nodiscard]] bool read_size(std::uint32_t& width, std::uint32_t& height) const;

    [[nodiscard]] bool read_platform_data(bgfx::PlatformData& data) const;
    [[nodiscard]] bool start_watching();
    void stop_watching() noexcept;
    static bool SDLCALL watch_surface_lifecycle(void* userdata, SDL_Event* event);

    SDL_Window* window_ = nullptr;
    void* context_ = nullptr;
    SuspendCallback suspend_ = nullptr;
    bgfx::PlatformData current_{};
    void* metal_view_ = nullptr;
    bool rendering_registered_ = false;
    bool watching_events_ = false;
    bool suspending_ = false;
    bool recreate_on_change_ = false;
};

} // namespace engine::render_detail
