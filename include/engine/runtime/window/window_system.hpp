#pragma once

#include "runtime/system/system.hpp"

#include <atomic>
#include <cstdint>
#include <vector>

struct SDL_Window;
union SDL_Event;

namespace engine
{

using WindowEventHandler = void (*)(void*, const SDL_Event&);

enum class WindowMode
{
    windowed,
    borderless,
    fullscreen
};

struct WindowRect
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct WindowSize
{
    std::uint32_t logical_width = 0;
    std::uint32_t logical_height = 0;
    std::uint32_t pixel_width = 0;
    std::uint32_t pixel_height = 0;
};

struct DisplayMode
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    float pixel_density = 0.0F;
    float refresh_rate = 0.0F;
};

enum class MouseMode
{
    normal,
    hidden,
    relative
};

class WindowSystem final : public System
{
public:
    WindowSystem() noexcept;
    ~WindowSystem() noexcept override;

    [[nodiscard]] bool exit_requested() const noexcept;
    [[nodiscard]] bool can_render() const noexcept;
    [[nodiscard]] bool request_exit() noexcept;
    // SDL 主线程；逻辑尺寸与窗口表面像素尺寸分别返回，失败清零。
    [[nodiscard]] bool size(WindowSize& output) const noexcept;
    [[nodiscard]] bool safe_area(WindowRect& output) const noexcept;
    [[nodiscard]] bool display_modes(std::vector<DisplayMode>& output) const;
    [[nodiscard]] bool mode(WindowMode& output) const noexcept;
    [[nodiscard]] bool set_size(std::uint32_t logical_width, std::uint32_t logical_height);
    // 独占模式只借用本次调用的枚举值；成功表示请求已接受，实际值继续查询。
    [[nodiscard]] bool set_mode(WindowMode mode, const DisplayMode* exclusive_mode = nullptr);
    [[nodiscard]] bool mouse_mode(MouseMode& output) const noexcept;
    [[nodiscard]] bool set_mouse_mode(MouseMode mode);

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
