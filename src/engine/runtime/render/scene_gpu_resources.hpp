#pragma once

#include "runtime/render/passes/ambient_occlusion_pass.hpp"
#include "runtime/render/passes/atmosphere_pass.hpp"
#include "runtime/render/passes/clustered_lighting_pass.hpp"
#include "runtime/render/passes/debug_draw_pass.hpp"
#include "runtime/render/passes/hdr_post_process_pass.hpp"
#include "runtime/render/passes/planar_reflection_pass.hpp"
#include "runtime/render/passes/screen_space_reflection_pass.hpp"
#include "runtime/render/passes/shadow_pass.hpp"
#include "runtime/render/passes/skinning_pass.hpp"
#include "runtime/render/passes/surface_pass.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/render/resources/gpu_mesh.hpp"
#include "runtime/render/resources/gpu_program.hpp"
#include "runtime/render/resources/gpu_texture.hpp"
#include "runtime/render/scene_draw.hpp"

#include <array>
#include <compare>
#include <deque>
#include <future>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace engine
{

class Scene;
class Resource;

namespace render_detail
{
struct RenderInstanceKey
{
    std::uint64_t world_epoch = 0;
    std::uint64_t entity = 0;
    std::uint64_t component_instance = 0;
    auto operator<=>(const RenderInstanceKey&) const = default;
};

struct TexturePrepareWork
{
    ResourceReference source;
    PreparedTexture part;
    std::future<bool> completion;
    std::uint64_t generation = 0;
    std::uint64_t reserved_bytes = 0;
    std::size_t uploaded = 0;
    std::uint32_t mip = 0;
    std::uint32_t face = 0;
    std::uint64_t byte_offset = 0;
    bool prepared = false;
};
} // namespace render_detail

// 独占当前场景GPU资源；由RenderSystem在bgfx关闭前释放，组件借用不跨帧保存。
struct RenderSystem::SceneGpuResources
{
    std::vector<RenderViewStats> view_stats;
    bool culling_applied = false;
    bool lod_applied = false;
    render_detail::AtmospherePass atmosphere;
    render_detail::DebugDrawPass debug_draw;
    std::map<std::pair<std::uint64_t, std::uint16_t>, std::size_t> selected_lods;
    std::map<std::pair<std::uint64_t, std::uint16_t>, std::uint64_t> selected_lod_revisions;
    std::uint32_t frame_time_stamp = UINT32_MAX;
    std::array<float, 4> frame_time{};
    bgfx::UniformHandle frame_time_uniform = BGFX_INVALID_HANDLE;
    std::vector<render_detail::GpuMesh> meshes;
    std::map<render_detail::RenderInstanceKey, std::size_t> mesh_index;
    std::map<const Resource*, std::weak_ptr<render_detail::GpuGeometry>> geometries;
    render_detail::SkinningPass skinning;
    render_detail::TextureTable textures;
    bgfx::UniformHandle normal_transform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle camera_position = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_direction = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_color = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ambient = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ambient_sky = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ambient_equator = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ambient_ground = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle fog_color = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle fog_range = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle tonemap = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle frame_inputs = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle frame_valid = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle planar_projection = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lighting_params = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle probe_sh = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lightmap_sampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle capture_mode = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle surface = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadow_matrices = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadow_rects = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadow_splits = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadow_params = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadow_settings = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadow_depth = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadow_sampler = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shadow_fallback = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ibl_params = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ibl_irradiance_sampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ibl_prefilter_sampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ibl_lut_sampler = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle ibl_cube_fallback = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle ibl_lut_fallback = BGFX_INVALID_HANDLE;
    std::array<render_detail::GpuTexture*, 3> ibl_textures{};
    std::uint64_t ibl_generation = 0;
    render_detail::ShadowPass shadow;
    std::array<render_detail::ClusteredLightingPass, 4> clusters;
    std::array<render_detail::PlanarReflectionPass, 2> planar;
    render_detail::SurfacePass surface_pass;
    render_detail::ScreenSpaceReflectionPass reflections;
    bgfx::ProgramHandle capture_sky_program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle capture_inverse = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle capture_fullscreen = BGFX_INVALID_HANDLE;
    std::vector<SceneCaptureLightmap> capture_sources;
    bgfx::TextureHandle capture_color = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle capture_specular = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle capture_depth = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle capture_readback = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle capture_framebuffer = BGFX_INVALID_HANDLE;
    std::vector<bgfx::TextureHandle> capture_lightmaps;
    std::vector<std::unique_ptr<render_detail::GpuGeometry>> capture_meshes;
    std::vector<float> capture_pixels;
    std::uint32_t capture_ready_frame = 0;
    std::uint16_t capture_width = 0, capture_height = 0;
    bool capture_pending = false;
    bool capture_cancelled = false;
    bgfx::TextureHandle hdr_capture_readback = BGFX_INVALID_HANDLE;
    std::vector<std::uint16_t> hdr_capture_pixels;
    std::uint32_t hdr_capture_ready_frame = 0;
    std::uint16_t hdr_capture_width = 0, hdr_capture_height = 0;
    bool hdr_capture_pending = false;
    render_detail::AmbientOcclusionPass ambient_occlusion;
    render_detail::HdrPostProcessPass hdr_post_process;
    bgfx::UniformHandle ao_settings = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ao_sampler = BGFX_INVALID_HANDLE;
    std::unordered_map<const schema::ShaderDataT*, render_detail::GpuProgram> programs;

    SceneGpuResources() = default;
    ~SceneGpuResources() noexcept;
    SceneGpuResources(const SceneGpuResources&) = delete;
    SceneGpuResources& operator=(const SceneGpuResources&) = delete;
    SceneGpuResources(SceneGpuResources&&) = delete;
    SceneGpuResources& operator=(SceneGpuResources&&) = delete;

    [[nodiscard]] bool initialize();
    [[nodiscard]] bool prepare_lighting(const WorldSystem& world, const RenderSettings& settings,
                                        const RenderConfig& config,
                                        render_detail::SceneDrawView& draw,
                                        const CameraSettings& lens,
                                        render_detail::ShadowFrame& frame,
                                        std::size_t cluster_index, bgfx::ViewId base,
                                        const SceneCaptureRequest* capture);
    [[nodiscard]] bool draw_meshes(const WorldSystem& world, const RenderSettings& settings,
                                   const render_detail::SceneDrawView& view,
                                   render_detail::SceneDrawMode mode);
    [[nodiscard]] bool prepare_capture(const SceneCaptureRequest& request);
    void release_capture() noexcept;
    [[nodiscard]] bool draw_capture_sky(const SceneCaptureRequest& request,
                                        const RenderSettings& settings);
    void release() noexcept;
    [[nodiscard]] std::uint32_t resource_count() const noexcept;
    [[nodiscard]] bool collect_ibl(const Scene* scene, RenderPreparation& preparation);
};

struct RenderSystem::RenderPreparation
{
    std::uint64_t generation = 0;
    RenderPrepareStatus status = RenderPrepareStatus::idle;
    RenderPrepareProgress progress;
    std::string error;
    WorldSystem* observed_world = nullptr;
    std::uint64_t world_epoch = 0;
    std::uint64_t camera_instance = 0;
    std::vector<std::uint64_t> observers;
    std::vector<std::uint64_t> scenes;
    std::set<std::uint64_t> dirty_nodes;
    bool discover = true;
    bool ibl_dirty = true;
    std::uint64_t staging_bytes = 0;
    std::size_t active_jobs = 0;
    std::map<const Resource*, std::shared_ptr<render_detail::TexturePrepareWork>> textures;
    std::set<const Resource*> ready_textures;
    std::map<const Resource*, std::size_t> texture_users;
    std::map<const schema::ShaderDataT*, std::size_t> program_users;
    std::set<const Resource*> unused_textures;
    std::set<const schema::ShaderDataT*> unused_programs;
    std::set<const schema::ShaderDataT*> pending_programs;
    std::array<const Resource*, 3> ibl_sources{};
    std::vector<std::shared_ptr<render_detail::TexturePrepareWork>> retired_work;
    std::vector<std::unique_ptr<SceneGpuResources>> retired_scenes;

    [[nodiscard]] bool request_texture(ResourceLoader* loader, ResourceHandle source);
    void detach_observers() noexcept;
};

} // namespace engine
