#include "runtime/render/passes/debug_draw_pass.hpp"

#include "runtime/render/render_color.hpp"
#include "runtime/render/render_constants.hpp"
#include "runtime/render/resources/gpu_mesh.hpp"
#include "runtime/render/resources/gpu_vertex.hpp"
#include "runtime/resource/resource.hpp"
#include "runtime/scene/camera_math.hpp"
#include "runtime/scene/ecs_scene.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/world/world_system.hpp"

#include <bx/bounds.h>
#include <bx/math.h>
#include <debugdraw/debugdraw.h>

#include "fs_debug_wireframe.bin.h"
#include "vs_debug_wireframe.bin.h"

#include <algorithm>
#include <limits>

namespace engine::render_detail
{
namespace
{
constexpr auto feature_count = static_cast<std::size_t>(RenderFeature::count);

std::uint32_t linear_abgr(std::uint32_t rgba)
{
    const auto channel = [](std::uint32_t value)
    { return static_cast<std::uint32_t>(255.0F * display_srgb_to_linear(value / 255.0F) + 0.5F); };
    return ((rgba & 255U) << 24U) | (channel((rgba >> 8U) & 255U) << 16U) |
           (channel((rgba >> 16U) & 255U) << 8U) | channel(rgba >> 24U);
}
} // namespace

DebugDrawPass::~DebugDrawPass() noexcept
{
    release();
}

bool DebugDrawPass::supports_wireframe() noexcept
{
    const auto* caps = bgfx::getCaps();
    return caps != nullptr && (caps->supported & BGFX_CAPS_COMPUTE) != 0 &&
           caps->limits.maxComputeBindings >= 2;
}

bool DebugDrawPass::prepare(bool wireframe)
{
    if (!initialized_)
    {
        // Upstream 29-debugdraw owns its shared geometry and shader resources.
        ddInit();
        initialized_ = true;
    }
    if (!wireframe || bgfx::isValid(wireframe_))
    {
        return true;
    }
    if (!supports_wireframe())
    {
        return false;
    }
    const auto vertex =
        bgfx::createShader(bgfx::copy(vs_debug_wireframe, sizeof(vs_debug_wireframe)));
    const auto fragment =
        bgfx::createShader(bgfx::copy(fs_debug_wireframe, sizeof(fs_debug_wireframe)));
    if (bgfx::isValid(vertex) && bgfx::isValid(fragment))
    {
        wireframe_ = bgfx::createProgram(vertex, fragment, false);
    }
    if (bgfx::isValid(vertex))
    {
        bgfx::destroy(vertex);
    }
    if (bgfx::isValid(fragment))
    {
        bgfx::destroy(fragment);
    }
    parameters_ = bgfx::createUniform("u_debug_wire_parameters", bgfx::UniformType::Vec4);
    color_ = bgfx::createUniform("u_debug_wire_color", bgfx::UniformType::Vec4);
    if (!bgfx::isValid(wireframe_) || !bgfx::isValid(parameters_) || !bgfx::isValid(color_))
    {
        release();
        return false;
    }
    return true;
}

bool DebugDrawPass::draw(const WorldSystem& world, Camera camera, std::span<const GpuMesh> meshes,
                         bgfx::FrameBufferHandle framebuffer, std::uint16_t width,
                         std::uint16_t height, const float* view, const float* projection,
                         const std::array<bool, feature_count>& features,
                         std::span<const DebugLine> lines, std::span<const std::size_t> lod_levels)
{
    const auto enabled = [&](RenderFeature feature)
    { return features[static_cast<std::size_t>(feature)]; };
    const bool wireframe = enabled(RenderFeature::wireframe) && supports_wireframe();
    if (!wireframe && !enabled(RenderFeature::mesh_bounds) &&
        !enabled(RenderFeature::camera_frusta) &&
        (!enabled(RenderFeature::debug_draw) || lines.empty()))
    {
        return true;
    }
    if (!prepare(wireframe))
    {
        return false;
    }
    bgfx::setViewName(debug_view, "Scene diagnostics");
    bgfx::setViewFrameBuffer(debug_view, framebuffer);
    bgfx::setViewRect(debug_view, 0, 0, width, height);
    bgfx::setViewClear(debug_view, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(debug_view, view, projection);
    bgfx::setViewMode(debug_view, bgfx::ViewMode::Sequential);
    DebugDrawEncoder encoder;
    encoder.begin(debug_view);
    encoder.setWireframe(true);
    encoder.setState(true, false, false);
    for (const auto& mesh : meshes)
    {
        const auto node = world.find_node(mesh.node_id);
        const auto renderer = node.get_component<MeshRenderer>();
        const auto transform = node.get_component<Transform>();
        std::array<float, 16> matrix{};
        if (!renderer || renderer.instance_id() != mesh.instance_id || !renderer.active() ||
            !renderer.visible_in_hierarchy() || !camera.mesh_visible(mesh.instance_id) ||
            !transform || !transform.world_matrix(matrix.data()) || !mesh.geometry)
        {
            continue;
        }
        if (enabled(RenderFeature::mesh_bounds))
        {
            encoder.setColor(linear_abgr(0x50ff90ff));
            encoder.setTransform(matrix.data());
            encoder.draw(bx::Aabb{{mesh.minimum[0], mesh.minimum[1], mesh.minimum[2]},
                                  {mesh.maximum[0], mesh.maximum[1], mesh.maximum[2]}});
            submitted_[static_cast<std::size_t>(RenderFeature::mesh_bounds)] = true;
        }
        if (wireframe)
        {
            const auto offset = static_cast<std::size_t>(&mesh - meshes.data());
            const auto level = offset < lod_levels.size() ? lod_levels[offset] : 0;
            const auto count = mesh.geometry->index_count(level);
            const auto indices = mesh.geometry->index_buffer(level);
            if (!bgfx::isValid(indices) || count == 0 ||
                count > std::numeric_limits<std::uint32_t>::max() / 6U)
            {
                continue;
            }
            const float parameters[] = {static_cast<float>(sizeof(GpuVertex) / sizeof(float)), 1.1F,
                                        0, 0};
            const float color[] = {0.1F, 0.8F, 1.0F, 0.9F};
            bgfx::setUniform(parameters_, parameters);
            bgfx::setUniform(color_, color);
            bgfx::setTransform(matrix.data());
            if (bgfx::isValid(mesh.deformed_vertices))
            {
                bgfx::setBuffer(0, mesh.deformed_vertices, bgfx::Access::Read);
            }
            else
            {
                bgfx::setBuffer(0, mesh.geometry->vertices, bgfx::Access::Read);
            }
            bgfx::setBuffer(1, indices, bgfx::Access::Read);
            bgfx::setVertexCount(count * 6U);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LEQUAL |
                           BGFX_STATE_BLEND_ALPHA | BGFX_STATE_MSAA);
            bgfx::submit(debug_view, wireframe_);
            submitted_[static_cast<std::size_t>(RenderFeature::wireframe)] = true;
        }
    }
    encoder.setTransform(nullptr);
    if (enabled(RenderFeature::camera_frusta))
    {
        auto query = flecs::world(world.native_world())
                         .query_builder<const ecs_detail::ComponentStorage<Camera::Data>>()
                         .with<ecs_detail::Published>()
                         .build();
        query.each(
            [&](flecs::entity entity, const auto&)
            {
                const auto node = world.find_node(entity.id());
                const auto target = node.get_component<Camera>();
                const auto transform = node.get_component<Transform>();
                float model[16]{}, observer[16]{}, lens[16]{}, combined[16]{};
                CameraSettings settings;
                if (target.get_settings(settings) && target.active() &&
                    target.visible_in_hierarchy() && transform && transform.world_matrix(model) &&
                    camera_detail::view_matrix(model, observer) &&
                    camera_detail::projection_matrix(settings, static_cast<double>(width) / height,
                                                     bgfx::getCaps()->homogeneousDepth, lens))
                {
                    bx::mtxMul(combined, observer, lens);
                    encoder.setColor(linear_abgr(0xffc050ff));
                    encoder.drawFrustum(combined);
                    submitted_[static_cast<std::size_t>(RenderFeature::camera_frusta)] = true;
                }
            });
    }
    if (enabled(RenderFeature::debug_draw))
    {
        for (const auto& line : lines)
        {
            encoder.setState(line.depth_test, false, false);
            encoder.setColor(linear_abgr(line.rgba));
            encoder.moveTo(line.start.x(), line.start.y(), line.start.z());
            encoder.lineTo(line.end.x(), line.end.y(), line.end.z());
        }
        submitted_[static_cast<std::size_t>(RenderFeature::debug_draw)] = !lines.empty();
    }
    encoder.end();
    return true;
}

void DebugDrawPass::reset_frame() noexcept
{
    submitted_.fill(false);
}

bool DebugDrawPass::submitted(RenderFeature feature) const noexcept
{
    const auto index = static_cast<std::size_t>(feature);
    return index < submitted_.size() && submitted_[index];
}

std::uint32_t DebugDrawPass::resource_count() const noexcept
{
    // Fixed upstream ddInit creates seven programs, two uniforms, one atlas and two buffers.
    // This integration never creates upstream sprites or custom geometry handles.
    return (initialized_ ? 12U : 0U) + static_cast<std::uint32_t>(bgfx::isValid(wireframe_)) +
           static_cast<std::uint32_t>(bgfx::isValid(parameters_)) +
           static_cast<std::uint32_t>(bgfx::isValid(color_));
}

void DebugDrawPass::release() noexcept
{
    if (bgfx::isValid(wireframe_))
    {
        bgfx::destroy(wireframe_);
    }
    if (bgfx::isValid(parameters_))
    {
        bgfx::destroy(parameters_);
    }
    if (bgfx::isValid(color_))
    {
        bgfx::destroy(color_);
    }
    wireframe_ = BGFX_INVALID_HANDLE;
    parameters_ = BGFX_INVALID_HANDLE;
    color_ = BGFX_INVALID_HANDLE;
    if (initialized_)
    {
        ddShutdown();
    }
    initialized_ = false;
    reset_frame();
}
} // namespace engine::render_detail
