#include "runtime/render/passes/clustered_lighting_pass.hpp"

#include "runtime/performance/profiling.hpp"
#include "runtime/render/render_math.hpp"
#include "runtime/render/render_system.hpp"

#include "cs_cluster_cull.bin.h"

#include <SDL3/SDL_log.h>
#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace engine::render_detail
{

static_assert(sizeof(ClusterLight) == sizeof(float) * 16);
static_assert(sizeof(ShadowRecord) == sizeof(float) * 24);

ClusteredLightingPass::~ClusteredLightingPass() noexcept
{
    release();
}

bool ClusteredLightingPass::prepare(const RenderConfig& config, std::uint16_t width,
                                    std::uint16_t height)
{
    const auto* caps = bgfx::getCaps();
    if (caps == nullptr || (caps->supported & BGFX_CAPS_COMPUTE) == 0 ||
        caps->limits.maxComputeBindings < 2 || caps->limits.maxTextureSamplers < 16 || width == 0 ||
        height == 0 || config.cluster_tile_size < 16 || config.cluster_tile_size > 256 ||
        config.cluster_depth_slices == 0 || config.cluster_depth_slices > 64 ||
        config.maximum_lights == 0 || config.maximum_lights > 4096 ||
        config.maximum_cluster_lights == 0 || config.maximum_cluster_lights > 256 ||
        config.maximum_local_shadow_faces > 24)
    {
        return false;
    }
    const auto x = (static_cast<std::uint32_t>(width) + config.cluster_tile_size - 1) /
                   config.cluster_tile_size;
    const auto y = (static_cast<std::uint32_t>(height) + config.cluster_tile_size - 1) /
                   config.cluster_tile_size;
    const std::uint64_t count = std::uint64_t{x} * y * config.cluster_depth_slices;
    const std::uint64_t indices = count * (std::uint64_t{config.maximum_cluster_lights} + 1);
    const std::uint64_t vectors = std::uint64_t{config.maximum_lights} * 4 +
                                  std::uint64_t{config.maximum_local_shadow_faces} * 6;
    // Uniform counters are exactly representable, and buffer byte sizes fit bgfx's API.
    if (count == 0 || count > 16777216 || indices > UINT32_MAX / sizeof(std::uint32_t) ||
        vectors > UINT32_MAX / (sizeof(float) * 4) || (count + 63) / 64 > 65535)
    {
        return false;
    }
    const std::array<float, 4> grid = {static_cast<float>(x), static_cast<float>(y),
                                       static_cast<float>(config.cluster_depth_slices),
                                       static_cast<float>(config.cluster_tile_size)};
    if (grid_ == grid && maximum_lights_ == config.maximum_lights &&
        maximum_cluster_lights_ == config.maximum_cluster_lights &&
        maximum_shadow_faces_ == config.maximum_local_shadow_faces && bgfx::isValid(program_))
    {
        viewport_ = {static_cast<float>(width), static_cast<float>(height), 1.0F / width,
                     1.0F / height};
        return true;
    }
    release();
    bgfx::VertexLayout layout;
    layout.begin().add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float).end();
    lights_ = bgfx::createDynamicVertexBuffer(static_cast<std::uint32_t>(vectors), layout,
                                              BGFX_BUFFER_COMPUTE_READ);
    clusters_ = bgfx::createDynamicIndexBuffer(
        static_cast<std::uint32_t>(indices), BGFX_BUFFER_INDEX32 | BGFX_BUFFER_COMPUTE_READ_WRITE);
    shader_ = bgfx::createShader(bgfx::copy(cs_cluster_cull, sizeof(cs_cluster_cull)));
    if (bgfx::isValid(shader_))
    {
        program_ = bgfx::createProgram(shader_, false);
    }
    grid_uniform_ = bgfx::createUniform("u_cluster_grid", bgfx::UniformType::Vec4);
    camera_uniform_ = bgfx::createUniform("u_cluster_camera", bgfx::UniformType::Vec4);
    counts_uniform_ = bgfx::createUniform("u_cluster_counts", bgfx::UniformType::Vec4);
    viewport_uniform_ = bgfx::createUniform("u_cluster_viewport", bgfx::UniformType::Vec4);
    view_uniform_ = bgfx::createUniform("u_cluster_view", bgfx::UniformType::Mat4);
    inverse_projection_uniform_ =
        bgfx::createUniform("u_cluster_inverse_projection", bgfx::UniformType::Mat4);
    if (!bgfx::isValid(lights_) || !bgfx::isValid(clusters_) || !bgfx::isValid(program_) ||
        !bgfx::isValid(grid_uniform_) || !bgfx::isValid(camera_uniform_) ||
        !bgfx::isValid(counts_uniform_) || !bgfx::isValid(viewport_uniform_) ||
        !bgfx::isValid(view_uniform_) || !bgfx::isValid(inverse_projection_uniform_))
    {
        release();
        return false;
    }
    grid_ = grid;
    viewport_ = {static_cast<float>(width), static_cast<float>(height), 1.0F / width,
                 1.0F / height};
    cluster_count_ = static_cast<std::uint32_t>(count);
    maximum_lights_ = config.maximum_lights;
    maximum_cluster_lights_ = config.maximum_cluster_lights;
    maximum_shadow_faces_ = config.maximum_local_shadow_faces;
    return true;
}

