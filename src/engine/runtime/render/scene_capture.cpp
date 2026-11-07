#include "runtime/render/render_constants.hpp"
#include "runtime/render/render_math.hpp"
#include "runtime/render/scene_gpu_resources.hpp"
#include "runtime/resource/codecs/texture_resource_codec.hpp"
#include "runtime/resource/resource.hpp"
#include "runtime/scene/ecs_scene.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/utils/mesh_utils.hpp"
#include "runtime/world/world_system.hpp"
#include <algorithm>
#include <bx/math.h>
#include <cmath>
#include <set>
#include <utility>

#include "fs_capture_sky.bin.h"
#include "vs_hdr.bin.h"

namespace engine
{
bool RenderSystem::request_hdr_capture()
{
    const auto* scene = world_ ? world_->scene() : nullptr;
    if (!is_initialized() || !renderer_initialized_ || !scene ||
        hdr_capture_status_ == SceneCaptureStatus::pending ||
        hdr_capture_status_ == SceneCaptureStatus::complete ||
        (scene_resources_ && scene_resources_->hdr_capture_pending))
    {
        return false;
    }
    hdr_capture_scene_id_ = scene->id();
    hdr_capture_status_ = SceneCaptureStatus::pending;
    return true;
}

SceneCaptureStatus RenderSystem::hdr_capture_status() const noexcept
{
    return hdr_capture_status_;
}

void RenderSystem::cancel_hdr_capture() noexcept
{
    if (hdr_capture_status_ != SceneCaptureStatus::pending &&
        hdr_capture_status_ != SceneCaptureStatus::complete)
    {
        return;
    }
    hdr_capture_status_ = SceneCaptureStatus::cancelled;
    hdr_capture_scene_id_ = 0;
    // bgfx still owns the destination pointer until its returned frame. Never
    // resize or clear the backing buffer while a readback is in flight.
    if (scene_resources_ && !scene_resources_->hdr_capture_pending)
    {
        scene_resources_->hdr_capture_pixels.clear();
    }
}

void RenderSystem::submit_hdr_capture()
{
    if (hdr_capture_status_ != SceneCaptureStatus::pending || !scene_resources_ ||
        scene_resources_->hdr_capture_pending)
    {
        return;
    }
    const auto* scene = world_ ? world_->scene() : nullptr;
    if (!scene || scene->id() != hdr_capture_scene_id_)
    {
        cancel_hdr_capture();
        return;
    }
    auto& resources = *scene_resources_;
    const auto* caps = bgfx::getCaps();
    constexpr auto readback_flags = BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK;
    const auto source = resources.hdr_post_process.scene_color();
    if (!caps || !bgfx::isValid(source) || width_ == 0 || height_ == 0 || width_ > UINT16_MAX ||
        height_ > UINT16_MAX ||
        !bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::RGBA16F, readback_flags))
    {
        hdr_capture_status_ = SceneCaptureStatus::failed;
        return;
    }
    resources.hdr_capture_width = static_cast<std::uint16_t>(width_);
    resources.hdr_capture_height = static_cast<std::uint16_t>(height_);
    resources.hdr_capture_readback =
        bgfx::createTexture2D(resources.hdr_capture_width, resources.hdr_capture_height, false, 1,
                              bgfx::TextureFormat::RGBA16F, readback_flags);
    if (!bgfx::isValid(resources.hdr_capture_readback))
    {
        hdr_capture_status_ = SceneCaptureStatus::failed;
        return;
    }
    resources.hdr_capture_pixels.resize(static_cast<std::size_t>(width_) * height_ * 4);
    // bgfx resolves multisample attachments when the drawing view ends. The
    // capture view follows transparent/extension and final display resolve;
    // display resolve samples but does not modify this linear scene texture.
    const bgfx::TextureRegion readback{.handle = resources.hdr_capture_readback};
    bgfx::blit(render_detail::hdr_capture_view, readback, bgfx::TextureRegion{.handle = source});
    resources.hdr_capture_ready_frame = bgfx::read(readback, resources.hdr_capture_pixels.data());
    resources.hdr_capture_pending = true;
}

