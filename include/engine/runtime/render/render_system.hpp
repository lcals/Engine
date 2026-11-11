#pragma once
#include "runtime/scene/camera.hpp"

#include "runtime/render/render_features.hpp"
#include "runtime/render/render_observation.hpp"
#include "runtime/render/render_settings.hpp"
#include "runtime/render/scene_capture.hpp"
#include "runtime/scene/scene_types.hpp"
#include "runtime/system/system.hpp"
#include <map>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
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
struct MovieFrame;

namespace render_detail
{
class FrameOutput;
class RenderSurface;
class MoviePass;
enum class FramePrepareStatus;
} // namespace render_detail

enum class RenderPrepareStatus;
struct RenderPrepareProgress;

enum class RenderSubmitStatus
{
    submitted,
    deferred,
    failed
};

struct RenderConfig
{
    std::size_t texture_batch_size = 4;
    std::size_t texture_staging_bytes = 64 * 1024 * 1024;
    std::uint32_t prepare_items_per_step = 64;
    double prepare_seconds_per_step = 0.002;
    std::uint64_t upload_bytes_per_step = 8 * 1024 * 1024;
    std::uint16_t cluster_tile_size = 64;
    std::uint16_t cluster_depth_slices = 24;
    std::uint32_t maximum_lights = 256;
    std::uint16_t maximum_cluster_lights = 64;
    std::uint16_t shadow_atlas_size = 4096;
    std::uint16_t local_shadow_tile_size = 512;
    std::uint16_t maximum_local_shadow_faces = 24;
    std::uint8_t maximum_planar_reflections = 2;
    // 1 关闭 MSAA；只接受 1、2、4、8。设备不支持时初始化明确失败。
    std::uint8_t msaa_samples = 1;
    bool max_anisotropy = false;
};

class RenderSystem final : public System
{
public:
    // windows 和可空借用对象须覆盖本系统生命周期；tasks 非空时须先初始化。
    explicit RenderSystem(WindowSystem& windows, WorldSystem* world = nullptr,
                          FontSystem* fonts = nullptr, UiSystem* ui = nullptr,
                          TaskSystem* tasks = nullptr, const RenderConfig& config = {}) noexcept;
    ~RenderSystem() noexcept override;

    // 主线程借用就绪世界及其字体、UI；传入全空解绑，借用须覆盖绑定期间。
    [[nodiscard]] bool bind_world(WorldSystem* world, FontSystem* fonts, UiSystem* ui);

    // 不转移所有权；同一扩展只能属于一个渲染器。回调期间拒绝更改登记。
    [[nodiscard]] bool add_extension(RenderExtension& extension);
    // 解除后清理扩展的场景和 GPU 资源；重复解除返回 false。
    [[nodiscard]] bool remove_extension(RenderExtension& extension) noexcept;

    [[nodiscard]] SceneWorkState prepare_scope(SceneScopeId scope, const SceneWorkBudget& budget,
                                               Camera camera = {});
    [[nodiscard]] bool scope_status(SceneScopeId scope, ScenePreparationSnapshot& output) const;
    void release_scope(SceneScopeId scope) noexcept;
    [[nodiscard]] bool frame_status(SceneScopeId scope, RenderFrameSnapshot& output) const;
    [[nodiscard]] bool request_screenshot(const ScreenshotRequest& request, ScreenshotId& output);
    [[nodiscard]] bool screenshot_status(ScreenshotId id, ScreenshotSnapshot& output) const;
    [[nodiscard]] bool cancel_screenshot(ScreenshotId id);
    [[nodiscard]] bool release_screenshot(ScreenshotId id);
    [[nodiscard]] bool request_diffuse_capture(const SceneCaptureRequest& request);
    [[nodiscard]] SceneCaptureStatus diffuse_capture_status() const noexcept;
    [[nodiscard]] bool take_diffuse_capture(SceneCaptureImage& output);
    void cancel_diffuse_capture() noexcept;
    // 正常场景的线性 HDR 帧，透明合成后、Bloom 和显示映射前；主线程异步使用。
    [[nodiscard]] bool request_hdr_capture();
    [[nodiscard]] SceneCaptureStatus hdr_capture_status() const noexcept;
    [[nodiscard]] bool take_hdr_capture(SceneCaptureImage& output);
    void cancel_hdr_capture() noexcept;
    void release_scene() noexcept;
    [[nodiscard]] std::uint32_t gpu_resource_count() const noexcept;
    // 通用 GPU 生命周期与提交进度；帧号为当前图形设备的 bgfx 提交帧号。
    [[nodiscard]] bool gpu_ready() const noexcept;
    [[nodiscard]] std::uint32_t frame_number() const noexcept;
    // 最近配置的绘制目标像素尺寸；系统或 GPU 未就绪时失败并清零。
    [[nodiscard]] bool viewport_size(Vec2& output) const noexcept;
    // 接受呈现配置请求；下次可提交的 render 应用，GPU 临时关闭期间保留。
    [[nodiscard]] bool set_vsync(bool enabled);
    [[nodiscard]] bool vsync() const noexcept;
    // 主线程提交设备画质请求；失败保持旧值，下一次可绘制帧应用。
    [[nodiscard]] bool set_msaa_samples(std::uint8_t samples);
    [[nodiscard]] std::uint8_t msaa_samples() const noexcept;
    [[nodiscard]] bool supports_msaa_samples(std::uint8_t samples) const noexcept;
    [[nodiscard]] bool set_max_anisotropy(bool enabled);
    [[nodiscard]] bool max_anisotropy() const noexcept;
    [[nodiscard]] bool supports_max_anisotropy() const noexcept;
    // 临时诊断开关；运行属性只能通过所属场景组件修改。
    [[nodiscard]] bool set_debug_feature(RenderFeature feature, bool enabled);
    [[nodiscard]] RenderFeatureState feature_state(RenderFeature feature) const noexcept;
    [[nodiscard]] std::vector<RenderViewStats> submission_stats() const;
    // 仅复制本次调用的数据，随成功场景帧递减；停止或切场景清空。
    [[nodiscard]] bool draw_debug_lines(std::span<const DebugLine> lines,
                                        std::uint16_t lifetime_frames = 1);
    // 只设置无活动场景时的清屏色；场景环境与后处理由对应组件管理。
    void set_idle_clear_color(std::uint32_t rgba) noexcept;
    [[nodiscard]] std::uint32_t idle_clear_color() const noexcept;
    // 返回当前场景组件快照；场景无相应组件时使用固定引擎默认值。
    [[nodiscard]] RenderSettings settings() const noexcept;
    // 主线程调用；开关与场景无关，图形设备临时关闭后保留并恢复。
    void set_stats_visible(bool visible);
    [[nodiscard]] bool stats_visible() const noexcept;
    [[nodiscard]] RenderSubmitStatus render(Camera camera = {});
    [[nodiscard]] RenderSubmitStatus present_movie_frame(const MovieFrame& frame);

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    [[nodiscard]] RenderPrepareStatus prepare_resources(RenderPrepareProgress& output,
                                                        double max_seconds, Camera camera);
    [[nodiscard]] render_detail::FramePrepareStatus prepare_frame();
    friend class RenderExtension;
    void detach_extension(RenderExtension& extension) noexcept;
    void release_extensions_scene() noexcept;
    static void suspend_renderer(void* context) noexcept;
    [[nodiscard]] bool initialize_renderer();
    [[nodiscard]] bool resolve_settings(RenderSettings& settings) const noexcept;
    void shutdown_renderer() noexcept;
    void poll_diffuse_capture();
    void poll_hdr_capture();
    void submit_hdr_capture();
    [[nodiscard]] bool capture_unchanged() const;
    [[nodiscard]] bool draw_diffuse_capture(const RenderSettings& settings);
    void release_draw_resources() noexcept;
    void collect_preparation(bool wait) noexcept;
    [[nodiscard]] bool draw_scene(Camera camera, const RenderSettings& settings);
    [[nodiscard]] bool draw_debug_overlay(Camera camera, const float* view,
                                          const float* projection);
    void publish_features() noexcept;
    void clear_debug_lines() noexcept;

