#include "runtime/font/font_system.hpp"
#include "runtime/performance/profiling.hpp"
#include "runtime/render/render_constants.hpp"
#include "runtime/render/render_context.hpp"
#include "runtime/render/render_extension.hpp"
#include "runtime/render/render_math.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/render/scene_gpu_resources.hpp"
#include "runtime/render/scene_render_query.hpp"
#include "runtime/resource/resource.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/camera_math.hpp"
#include "runtime/scene/ecs_scene.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/planar_reflection_component.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/world/world_system.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <bx/math.h>
#include <cmath>
#include <span>
#include <vector>

namespace engine
{
bool RenderSystem::draw_scene(Camera view_camera, const RenderSettings& rendering)
{
    ENGINE_PROFILE_ZONE("Render.drawScene");
    if (hdr_capture_status_ == SceneCaptureStatus::pending &&
        (!world_ || !world_->scene() || world_->scene()->id() != hdr_capture_scene_id_ ||
         ((!scene_resources_ || !scene_resources_->hdr_capture_pending) &&
          !render_detail::drawable_scene(*world_, view_camera, !extensions_.empty()))))
    {
        cancel_hdr_capture();
    }
    if (capture_status_ == SceneCaptureStatus::pending && !capture_unchanged())
    {
        cancel_diffuse_capture();
    }
    if (!prepare_scene(view_camera))
    {
        return false;
    }
    const Scene* scene = world_ ? world_->scene() : nullptr;
    if (!scene || !scene_resources_ ||
        !render_detail::drawable_scene(*world_, view_camera, !extensions_.empty()))
    {
        return true;
    }
    const Camera camera = view_camera ? view_camera : scene->active_camera();
    float camera_world[16]{}, view[16]{}, projection[16]{};
    const auto camera_transform = camera.node().get_component<Transform>();
    const auto& settings = camera.settings();
    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    if (!camera_transform || !camera_transform.world_matrix(camera_world) ||
        !camera_detail::view_matrix(camera_world, view) ||
        !camera_detail::projection_matrix(settings, aspect, bgfx::getCaps()->homogeneousDepth,
                                          projection))
    {
        return false;
    }
    // Mirrors and light capture need the actual orthonormal camera, not a scaled Node transform.
    bx::mtxInverse(camera_world, view);
    auto& resources = *scene_resources_;
    struct Plane
    {
        Node node = {};
        float distance = 0;
    };
    std::vector<Plane> planes;
    {
        const auto process =
            [&](Node node,
                const ecs_detail::ComponentStorage<PlanarReflectionComponent::Data>& stored) -> bool
        {
            const auto* owner = node.scene();
            (void)owner;
            (void)stored;

            const auto reflection = node.get_component<PlanarReflectionComponent>();
            const auto mesh = node.get_component<MeshRenderer>();
            const auto transform = node.get_component<Transform>();
            std::array<float, 16> matrix{};
            if (reflection && reflection.active() && reflection.visible_in_hierarchy() && mesh &&
                mesh.active() && mesh.visible_in_hierarchy() &&
                camera.mesh_visible(mesh.instance_id()) && transform &&
                transform.world_matrix(matrix.data()))
            {
                planes.push_back(
                    {node, std::hypot(matrix[12] - camera_world[12], matrix[13] - camera_world[13],
                                      matrix[14] - camera_world[14])});
            }

            return true;
        };
        auto query = flecs::world(world_->native_world())
                         .query_builder<
                             const ecs_detail::ComponentStorage<PlanarReflectionComponent::Data>>()
                         .with<ecs_detail::Published>()
                         .build();
        bool query_success = true;
        query.each(
            [&](flecs::entity entity,
                const ecs_detail::ComponentStorage<PlanarReflectionComponent::Data>& stored)
            {
                if (query_success)
                {
                    query_success = process(world_->find_node(entity.id()), stored);
                }
            });
        (void)query_success;
    }
    std::sort(
        planes.begin(), planes.end(), [](const Plane& a, const Plane& b)
        { return a.distance == b.distance ? a.node.id() < b.node.id() : a.distance < b.distance; });
    const auto plane_count =
        std::min<std::size_t>(config_.maximum_planar_reflections, planes.size());
    for (std::size_t index = 0; index < resources.planar.size(); ++index)
    {
        auto& target = resources.planar[index];
        if (index >= plane_count)
        {
            target.release();
            resources.clusters[index].release();
            continue;
        }
        const auto node = planes[index].node;
        const auto& options = node.get_component<PlanarReflectionComponent>().settings();
        float matrix[16]{};
        if (!node.get_component<Transform>().world_matrix(matrix))
        {
            return false;
        }
        if (!target.prepare(
                static_cast<std::uint16_t>(std::max(
                    1.0F, std::ceil(static_cast<float>(width_) * options.resolution_scale))),
                static_cast<std::uint16_t>(std::max(
                    1.0F, std::ceil(static_cast<float>(height_) * options.resolution_scale))),
                config_.msaa_samples))
        {
            return false;
        }
        if (!target.mirror(matrix, camera_world, projection, options.clip_bias))
        {
            target.release();
            resources.clusters[index].release();
            continue;
        }
        target.node_id = node.id();
        const auto base = static_cast<bgfx::ViewId>(1 + index * 39);
        render_detail::SceneDrawView draw;
        draw.id = static_cast<bgfx::ViewId>(base + 29);
        draw.width = target.width;
        draw.height = target.height;
        draw.world = target.world.data();
        draw.view = target.view.data();
        draw.projection = target.projection.data();
        draw.reflection = true;
        draw.multisampling = config_.msaa_samples > 1;
        draw.max_anisotropy = config_.max_anisotropy;
        render_detail::ShadowFrame shadow;
        if (!resources.prepare_lighting(*world_, rendering, config_, draw, settings, shadow, index,
                                        base, nullptr))
        {
            return false;
        }
        target.begin(draw.id, rendering.clear_color);
        if (!resources.draw_meshes(*world_, rendering, draw, render_detail::SceneDrawMode::opaque))
        {
            return false;
        }
    }
    render_detail::SceneDrawView draw;
    draw.width = static_cast<std::uint16_t>(width_);
    draw.height = static_cast<std::uint16_t>(height_);
    draw.world = camera_world;
    draw.view = view;
    draw.projection = projection;
    draw.visibility = camera;
    draw.max_anisotropy = config_.max_anisotropy;
    render_detail::ShadowFrame shadow;
    if (!resources.prepare_lighting(*world_, rendering, config_, draw, settings, shadow, 2,
                                    render_detail::shadow_view, nullptr))
    {
        return false;
    }
    if (!resources.surface_pass.prepare(draw.width, draw.height) ||
        !resources.hdr_post_process.prepare(draw.width, draw.height, rendering.bloom_enabled,
                                            config_.msaa_samples))
    {
        return false;
    }
    if (rendering.ao_enabled)
    {
        if (!resources.ambient_occlusion.prepare(draw.width, draw.height))
        {
            return false;
        }
    }
    else
    {
        resources.ambient_occlusion.release();
    }
    resources.surface_pass.begin(view, projection);
    draw.id = render_detail::ambient_occlusion_depth_view;
    if (!resources.draw_meshes(*world_, rendering, draw, render_detail::SceneDrawMode::depth))
    {
        return false;
    }
    draw.id = render_detail::surface_view;
    if (!resources.draw_meshes(*world_, rendering, draw, render_detail::SceneDrawMode::surface))
    {
        return false;
    }
    if (rendering.ao_enabled &&
        !resources.ambient_occlusion.compute(settings, rendering, projection,
                                             resources.surface_pass.depth()))
    {
        return false;
    }
    draw.id = render_detail::main_view;
    draw.multisampling = config_.msaa_samples > 1;
    draw.ambient_occlusion = rendering.ao_enabled;
    bgfx::setViewTransform(draw.id, view, projection);
    resources.hdr_post_process.begin_scene(rendering.clear_color);
    if (!resources.draw_meshes(*world_, rendering, draw, render_detail::SceneDrawMode::opaque))
    {
        return false;
    }
    auto frozen_scene = resources.hdr_post_process.opaque_color();
    if (rendering.ssr_enabled)
    {
        // Match the main PBR pass: disabling IBL selects the analytical DFG
        // fallback even when an earlier bake has left its LUT loaded.
        const bgfx::TextureHandle reflection_lut = rendering.ibl_enabled
                                                      ? resources.ibl_textures[2].handle
                                                      : bgfx::TextureHandle{bgfx::kInvalidHandle};
        if (!resources.reflections.prepare(draw.width, draw.height, rendering) ||
            !resources.reflections.render(
                view, projection, frozen_scene, resources.hdr_post_process.opaque_specular(),
                resources.surface_pass.depth(), resources.surface_pass.normal_roughness(),
                resources.surface_pass.specular(), reflection_lut, rendering))
        {
            return false;
        }
        frozen_scene = resources.reflections.texture();
    }
    else
    {
        resources.reflections.release();
    }
    resources.hdr_post_process.begin_transparency(frozen_scene, view, projection);
    draw.id = render_detail::transparent_view;
    draw.textures[0] = frozen_scene;
    draw.textures[1] = resources.surface_pass.depth();
    if (!resources.draw_meshes(*world_, rendering, draw, render_detail::SceneDrawMode::transparent))
    {
        return false;
    }
    if (fonts_)
    {
        for (const auto& owner : world_->scenes())
        {
            if (!fonts_->render_scene(*owner, render_detail::transparent_view, true))
            {
                return false;
            }
        }
    }
    if (!extensions_.empty())
    {
        RenderContext context{*scene};
        std::copy_n(view, context.view.size(), context.view.begin());
        std::copy_n(projection, context.projection.size(), context.projection.begin());
        context.view_id = render_detail::extension_view;
        context.width = draw.width;
        context.height = draw.height;
        context.hdr = true;
        bgfx::setViewName(context.view_id, "Scene extensions");
        bgfx::setViewFrameBuffer(context.view_id, resources.hdr_post_process.scene_framebuffer());
        bgfx::setViewRect(context.view_id, 0, 0, context.width, context.height);
        bgfx::setViewClear(context.view_id, BGFX_CLEAR_NONE);
        bgfx::setViewMode(context.view_id, bgfx::ViewMode::Sequential);
        bgfx::setViewTransform(context.view_id, view, projection);
        invoking_extensions_ = true;
        for (auto* extension : extensions_)
        {
            if (!extension->draw(context))
            {
                invoking_extensions_ = false;
                return false;
            }
        }
        invoking_extensions_ = false;
    }
    if (!resources.hdr_post_process.resolve(rendering))
    {
        return false;
    }
    submit_hdr_capture();
    return true;
}
} // namespace engine