void RenderSystem::poll_hdr_capture()
{
    const auto* scene = world_ ? world_->scene() : nullptr;
    if ((hdr_capture_status_ == SceneCaptureStatus::pending ||
         hdr_capture_status_ == SceneCaptureStatus::complete) &&
        (!scene || scene->id() != hdr_capture_scene_id_))
    {
        cancel_hdr_capture();
    }
    if (!scene_resources_ || !scene_resources_->hdr_capture_pending ||
        static_cast<std::int32_t>(frame_number_ - scene_resources_->hdr_capture_ready_frame) < 0)
    {
        return;
    }
    auto& resources = *scene_resources_;
    resources.hdr_capture_pending = false;
    bgfx::destroy(resources.hdr_capture_readback);
    resources.hdr_capture_readback = BGFX_INVALID_HANDLE;
    if (hdr_capture_status_ == SceneCaptureStatus::pending)
    {
        hdr_capture_status_ = SceneCaptureStatus::complete;
    }
    else
    {
        resources.hdr_capture_pixels.clear();
    }
}

bool RenderSystem::take_hdr_capture(SceneCaptureImage& output)
{
    output = {};
    const auto* scene = world_ ? world_->scene() : nullptr;
    if (hdr_capture_status_ == SceneCaptureStatus::complete &&
        (!scene || scene->id() != hdr_capture_scene_id_))
    {
        cancel_hdr_capture();
    }
    if (hdr_capture_status_ != SceneCaptureStatus::complete || !scene_resources_)
    {
        return false;
    }
    auto& resources = *scene_resources_;
    output.width = resources.hdr_capture_width;
    output.height = resources.hdr_capture_height;
    output.rgba.resize(resources.hdr_capture_pixels.size());
    const std::size_t row = static_cast<std::size_t>(output.width) * 4;
    const bool flip = bgfx::getCaps()->originBottomLeft;
    for (std::size_t y = 0; y < output.height; ++y)
    {
        const auto source_row = flip ? output.height - 1 - y : y;
        for (std::size_t x = 0; x < row; ++x)
        {
            output.rgba[y * row + x] =
                bx::halfToFloat(resources.hdr_capture_pixels[source_row * row + x]);
        }
    }
    resources.hdr_capture_pixels.clear();
    hdr_capture_scene_id_ = 0;
    hdr_capture_status_ = SceneCaptureStatus::idle;
    return true;
}

