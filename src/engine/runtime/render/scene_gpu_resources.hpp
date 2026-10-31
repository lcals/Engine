#pragma once

#include "runtime/render/passes/ambient_occlusion_pass.hpp"
#include "runtime/render/passes/hdr_post_process_pass.hpp"
#include "runtime/render/passes/shadow_pass.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/render/resources/gpu_mesh.hpp"
#include "runtime/render/resources/gpu_program.hpp"
#include "runtime/render/resources/gpu_texture.hpp"

#include <array>
#include <vector>

namespace engine
{

class Scene;

// 独占当前场景GPU资源；由RenderSystem在bgfx关闭前释放，组件借用不跨帧保存。
struct RenderSystem::SceneGpuResources
{
    std::vector<render_detail::GpuMesh> meshes;
    std::vector<render_detail::GpuTexture> textures;
    bgfx::UniformHandle normal_transform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle camera_position = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_direction = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_color = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_count = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_position_range = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_direction_outer = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_color_intensity = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_kind_inner = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ambient = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ambient_sky = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ambient_equator = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ambient_ground = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle fog_color = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle fog_range = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle tonemap = BGFX_INVALID_HANDLE;
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
    std::array<render_detail::GpuTexture, 3> ibl_textures;
    std::uint64_t ibl_generation = 0;
    render_detail::ShadowPass shadow;
    render_detail::AmbientOcclusionPass ambient_occlusion;
    render_detail::HdrPostProcessPass hdr_post_process;
    bgfx::UniformHandle ao_settings = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ao_sampler = BGFX_INVALID_HANDLE;
    std::vector<render_detail::GpuProgram> programs;

    SceneGpuResources() = default;
    ~SceneGpuResources() noexcept;
    SceneGpuResources(const SceneGpuResources&) = delete;
    SceneGpuResources& operator=(const SceneGpuResources&) = delete;
    SceneGpuResources(SceneGpuResources&&) = delete;
    SceneGpuResources& operator=(SceneGpuResources&&) = delete;

    [[nodiscard]] bool initialize();
    void release() noexcept;
    [[nodiscard]] std::uint32_t resource_count() const noexcept;
    [[nodiscard]] bool prepare_ibl(const Scene& scene);
};

} // namespace engine
