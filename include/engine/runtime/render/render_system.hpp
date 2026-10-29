#pragma once

#include "runtime/system/system.hpp"

#include <cstdint>

union SDL_Event;

namespace engine
{

class WindowSystem;

class RenderSystem final : public System
{
public:
    // windows 的有效期须覆盖本对象整个生命周期。
    explicit RenderSystem(WindowSystem& windows) noexcept;
    ~RenderSystem() noexcept override;

    [[nodiscard]] bool render();

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    static bool watch_surface_lifecycle(void* userdata, SDL_Event* event);
    [[nodiscard]] bool initialize_renderer();
    void shutdown_renderer() noexcept;

    WindowSystem& windows_;
    void* metal_view_ = nullptr;
    void* native_window_ = nullptr;
    void* native_display_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool surface_registered_ = false;
    bool watching_events_ = false;
    bool renderer_initialized_ = false;
};

} // namespace engine