namespace
{
void append_value(std::vector<std::uint8_t>& bytes, const auto& value)
{
    const auto* begin = reinterpret_cast<const std::uint8_t*>(&value);
    bytes.insert(bytes.end(), begin, begin + sizeof(value));
}

bool append_component(std::vector<std::uint8_t>& bytes, const Component& component)
{
    flatbuffers::FlatBufferBuilder builder;
    schema::ComponentDataT data;
    if (!component.get_data(data))
    {
        return false;
    }
    builder.Finish(schema::CreateComponentData(builder, &data));
    append_value(bytes, builder.GetSize());
    bytes.insert(bytes.end(), builder.GetBufferPointer(),
                 builder.GetBufferPointer() + builder.GetSize());
    return true;
}

bool capture_snapshot(const WorldSystem& world, const SceneCaptureRequest& request,
                      std::vector<std::uint8_t>& output)
{
    output.clear();
    if (!world.scene())
    {
        return false;
    }
    append_value(output, world.scene()->id());
    const auto append_node = [&](std::uint64_t id)
    {
        const auto node = world.find_node(id);
        const auto transform = node ? node.get_component<Transform>() : Transform{};
        std::array<float, 16> matrix{};
        if (!node || !transform || !transform.world_matrix(matrix.data()))
        {
            return false;
        }
        append_value(output, id);
        append_value(output, matrix);
        return true;
    };
    for (const auto id : request.node_ids)
    {
        if (!append_node(id))
        {
            return false;
        }
        const auto renderer = world.find_node(id).get_component<MeshRenderer>();
        const auto* mesh = renderer ? renderer.mesh_data() : nullptr;
        if (!renderer || !renderer.active() || !renderer.visible_in_hierarchy() || !mesh)
        {
            return false;
        }
        if (!append_component(output, renderer))
        {
            return false;
        }
        append_value(output, renderer.instance_id());
        append_value(output, renderer.mesh_revision());
        append_value(output, renderer.deformation_revision());
        for (std::size_t slot = 0; slot < renderer.material_count(); ++slot)
        {
            const auto material = renderer.material(slot);
            const auto* shader = material ? material.shader_data() : nullptr;
            if (!shader)
            {
                return false;
            }
            append_value(output, reinterpret_cast<std::uintptr_t>(shader));
            append_value(output, material.cull_mode());
            append_value(output, material.blend_mode());
            for (const auto& parameter : shader->parameters)
            {
                if (!parameter)
                {
                    return false;
                }
                // Non-exposed values belong to the immutable Shader resource,
                // whose identity is already part of the snapshot.
                if (!parameter->exposed)
                {
                    continue;
                }
                float scalar{};
                Vec2 pair{};
                Vec3 triple{};
                Vec4 quad{};
                if (material.get_float(parameter->name, scalar))
                {
                    append_value(output, scalar);
                }
                else if (material.get_v2(parameter->name, pair))
                {
                    append_value(output, pair);
                }
                else if (material.get_v3(parameter->name, triple))
                {
                    append_value(output, triple);
                }
                else if (material.get_v4(parameter->name, quad))
                {
                    append_value(output, quad);
                }
                else
                {
                    return false;
                }
            }
            for (const auto& sampler : shader->samplers)
            {
                if (!sampler)
                {
                    return false;
                }
                const auto handle = material.texture(sampler->name);
                append_value(output, handle.owner);
                append_value(output, handle.id);
                append_value(output, handle.generation);
            }
        }
    }
    for (const auto id : request.light_node_ids)
    {
        if (!append_node(id))
        {
            return false;
        }
        const auto light = world.find_node(id).get_component<Light>();
        if (!light || !light.active() || !light.visible_in_hierarchy())
        {
            return false;
        }
        if (!append_component(output, light))
        {
            return false;
        }
    }
    std::uint64_t atmosphere_sun_id = 0;
    bool complete = true;
    auto environments =
        flecs::world(world.native_world())
            .query_builder<const ecs_detail::ComponentStorage<EnvironmentComponent::Data>,
                           const ecs_detail::NodeRuntime>()
            .with<ecs_detail::Published>()
            .build();
    environments.each(
        [&](flecs::entity entity, const auto& stored, const ecs_detail::NodeRuntime& state)
        {
            if (state.scene == world.scene() && state.active && state.visible_in_hierarchy &&
                stored.state.flags.enabled && stored.state.flags.visible)
            {
                if (!append_component(
                        output, world.find_node(entity.id()).get_component<EnvironmentComponent>()))
                {
                    complete = false;
                    return;
                }
                if (stored.data.atmosphere_enabled)
                {
                    atmosphere_sun_id = stored.data.atmosphere_sun_node_id;
                }
            }
        });
    if (!complete)
    {
        return false;
    }
    if (atmosphere_sun_id != 0)
    {
        bool found_sun = false;
        auto suns =
            flecs::world(world.native_world())
                .query_builder<const ecs_detail::ComponentStorage<Light::Data>,
                               const schema::NodePropertiesT, const ecs_detail::NodeRuntime>()
                .with<ecs_detail::Published>()
                .build();
        suns.each(
            [&](flecs::entity entity, const auto& stored, const auto& properties,
                const ecs_detail::NodeRuntime& state)
            {
                if (state.scene != world.scene() || properties.id != atmosphere_sun_id)
                {
                    return;
                }
                const auto light = world.find_node(entity.id()).get_component<Light>();
                found_sun = state.active && state.visible_in_hierarchy &&
                            stored.state.flags.enabled && stored.state.flags.visible &&
                            stored.data.kind == schema::LightKind::directional &&
                            append_node(entity.id());
                if (found_sun)
                {
                    found_sun = append_component(output, light);
                }
            });
        if (!found_sun)
        {
            return false;
        }
    }
    return true;
}
} // namespace

