#pragma once

#include "runtime/system/system.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

union SDL_Event;

namespace engine
{

class Camera;
class WindowSystem;
class WorldSystem;
class FontSystem;
class UiSystem;

enum class FogMode : std::uint8_t
{
    none,
    linear,
    exponential,
    exponential_squared
};

struct RenderSettings
{
    std::array<float, 3> ambient_color{1.0F, 1.0F, 1.0F};
    float ambient_intensity = 0.0F;
    bool ambient_tricolor = false;
    std::array<float, 3> ambient_sky_color{1.0F, 1.0F, 1.0F};
    std::array<float, 3> ambient_equator_color{1.0F, 1.0F, 1.0F};
    std::array<float, 3> ambient_ground_color{1.0F, 1.0F, 1.0F};
    FogMode fog_mode = FogMode::none;
    std::array<float, 3> fog_color{0.5F, 0.5F, 0.5F};
    float fog_start = 0.0F;
    float fog_end = 100.0F;
    float fog_density = 0.01F;
    float exposure = 1.0F;
    bool filmic_tonemap = false;
    bool ao_enabled = false;
    float ao_radius = 1.0F;
    float ao_strength = 1.0F;
    float ao_power = 1.5F;
    float ao_fade_start = 80.0F;
    float ao_fade_end = 150.0F;
    bool hdr_enabled = false;
    bool bloom_enabled = false;
    float bloom_threshold = 1.0F;
    float bloom_intensity = 0.15F;
    float bloom_radius = 1.0F;
    bool ibl_enabled = false;
    float ibl_intensity = 1.0F;
    bool directional_shadows = false;
    std::uint16_t shadow_resolution = 2048;
    float shadow_bias = 0.001F;
    std::uint8_t shadow_cascades = 4;
    float shadow_distance = 150.0F;
    float shadow_split_lambda = 0.6F;
    float shadow_cascade_blend = 0.1F;
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

    // camera 仅在本次调用借用，可属于调用方独立场景；空值采用世界的活动镜头。
    [[nodiscard]] bool prepare_scene(const Camera* camera = nullptr);
    void release_scene() noexcept;
    [[nodiscard]] bool scene_prepared() const noexcept;
    [[nodiscard]] std::uint32_t gpu_resource_count() const noexcept;
    // 通用 GPU 生命周期与提交进度；帧号为当前图形设备的 bgfx 提交帧号。
    [[nodiscard]] bool gpu_ready() const noexcept;
    [[nodiscard]] std::uint32_t frame_number() const noexcept;
    // 有场景时只修改现有组件；缺少变更域的组件时失败，不创建节点。
    // 无场景时保存空白窗口配置，该配置不成为后续场景的默认值。
    [[nodiscard]] bool set_settings(const RenderSettings& settings);
    // 返回当前场景组件快照；场景无相应组件时使用固定引擎默认值。
    [[nodiscard]] RenderSettings settings() const noexcept;
    // 主线程调用；开关与场景无关，图形设备临时关闭后保留并恢复。
    void set_stats_visible(bool visible);
    [[nodiscard]] bool stats_visible() const noexcept;
    // 主线程传入绝对 PNG 路径；true 仅表示排队，须继续提交帧并查询状态。
    [[nodiscard]] bool request_screenshot(std::string_view utf8_png_path);
    [[nodiscard]] ScreenshotStatus screenshot_status() const noexcept;
    [[nodiscard]] bool render(const Camera* camera = nullptr);

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    static bool watch_surface_lifecycle(void* userdata, SDL_Event* event);
    [[nodiscard]] bool initialize_renderer();
    [[nodiscard]] bool resolve_settings(RenderSettings& settings) const noexcept;
    void shutdown_renderer() noexcept;
    void release_draw_resources() noexcept;
    [[nodiscard]] bool draw_scene(const Camera* camera, const RenderSettings& settings);

    struct SceneGpuResources;
    struct RendererCallbacks;

    WindowSystem& windows_;
    WorldSystem* world_ = nullptr;
    FontSystem* fonts_ = nullptr;
    UiSystem* ui_ = nullptr;
    std::unique_ptr<SceneGpuResources> scene_resources_;
    std::unique_ptr<RendererCallbacks> callbacks_;
    RenderSettings idle_settings_{};
    std::uint64_t scene_id_ = 0;
    void* metal_view_ = nullptr;
    void* native_window_ = nullptr;
    void* native_display_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t frame_number_ = 0;
    bool surface_registered_ = false;
    bool watching_events_ = false;
    bool renderer_initialized_ = false;
    bool stats_visible_ = false;
};

} // namespace engine
