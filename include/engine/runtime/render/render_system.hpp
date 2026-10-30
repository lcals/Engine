#pragma once

#include "runtime/system/system.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

union SDL_Event;

namespace engine
{

class WindowSystem;
class WorldSystem;
class FontSystem;
class UiSystem;

struct RenderSettings
{
    std::array<float, 3> ambient_color{1.0F, 1.0F, 1.0F};
    float ambient_intensity = 0.0F;
    float exposure = 1.0F;
    bool filmic_tonemap = false;
    bool directional_shadows = false;
    std::uint16_t shadow_resolution = 2048;
    float shadow_bias = 0.001F;
    std::uint32_t clear_color = 0x181818ff;
};

enum class ScreenshotStatus
{
    idle,
    pending,
    complete,
    failed
};

class RenderSystem final : public System
{
public:
    // windows 和可空 world 的有效期须覆盖本对象整个生命周期。
    explicit RenderSystem(WindowSystem& windows, WorldSystem* world = nullptr,
                          FontSystem* fonts = nullptr, UiSystem* ui = nullptr) noexcept;
    ~RenderSystem() noexcept override;

    [[nodiscard]] bool prepare_scene();
    void release_scene() noexcept;
    [[nodiscard]] bool scene_prepared() const noexcept;
    [[nodiscard]] std::uint32_t gpu_resource_count() const noexcept;
    [[nodiscard]] bool set_settings(const RenderSettings& settings);
    [[nodiscard]] const RenderSettings& settings() const noexcept;
    // 主线程传入绝对 PNG 路径；true 仅表示排队，须继续提交帧并查询状态。
    [[nodiscard]] bool request_screenshot(std::string_view utf8_png_path);
    [[nodiscard]] ScreenshotStatus screenshot_status() const noexcept;
    [[nodiscard]] bool render();

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    static bool watch_surface_lifecycle(void* userdata, SDL_Event* event);
    [[nodiscard]] bool initialize_renderer();
    void shutdown_renderer() noexcept;
    void release_draw_resources() noexcept;
    [[nodiscard]] bool draw_scene();

    struct SceneGpuResources;
    struct RendererCallbacks;

    WindowSystem& windows_;
    WorldSystem* world_ = nullptr;
    FontSystem* fonts_ = nullptr;
    UiSystem* ui_ = nullptr;
    std::unique_ptr<SceneGpuResources> scene_resources_;
    std::unique_ptr<RendererCallbacks> callbacks_;
    RenderSettings settings_{};
    std::uint64_t scene_id_ = 0;
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