bool ClusteredLightingPass::cull(bgfx::ViewId view, const CameraSettings& camera,
                                 const float* view_matrix, const float* projection,
                                 std::span<const ClusterLight> lights,
                                 std::span<const ShadowRecord> shadows)
{
    ENGINE_PROFILE_ZONE("Render.clusterSubmit");
    ready_ = false;
    if (!bgfx::isValid(program_) || view_matrix == nullptr || projection == nullptr ||
        !finite_matrix(view_matrix) || !finite_matrix(projection) ||
        !std::isfinite(camera.near_clip) || !std::isfinite(camera.far_clip) ||
        camera.near_clip <= 0.0F || camera.far_clip <= camera.near_clip ||
        lights.size() > maximum_lights_ || shadows.size() > maximum_shadow_faces_)
    {
        return false;
    }
    float inverse[16]{};
    bx::mtxInverse(inverse, projection);
    if (!finite_matrix(inverse))
    {
        return false;
    }
    const auto light_bytes = lights.size_bytes();
    const auto shadow_bytes = shadows.size_bytes();
    // An empty scene still uploads a valid record; the compute pass clears every count.
    std::vector<std::uint8_t> bytes(std::max<std::size_t>(16, light_bytes + shadow_bytes));
    if (light_bytes != 0)
    {
        std::memcpy(bytes.data(), lights.data(), light_bytes);
    }
    if (shadow_bytes != 0)
    {
        std::memcpy(bytes.data() + light_bytes, shadows.data(), shadow_bytes);
    }
    bgfx::update(lights_, 0, bgfx::copy(bytes.data(), static_cast<std::uint32_t>(bytes.size())));
    const auto* caps = bgfx::getCaps();
    camera_ = {camera.near_clip, camera.far_clip, caps->homogeneousDepth ? 1.0F : 0.0F,
               caps->originBottomLeft ? 1.0F : 0.0F};
    counts_ = {static_cast<float>(lights.size()), static_cast<float>(maximum_cluster_lights_),
               static_cast<float>(lights.size() * 4), static_cast<float>(cluster_count_)};
    bgfx::setViewName(view, "Clustered lighting");
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
    bgfx::setUniform(grid_uniform_, grid_.data());
    bgfx::setUniform(camera_uniform_, camera_.data());
    bgfx::setUniform(counts_uniform_, counts_.data());
    bgfx::setUniform(viewport_uniform_, viewport_.data());
    bgfx::setUniform(view_uniform_, view_matrix);
    bgfx::setUniform(inverse_projection_uniform_, inverse);
    bgfx::setBuffer(0, lights_, bgfx::Access::Read);
    bgfx::setBuffer(1, clusters_, bgfx::Access::Write);
    bgfx::dispatch(view, program_, (cluster_count_ + 63) / 64);
    ready_ = true;
    return true;
}

void ClusteredLightingPass::bind() const
{
    if (!ready_)
    {
        return;
    }
    bgfx::setUniform(grid_uniform_, grid_.data());
    bgfx::setUniform(camera_uniform_, camera_.data());
    bgfx::setUniform(counts_uniform_, counts_.data());
    bgfx::setBuffer(5, lights_, bgfx::Access::Read);
    bgfx::setBuffer(7, clusters_, bgfx::Access::Read);
}

void ClusteredLightingPass::release() noexcept
{
    if (bgfx::isValid(lights_))
    {
        bgfx::destroy(lights_);
    }
    if (bgfx::isValid(clusters_))
    {
        bgfx::destroy(clusters_);
    }
    if (bgfx::isValid(program_))
    {
        bgfx::destroy(program_);
    }
    if (bgfx::isValid(shader_))
    {
        bgfx::destroy(shader_);
    }
    for (auto uniform : {grid_uniform_, camera_uniform_, counts_uniform_, viewport_uniform_,
                         view_uniform_, inverse_projection_uniform_})
    {
        if (bgfx::isValid(uniform))
        {
            bgfx::destroy(uniform);
        }
    }
    lights_ = BGFX_INVALID_HANDLE;
    clusters_ = BGFX_INVALID_HANDLE;
    program_ = BGFX_INVALID_HANDLE;
    shader_ = BGFX_INVALID_HANDLE;
    grid_uniform_ = camera_uniform_ = counts_uniform_ = view_uniform_ =
        inverse_projection_uniform_ = BGFX_INVALID_HANDLE;
    viewport_uniform_ = BGFX_INVALID_HANDLE;
    grid_ = {};
    cluster_count_ = maximum_lights_ = 0;
    maximum_cluster_lights_ = maximum_shadow_faces_ = 0;
    ready_ = false;
}

std::uint32_t ClusteredLightingPass::resource_count() const noexcept
{
    return static_cast<std::uint32_t>(bgfx::isValid(lights_)) +
           static_cast<std::uint32_t>(bgfx::isValid(clusters_)) +
           static_cast<std::uint32_t>(bgfx::isValid(program_)) +
           static_cast<std::uint32_t>(bgfx::isValid(shader_)) +
           static_cast<std::uint32_t>(bgfx::isValid(grid_uniform_)) +
           static_cast<std::uint32_t>(bgfx::isValid(camera_uniform_)) +
           static_cast<std::uint32_t>(bgfx::isValid(counts_uniform_)) +
           static_cast<std::uint32_t>(bgfx::isValid(viewport_uniform_)) +
           static_cast<std::uint32_t>(bgfx::isValid(view_uniform_)) +
           static_cast<std::uint32_t>(bgfx::isValid(inverse_projection_uniform_));
}

} // namespace engine::render_detail