bool RenderSystem::draw_diffuse_capture(const RenderSettings& rendering)
{
    if (capture_status_ != SceneCaptureStatus::pending)
    {
        return true;
    }
    const auto fail = [&]()
    {
        capture_status_ = SceneCaptureStatus::failed;
        capture_request_ = {};
        capture_snapshot_.clear();
        return false;
    };
    if (!capture_unchanged())
    {
        cancel_diffuse_capture();
        return true;
    }
    if (!scene_resources_)
    {
        return fail();
    }
    auto& resources = *scene_resources_;
    if (capture_status_ == SceneCaptureStatus::pending && !resources.capture_pending)
    {
        auto& request = capture_request_;
        if (!resources.prepare_capture(request))
        {
            resources.release_capture();
            return fail();
        }
        float capture_world[16];
        bx::mtxInverse(capture_world, request.view.data());
        render_detail::SceneDrawView draw;
        draw.renderer = this;
        draw.id = render_detail::capture_view;
        draw.width = request.width;
        draw.height = request.height;
        draw.world = capture_world;
        draw.view = request.view.data();
        draw.projection = request.projection.data();
        draw.capture = &request;
        CameraSettings lens;
        if (!render_detail::projection_camera_settings(
                request.projection.data(), static_cast<float>(request.width) / request.height,
                bgfx::getCaps()->homogeneousDepth, lens))
        {
            return fail();
        }
        render_detail::ShadowFrame capture_shadow;
        if (!resources.prepare_lighting(*world_, rendering, config_, draw, lens, capture_shadow, 3,
                                        render_detail::capture_shadow_view, &request))
        {
            return fail();
        }
        bgfx::setViewName(draw.id, "Diffuse lighting capture");
        bgfx::setViewFrameBuffer(draw.id, resources.capture_framebuffer);
        bgfx::setViewRect(draw.id, 0, 0, draw.width, draw.height);
        bgfx::setViewMode(draw.id, bgfx::ViewMode::Sequential);
        bgfx::setViewTransform(draw.id, draw.view, draw.projection);
        bgfx::setViewClear(draw.id, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, std::uint32_t{0x000000ff},
                           1.0F, std::uint8_t{0});
        bgfx::touch(draw.id);
        if (!resources.draw_capture_sky(request, rendering) ||
            !resources.draw_meshes(*world_, rendering, draw, render_detail::SceneDrawMode::capture))
        {
            return fail();
        }
        const bgfx::TextureRegion readback{.handle = resources.capture_readback};
        bgfx::blit(render_detail::capture_copy_view, readback,
                   bgfx::TextureRegion{.handle = resources.capture_color});
        resources.capture_ready_frame = bgfx::read(readback, resources.capture_pixels.data());
        resources.capture_pending = true;
    }
    return true;
}

