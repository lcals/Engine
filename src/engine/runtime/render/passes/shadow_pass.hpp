#pragma once

#include "runtime/render/passes/shadow_frame.hpp"
#include "runtime/render/resources/gpu_mesh.hpp"
#include "runtime/render/resources/gpu_texture.hpp"

#include <bgfx/bgfx.h>

#include <span>
#include <vector>

namespace engine
{
class WorldSystem;
class Material;
struct RenderConfig;
} // namespace engine

namespace engine::render_detail
{

class ShadowPass final
{
public:
    ShadowPass() = default;
    ~ShadowPass() noexcept;
    ShadowPass(const ShadowPass&) = delete;
    ShadowPass& operator=(const ShadowPass&) = delete;
    ShadowPass(ShadowPass&&) = delete;
    ShadowPass& operator=(ShadowPass&&) = delete;

    [[nodiscard]] bool prepare(std::uint16_t size);
    [[nodiscard]] bool draw(const WorldSystem& world, const RenderSettings& settings,
                            ShadowFrame& frame, const std::vector<GpuMesh>& meshes,
                            const std::vector<GpuTexture>& textures, bgfx::TextureHandle fallback,
                            bgfx::UniformHandle depth_uniform, const RenderConfig& config,
                            std::span<const LocalShadowLight> local_lights,
                            std::span<ClusterLight> lights, const float* camera_world,
                            bgfx::ViewId base_view,
                            const std::span<const std::uint64_t>* caster_ids = nullptr);
    [[nodiscard]] bgfx::TextureHandle texture() const noexcept;
    [[nodiscard]] std::uint32_t resource_count() const noexcept;
    void release() noexcept;

private:
    [[nodiscard]] bool bind_material(const Material& material,
                                     const std::vector<GpuTexture>& textures,
                                     bgfx::TextureHandle fallback);

    bgfx::TextureHandle color_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle depth_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle framebuffer_ = BGFX_INVALID_HANDLE;
    bgfx::ShaderHandle vertex_ = BGFX_INVALID_HANDLE;
    bgfx::ShaderHandle fragment_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle alpha_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle alpha_sampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uv_transform_ = BGFX_INVALID_HANDLE;
    std::uint16_t resolution_ = 0;
    std::vector<bgfx::ViewId> attached_views_;
    std::uint32_t last_skipped_lights_ = 0;
};

} // namespace engine::render_detail
