#pragma once

#include "runtime/system/system.hpp"

#include <atomic>

struct SDL_Window;
union SDL_Event;

namespace engine
{

using WindowEventHandler = void (*)(void*, const SDL_Event&);

class WindowSystem final : public System
{
public:
    WindowSystem() noexcept;
    ~WindowSystem() noexcept override;

    [[nodiscard]] bool exit_requested() const noexcept;
    [[nodiscard]] bool can_render() const noexcept;

    // 借用窗口，未创建时为空；渲染系统必须先于窗口系统关闭。
    [[nodiscard]] SDL_Window* native_window() const noexcept;
    // 单宿主事件接收者；所有输入与认领规则由宿主串联，不在窗口缓存输入状态。
    void set_event_handler(WindowEventHandler handler, void* context) noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    WindowEventHandler event_handler_ = nullptr;
    void* event_context_ = nullptr;
    SDL_Window* window_ = nullptr;
    std::atomic<bool> background_ = false;
    bool sdl_initialized_ = false;
    bool watching_events_ = false;
    bool exit_requested_ = false;
};

} // namespace engine