bool RenderSystem::request_diffuse_capture(const SceneCaptureRequest& request)
{
    if (!renderer_initialized_ || !world_ || !world_->scene() ||
        capture_status_ == SceneCaptureStatus::pending || request.width == 0 ||
        request.height == 0 || request.width > bgfx::getCaps()->limits.maxTextureSize ||
        request.height > bgfx::getCaps()->limits.maxTextureSize ||
        !render_detail::finite_matrix(request.view.data()) ||
        !render_detail::finite_matrix(request.projection.data()))
    {
        return false;
    }
    float inverse[16];
    bx::mtxInverse(inverse, request.view.data());
    if (!render_detail::finite_matrix(inverse))
    {
        return false;
    }
    bx::mtxInverse(inverse, request.projection.data());
    if (!render_detail::finite_matrix(inverse))
    {
        return false;
    }
    CameraSettings lens;
    if (!render_detail::projection_camera_settings(
            request.projection.data(), static_cast<float>(request.width) / request.height,
            bgfx::getCaps()->homogeneousDepth, lens) ||
        std::set<std::uint64_t>(request.node_ids.begin(), request.node_ids.end()).size() !=
            request.node_ids.size() ||
        std::set<std::uint64_t>(request.light_node_ids.begin(), request.light_node_ids.end())
                .size() != request.light_node_ids.size())
    {
        return false;
    }
    std::set<std::uint64_t> overrides;
    for (const auto& item : request.indirect)
    {
        if (!item.image || !overrides.insert(item.node_id).second ||
            std::find(request.node_ids.begin(), request.node_ids.end(), item.node_id) ==
                request.node_ids.end())
        {
            return false;
        }
    }
    std::vector<std::uint8_t> snapshot;
    if (!capture_snapshot(*world_, request, snapshot))
    {
        return false;
    }
    capture_request_ = request;
    capture_snapshot_ = std::move(snapshot);
    capture_status_ = SceneCaptureStatus::pending;
    if (scene_resources_)
    {
        scene_resources_->capture_cancelled = false;
    }
    return true;
}

SceneCaptureStatus RenderSystem::diffuse_capture_status() const noexcept
{
    return capture_status_;
}

bool RenderSystem::capture_unchanged() const
{
    std::vector<std::uint8_t> current;
    return world_ && capture_snapshot(*world_, capture_request_, current) &&
           current == capture_snapshot_;
}

void RenderSystem::poll_diffuse_capture()
{
    if (capture_status_ != SceneCaptureStatus::pending || !scene_resources_ ||
        !scene_resources_->capture_pending ||
        static_cast<std::int32_t>(frame_number_ - scene_resources_->capture_ready_frame) < 0)
    {
        return;
    }
    auto& resources = *scene_resources_;
    resources.capture_pending = false;
    if (resources.capture_cancelled || !capture_unchanged())
    {
        capture_request_ = {};
        capture_snapshot_.clear();
        capture_status_ = SceneCaptureStatus::cancelled;
        return;
    }
    if (bgfx::getCaps()->originBottomLeft)
    {
        const auto row = static_cast<std::size_t>(resources.capture_width) * 4;
        for (std::size_t y = 0; y < resources.capture_height / 2; ++y)
        {
            std::swap_ranges(
                resources.capture_pixels.begin() + static_cast<std::ptrdiff_t>(y * row),
                resources.capture_pixels.begin() + static_cast<std::ptrdiff_t>((y + 1) * row),
                resources.capture_pixels.begin() +
                    static_cast<std::ptrdiff_t>((resources.capture_height - y - 1) * row));
        }
    }
    capture_status_ = SceneCaptureStatus::complete;
}

bool RenderSystem::take_diffuse_capture(SceneCaptureImage& output)
{
    output = {};
    if (capture_status_ != SceneCaptureStatus::complete || !scene_resources_)
    {
        return false;
    }
    if (!capture_unchanged())
    {
        cancel_diffuse_capture();
        return false;
    }
    output.width = scene_resources_->capture_width;
    output.height = scene_resources_->capture_height;
    output.rgba = std::move(scene_resources_->capture_pixels);
    capture_request_ = {};
    capture_status_ = SceneCaptureStatus::idle;
    capture_snapshot_.clear();
    return true;
}

void RenderSystem::cancel_diffuse_capture() noexcept
{
    if (capture_status_ != SceneCaptureStatus::pending &&
        capture_status_ != SceneCaptureStatus::complete)
    {
        return;
    }
    if (scene_resources_ && scene_resources_->capture_pending)
    {
        scene_resources_->capture_cancelled = true;
    }
    else
    {
        if (scene_resources_)
        {
            scene_resources_->capture_pixels.clear();
        }
        capture_request_ = {};
        capture_snapshot_.clear();
        capture_status_ = SceneCaptureStatus::cancelled;
    }
}