    void record_frame(RenderFrameKind kind);
    struct ScopePreparation
    {
        ScenePreparationSnapshot snapshot;
        std::uint64_t completed = 0;
        std::uint64_t started_ns = 0;
    };
    std::map<SceneScopeId, ScopePreparation> scope_preparations_;
    Scene* scope_preparing_scene_ = nullptr;
    RenderFrameSnapshot last_frame_;
    bool scene_collected_ = false;
    std::map<SceneScopeId, RenderFrameSnapshot> scope_frames_;
    [[nodiscard]] RenderFrameSnapshot submitted_frame(RenderFrameKind kind) const;
    std::uint64_t device_generation_ = 0;
    std::uint64_t submission_sequence_ = 0;
    struct ScreenshotOperation
    {
        ScreenshotRequest request;
        ScreenshotSnapshot snapshot;
        std::string temporary_path;
        bool release_requested = false;
    };
    std::map<std::uint64_t, ScreenshotOperation> screenshots_;
    std::uint64_t screenshot_sequence_ = 0;
    std::uint64_t screenshot_owner_ = 0;
    std::uint64_t active_screenshot_ = 0;
    void advance_screenshots();
    void submit_screenshot(RenderFrameKind kind);
    std::unique_ptr<render_detail::FrameOutput> frame_output_;
    struct SceneGpuResources;
    struct RenderPreparation;
    struct RendererCallbacks;

    WindowSystem& windows_;
    WorldSystem* world_ = nullptr;
    std::unique_ptr<render_detail::MoviePass> movie_pass_;
    FontSystem* fonts_ = nullptr;
    UiSystem* ui_ = nullptr;
    TaskSystem* tasks_ = nullptr;
    RenderConfig config_;
    std::unique_ptr<SceneGpuResources> scene_resources_;
    std::unique_ptr<RenderPreparation> preparation_;
    std::unique_ptr<RendererCallbacks> callbacks_;
    std::uint32_t idle_clear_color_ = 0x181818ff;
    std::unique_ptr<render_detail::RenderSurface> surface_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t frame_number_ = 0;
    SceneCaptureStatus capture_status_ = SceneCaptureStatus::idle;
    SceneCaptureStatus hdr_capture_status_ = SceneCaptureStatus::idle;
    std::uint64_t hdr_capture_scene_id_ = 0;
    SceneCaptureRequest capture_request_;
    std::vector<std::uint8_t> capture_snapshot_;
    bool renderer_initialized_ = false;
    std::array<bool, static_cast<std::size_t>(RenderFeature::count)> debug_requests_{};
    std::array<bool, static_cast<std::size_t>(RenderFeature::count)> feature_applied_{};
    struct DebugLineBatch
    {
        std::vector<DebugLine> lines;
        std::uint16_t frames = 1;
        std::uint64_t scene_id = 0;
    };
    std::vector<DebugLineBatch> debug_lines_;
    bool vsync_enabled_ = true;
    bool vsync_applied_ = true;
    bool max_anisotropy_applied_ = false;
    std::vector<RenderExtension*> extensions_;
    bool invoking_extensions_ = false;
};

} // namespace engine
