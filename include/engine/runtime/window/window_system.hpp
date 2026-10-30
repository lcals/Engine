#pragma once

#include "runtime/schema/common_generated.h"
#include "runtime/system/system.hpp"

#include <array>
#include <atomic>

struct SDL_Window;

namespace engine
{

enum class WindowKey
{
    w,
    a,
    s,
    d,
    space,
    tab,
    count
};
enum class WindowPointerButton
{
    left,
    middle,
    right
};

class WindowSystem final : public System
{
public:
    WindowSystem() noexcept;
    ~WindowSystem() noexcept override;

    [[nodiscard]] bool exit_requested() const noexcept;
    [[nodiscard]] bool can_render() const noexcept;

    // 借用窗口，未创建时为空；渲染系统必须先于窗口系统关闭。
    [[nodiscard]] SDL_Window* native_window() const noexcept;
    [[nodiscard]] bool key_down(WindowKey key) const noexcept;
    [[nodiscard]] bool key_pressed(WindowKey key) const noexcept;
    [[nodiscard]] schema::Vec2 pointer_delta() const noexcept;
    [[nodiscard]] bool pointer_down(WindowPointerButton button) const noexcept;

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
    std::array<bool, static_cast<std::size_t>(WindowKey::count)> keys_{};
    std::array<bool, static_cast<std::size_t>(WindowKey::count)> pressed_{};
    std::array<bool, 3> buttons_{};
    schema::Vec2 pointer_delta_{};
};

} // namespace engine