bool RenderSystem::SceneGpuResources::prepare_capture(const SceneCaptureRequest& request)
{
    const bool dimensions_changed =
        capture_width != request.width || capture_height != request.height;
    if (dimensions_changed || !bgfx::isValid(capture_framebuffer))
    {
        release_capture();
        constexpr auto flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_UVW_CLAMP;
        if (!bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::RGBA32F, flags) ||
            !bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::RGBA32F,
                                  BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK) ||
            !bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::RGBA16F, flags) ||
            !bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::D32F,
                                  BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY) ||
            bgfx::getCaps()->limits.maxFBAttachments < 3)
        {
            return false;
        }
        capture_color = bgfx::createTexture2D(request.width, request.height, false, 1,
                                              bgfx::TextureFormat::RGBA32F, flags);
        capture_specular = bgfx::createTexture2D(request.width, request.height, false, 1,
                                                 bgfx::TextureFormat::RGBA16F, flags);
        capture_depth = bgfx::createTexture2D(request.width, request.height, false, 1,
                                              bgfx::TextureFormat::D32F,
                                              BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY);
        capture_readback = bgfx::createTexture2D(request.width, request.height, false, 1,
                                                 bgfx::TextureFormat::RGBA32F,
                                                 BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
        if (!bgfx::isValid(capture_color) || !bgfx::isValid(capture_depth) ||
            !bgfx::isValid(capture_specular) || !bgfx::isValid(capture_readback))
        {
            release_capture();
            return false;
        }
        const std::array attachments{capture_color, capture_specular, capture_depth};
        capture_framebuffer = bgfx::createFrameBuffer(3, attachments.data(), false);
        if (!bgfx::isValid(capture_framebuffer))
        {
            release_capture();
            return false;
        }
        capture_width = request.width;
        capture_height = request.height;
    }
    capture_pixels.resize(static_cast<std::size_t>(request.width) * request.height * 4);
    bool changed = capture_sources.size() != request.indirect.size();
    for (std::size_t index = 0; !changed && index < capture_sources.size(); ++index)
    {
        const auto& before = capture_sources[index];
        const auto& after = request.indirect[index];
        changed = before.node_id != after.node_id || before.image != after.image ||
                  before.mesh != after.mesh;
    }
    if (!changed)
    {
        return true;
    }
    for (auto handle : capture_lightmaps)
    {
        if (bgfx::isValid(handle))
        {
            bgfx::destroy(handle);
        }
    }
    capture_lightmaps.clear();
    capture_meshes.clear();
    capture_sources.clear();
    for (const auto& item : request.indirect)
    {
        const auto& image = *item.image;
        if (!TextureResourceCodec::validate(image) ||
            image.encoding != schema::TextureEncoding::native || image.srgb || image.cube_map ||
            image.format != schema::TextureFormat::rgba16f || image.data.empty() ||
            image.data.size() > UINT32_MAX)
        {
            return false;
        }
        const auto texture = bgfx::createTexture2D(
            static_cast<std::uint16_t>(image.width), static_cast<std::uint16_t>(image.height),
            image.mip_count > 1, 1, bgfx::TextureFormat::RGBA16F, BGFX_SAMPLER_UVW_CLAMP);
        capture_lightmaps.push_back(texture);
        if (!bgfx::isValid(texture))
        {
            return false;
        }
        for (const auto& part : image.subresources)
        {
            if (part.face() != 0 || part.mip() >= image.mip_count ||
                part.offset() > image.data.size() ||
                part.size() > image.data.size() - part.offset())
            {
                return false;
            }
            bgfx::updateTexture2D(
                texture, 0, static_cast<std::uint8_t>(part.mip()), 0, 0,
                static_cast<std::uint16_t>(std::max(1U, image.width >> part.mip())),
                static_cast<std::uint16_t>(std::max(1U, image.height >> part.mip())),
                bgfx::copy(image.data.data() + part.offset(),
                           static_cast<std::uint32_t>(part.size())));
        }
        std::unique_ptr<render_detail::GpuGeometry> geometry;
        if (item.mesh)
        {
            const auto& data = *item.mesh;
            std::vector<schema::Vec4> tangents;
            std::vector<render_detail::GpuVertex> vertices;
            geometry = std::make_unique<render_detail::GpuGeometry>();
            if (!MeshUtils::build_tangents(data, tangents) ||
                !render_detail::GpuGeometry::pack_vertices(data, data.vertices, tangents, vertices,
                                                           geometry->minimum, geometry->maximum) ||
                vertices.size() > UINT32_MAX / sizeof(render_detail::GpuVertex) ||
                data.indices.size() > UINT32_MAX / sizeof(std::uint32_t))
            {
                return false;
            }
            geometry->vertices = bgfx::createVertexBuffer(
                bgfx::copy(vertices.data(),
                           static_cast<std::uint32_t>(vertices.size() * sizeof(vertices[0]))),
                render_detail::GpuGeometry::vertex_layout());
            geometry->indices = bgfx::createIndexBuffer(
                bgfx::copy(data.indices.data(), static_cast<std::uint32_t>(
                                                    data.indices.size() * sizeof(data.indices[0]))),
                BGFX_BUFFER_INDEX32);
            if (!bgfx::isValid(geometry->vertices) || !bgfx::isValid(geometry->indices))
            {
                return false;
            }
            geometry->vertex_count = static_cast<std::uint32_t>(vertices.size());
        }
        capture_meshes.push_back(std::move(geometry));
    }
    capture_sources = request.indirect;
    return true;
}

