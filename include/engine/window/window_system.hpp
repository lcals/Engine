#pragma once

#include "system.hpp"

#include <atomic>

struct SDL_Window;

namespace engine
{

class WindowSystem final : public System
{
public:
    WindowSystem() noexcept;
    ~WindowSystem() noexcept override;

    [[nodiscard]] bool exit_requested() const noexcept;
    [[nodiscard]] bool can_render() const noexcept;

    // 借用窗口，未创建时为空；渲染系统必须先于窗口系统关闭。
    [[nodiscard]] SDL_Window* native_window() const noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    SDL_Window* window_ = nullptr;
    std::atomic<bool> background_ = false;
    bool sdl_initialized_ = false;
    bool watching_events_ = false;
    bool exit_requested_ = false;
};

} // namespace engine
