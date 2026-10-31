#pragma once

#include "runtime/render/render_settings.hpp"
#include "runtime/system/system.hpp"

#include <cstdint>
#include <memory>
#include <string_view>

namespace engine
{

class Camera;
class WindowSystem;
class WorldSystem;
class FontSystem;
class UiSystem;

namespace render_detail
{
class RenderSurface;
}

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
    static void suspend_renderer(void* context) noexcept;
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
    std::unique_ptr<render_detail::RenderSurface> surface_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t frame_number_ = 0;
    bool renderer_initialized_ = false;
    bool stats_visible_ = false;
};

} // namespace engine