bool RenderSystem::SceneGpuResources::draw_capture_sky(const SceneCaptureRequest& request,
                                                       const RenderSettings& settings)
{
    if (settings.atmosphere.atmosphere_enabled)
    {
        // An explicit capture must not publish a bake with silently missing sky.
        if (!settings.atmosphere_sun_valid || !render_detail::AtmospherePass::supported())
        {
            return false;
        }
        float camera_world[16]{};
        bx::mtxInverse(camera_world, request.view.data());
        render_detail::SceneDrawView draw;
        draw.id = render_detail::capture_view;
        draw.width = request.width;
        draw.height = request.height;
        draw.world = camera_world;
        draw.view = request.view.data();
        draw.projection = request.projection.data();
        return atmosphere.draw(settings.atmosphere_scene_id, settings, draw, 3,
                               render_detail::atmosphere_capture_skyview);
    }
    if (!bgfx::isValid(capture_sky_program))
    {
        const auto vertex = bgfx::createShader(bgfx::copy(vs_hdr, sizeof(vs_hdr)));
        const auto fragment =
            bgfx::createShader(bgfx::copy(fs_capture_sky, sizeof(fs_capture_sky)));
        if (bgfx::isValid(vertex) && bgfx::isValid(fragment))
        {
            capture_sky_program = bgfx::createProgram(vertex, fragment, false);
        }
        if (bgfx::isValid(vertex))
        {
            bgfx::destroy(vertex);
        }
        if (bgfx::isValid(fragment))
        {
            bgfx::destroy(fragment);
        }
    }
    if (!bgfx::isValid(capture_inverse))
    {
        capture_inverse = bgfx::createUniform("u_capture_inverse", bgfx::UniformType::Mat4);
    }
    if (!bgfx::isValid(capture_fullscreen))
    {
        struct Vertex
        {
            float x, y, z, u, v;
        };
        std::array vertices{Vertex{-1, -1, 0, 0, 1}, Vertex{3, -1, 0, 2, 1},
                            Vertex{-1, 3, 0, 0, -1}};
        if (bgfx::getCaps()->originBottomLeft)
        {
            for (auto& value : vertices)
            {
                value.v = 1 - value.v;
            }
        }
        bgfx::VertexLayout layout;
        layout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
        capture_fullscreen =
            bgfx::createVertexBuffer(bgfx::copy(vertices.data(), sizeof(vertices)), layout);
    }
    if (!bgfx::isValid(capture_sky_program) || !bgfx::isValid(capture_inverse) ||
        !bgfx::isValid(capture_fullscreen))
    {
        return false;
    }
    float view_projection[16], inverse[16];
    bx::mtxMul(view_projection, request.view.data(), request.projection.data());
    bx::mtxInverse(inverse, view_projection);
    const bool ibl_ready =
        settings.ibl_enabled && ibl_textures[1] && bgfx::isValid(ibl_textures[1]->handle);
    const float ibl[]{ibl_ready ? 1.0F : 0.0F, settings.ibl_intensity, 0, 0};
    const float mode[]{1, bgfx::getCaps()->homogeneousDepth ? -1.0F : 0.0F,
                       bgfx::getCaps()->originBottomLeft ? 1.0F : 0.0F, 0};
    const float ambient_value[]{settings.ambient_color[0], settings.ambient_color[1],
                                settings.ambient_color[2], settings.ambient_intensity};
    const float sky[]{settings.ambient_sky_color[0], settings.ambient_sky_color[1],
                      settings.ambient_sky_color[2], settings.ambient_tricolor ? 1.0F : 0.0F};
    const float equator[]{settings.ambient_equator_color[0], settings.ambient_equator_color[1],
                          settings.ambient_equator_color[2], 0};
    const float ground[]{settings.ambient_ground_color[0], settings.ambient_ground_color[1],
                         settings.ambient_ground_color[2], 0};
    bgfx::setUniform(capture_inverse, inverse);
    bgfx::setViewUniform(render_detail::capture_view, capture_mode, mode);
    bgfx::setViewUniform(render_detail::capture_view, ibl_params, ibl);
    bgfx::setViewUniform(render_detail::capture_view, ambient, ambient_value);
    bgfx::setViewUniform(render_detail::capture_view, ambient_sky, sky);
    bgfx::setViewUniform(render_detail::capture_view, ambient_equator, equator);
    bgfx::setViewUniform(render_detail::capture_view, ambient_ground, ground);
    bgfx::setTexture(13, ibl_prefilter_sampler,
                     ibl_ready ? ibl_textures[1]->handle : ibl_cube_fallback);
    bgfx::setVertexBuffer(0, capture_fullscreen);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(render_detail::capture_view, capture_sky_program);
    return true;
}

