#pragma once

#include "system.hpp"

#include <cstdint>

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
    WindowSystem& windows_;
    void* metal_view_ = nullptr;
    void* native_window_ = nullptr;
    void* native_display_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool renderer_initialized_ = false;
};

} // namespace engine
