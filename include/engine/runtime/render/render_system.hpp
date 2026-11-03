#pragma once

#include "runtime/render/render_settings.hpp"
#include "runtime/render/scene_capture.hpp"
#include "runtime/scene/scene_types.hpp"
#include "runtime/system/system.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace engine
{

class Camera;
class WindowSystem;
class WorldSystem;
class FontSystem;
class UiSystem;
class RenderExtension;
class TaskSystem;

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

struct RenderConfig
{
    std::size_t texture_batch_size = 4;
    std::size_t texture_staging_bytes = 64 * 1024 * 1024;
    std::uint16_t cluster_tile_size = 64;
    std::uint16_t cluster_depth_slices = 24;
    std::uint32_t maximum_lights = 256;
    std::uint16_t maximum_cluster_lights = 64;
    std::uint16_t shadow_atlas_size = 4096;
    std::uint16_t local_shadow_tile_size = 512;
    std::uint16_t maximum_local_shadow_faces = 24;
    std::uint8_t maximum_planar_reflections = 2;
};

class RenderSystem final : public System
{
public:
    // windows 和可空借用对象须覆盖本系统生命周期；tasks 非空时须先初始化。
    explicit RenderSystem(WindowSystem& windows, WorldSystem* world = nullptr,
                          FontSystem* fonts = nullptr, UiSystem* ui = nullptr,
                          TaskSystem* tasks = nullptr, const RenderConfig& config = {}) noexcept;
    ~RenderSystem() noexcept override;

    // 不转移所有权；同一扩展只能属于一个渲染器。回调期间拒绝更改登记。
    [[nodiscard]] bool add_extension(RenderExtension& extension);
    // 解除后清理扩展的场景和 GPU 资源；重复解除返回 false。
    [[nodiscard]] bool remove_extension(RenderExtension& extension) noexcept;

    // camera 仅在本次调用借用，可属于调用方独立场景；空值采用世界的活动镜头。
    [[nodiscard]] bool prepare_scene(const Camera* camera = nullptr);
    [[nodiscard]] bool request_diffuse_capture(const SceneCaptureRequest& request);
    [[nodiscard]] SceneCaptureStatus diffuse_capture_status() const noexcept;
    [[nodiscard]] bool take_diffuse_capture(SceneCaptureImage& output);
    void cancel_diffuse_capture() noexcept;
    void release_scene() noexcept;
    [[nodiscard]] bool scene_prepared() const noexcept;
    [[nodiscard]] std::uint32_t gpu_resource_count() const noexcept;
    // 通用 GPU 生命周期与提交进度；帧号为当前图形设备的 bgfx 提交帧号。
    [[nodiscard]] bool gpu_ready() const noexcept;
    [[nodiscard]] std::uint32_t frame_number() const noexcept;
    // 最近配置的绘制目标像素尺寸；系统或 GPU 未就绪时失败并清零。
    [[nodiscard]] bool viewport_size(Vec2& output) const noexcept;
    // 接受呈现配置请求；下次可提交的 render 应用，GPU 临时关闭期间保留。
    [[nodiscard]] bool set_vsync(bool enabled);
    [[nodiscard]] bool vsync() const noexcept;
    // 只设置无活动场景时的清屏色；场景环境与后处理由对应组件管理。
    void set_idle_clear_color(std::uint32_t rgba) noexcept;
    [[nodiscard]] std::uint32_t idle_clear_color() const noexcept;
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
    friend class RenderExtension;
    void detach_extension(RenderExtension& extension) noexcept;
    void release_extensions_scene() noexcept;
    static void suspend_renderer(void* context) noexcept;
    [[nodiscard]] bool initialize_renderer();
    [[nodiscard]] bool resolve_settings(RenderSettings& settings) const noexcept;
    void shutdown_renderer() noexcept;
    void poll_diffuse_capture();
    [[nodiscard]] bool capture_unchanged() const;
    [[nodiscard]] bool draw_diffuse_capture(const RenderSettings& settings);
    void release_draw_resources() noexcept;
    [[nodiscard]] bool draw_scene(const Camera* camera, const RenderSettings& settings);

    struct SceneGpuResources;
    struct RendererCallbacks;

    WindowSystem& windows_;
    WorldSystem* world_ = nullptr;
    FontSystem* fonts_ = nullptr;
    UiSystem* ui_ = nullptr;
    TaskSystem* tasks_ = nullptr;
    RenderConfig config_;
    std::unique_ptr<SceneGpuResources> scene_resources_;
    std::unique_ptr<RendererCallbacks> callbacks_;
    std::uint32_t idle_clear_color_ = 0x181818ff;
    std::unique_ptr<render_detail::RenderSurface> surface_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t frame_number_ = 0;
    SceneCaptureStatus capture_status_ = SceneCaptureStatus::idle;
    SceneCaptureRequest capture_request_;
    std::vector<std::uint8_t> capture_snapshot_;
    bool renderer_initialized_ = false;
    bool stats_visible_ = false;
    bool vsync_enabled_ = true;
    bool vsync_applied_ = true;
    std::vector<RenderExtension*> extensions_;
    bool invoking_extensions_ = false;
};

} // namespace engine