void RenderSystem::SceneGpuResources::release_capture() noexcept
{
    // bgfx owns the readback pointer until its returned frame is reached.
    while (capture_pending)
    {
        if (static_cast<std::int32_t>(bgfx::frame() - capture_ready_frame) >= 0)
        {
            capture_pending = false;
        }
    }
    if (bgfx::isValid(capture_framebuffer))
    {
        bgfx::setViewFrameBuffer(render_detail::capture_view, BGFX_INVALID_HANDLE);
        bgfx::destroy(capture_framebuffer);
        capture_framebuffer = BGFX_INVALID_HANDLE;
    }
    for (auto* handle : {&capture_color, &capture_specular, &capture_depth, &capture_readback})
    {
        if (bgfx::isValid(*handle))
        {
            bgfx::destroy(*handle);
            *handle = BGFX_INVALID_HANDLE;
        }
    }
    for (auto handle : capture_lightmaps)
    {
        if (bgfx::isValid(handle))
        {
            bgfx::destroy(handle);
        }
    }
    if (bgfx::isValid(capture_sky_program))
    {
        bgfx::destroy(capture_sky_program);
        capture_sky_program = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(capture_inverse))
    {
        bgfx::destroy(capture_inverse);
        capture_inverse = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(capture_fullscreen))
    {
        bgfx::destroy(capture_fullscreen);
        capture_fullscreen = BGFX_INVALID_HANDLE;
    }
    capture_lightmaps.clear();
    capture_meshes.clear();
    capture_sources.clear();
    capture_pixels.clear();
    capture_width = capture_height = 0;
    capture_cancelled = false;
}
} // namespace engine
