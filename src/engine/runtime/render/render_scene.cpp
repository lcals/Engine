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
    // Disabling a sky-only scene makes it non-drawable. Retire its LUTs before that early return.
    if (scene_resources_ &&
        (!rendering.atmosphere.atmosphere_enabled || !rendering.atmosphere_sun_valid))
    {
        scene_resources_->atmosphere.release();
    }
    const Scene* scene = world_ ? world_->scene() : nullptr;
    if (!scene || !scene_resources_ ||
        !render_detail::drawable_scene(*world_, view_camera, !extensions_.empty()))
    {
        scene_collected_ = scene != nullptr && !render_detail::drawable_scene(*world_, view_camera,
                                                                              !extensions_.empty());
        return true;
    }
    const Camera camera = view_camera ? view_camera : scene->active_camera();
    float camera_world[16]{}, view[16]{}, projection[16]{};
    const auto camera_transform = camera.node().get_component<Transform>();
    CameraSettings settings;
    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    if (!camera.get_settings(settings) || !camera_transform ||
        !camera_transform.world_matrix(camera_world) ||
        !camera_detail::view_matrix(camera_world, view) ||
        !camera_detail::projection_matrix(settings, aspect, bgfx::getCaps()->homogeneousDepth,
                                          projection))
    {
        return false;
    }
    // Mirrors and light capture need the actual orthonormal camera, not a scaled Node transform.
    bx::mtxInverse(camera_world, view);
    auto& resources = *scene_resources_;
    resources.select_targets(render_detail::target_scope(*scene));
    if (resources.targets().stage != 10)
    {
        return true;
    }
    if (!resources.collect_frame(*world_, resources.frame_data))
    {
        return false;
    }
    scene_collected_ = resources.frame_complete;
    for (std::size_t index = 0; index < resources.targets().planar.size(); ++index)
    {
        auto& target = resources.targets().planar[index];
        if (target.node_id == 0)
        {
            continue;
        }
        const auto node = world_->find_node(target.node_id);
        const auto base = static_cast<bgfx::ViewId>(3 + index * 39);
        render_detail::SceneDrawView draw;
        draw.renderer = this;
        draw.id = static_cast<bgfx::ViewId>(base + 30);
        draw.width = target.width;
        draw.height = target.height;
        draw.world = target.world.data();
        draw.view = target.view.data();
        draw.projection = target.projection.data();
        draw.reflection = true;
        draw.frustum_culling_enabled = settings.frustum_culling_enabled;
        draw.lod_observer = {camera.instance_id(),
                             node.get_component<PlanarReflectionComponent>().instance_id()};
        draw.multisampling = config_.msaa_samples > 1;
        draw.max_anisotropy = config_.max_anisotropy;
        render_detail::ShadowFrame shadow;
        if (!resources.prepare_lighting(resources.frame_data, rendering, config_, draw, settings,
                                        shadow, index, base, nullptr))
        {
            return false;
        }
        target.begin(draw.id, rendering.clear_color);
        if (!resources.atmosphere.draw(scene->id(), rendering, draw, index,
                                       static_cast<bgfx::ViewId>(base + 29)))
        {
            return false;
        }
        if (!resources.collect_view(resources.frame_data, draw, resources.draw_lists[index]) ||
            !resources.draw_meshes(resources.frame_data, resources.draw_lists[index], rendering,
                                   draw, render_detail::SceneDrawMode::opaque))
        {
            return false;
        }
    }
    render_detail::SceneDrawView draw;
    draw.renderer = this;
    draw.width = static_cast<std::uint16_t>(width_);
    draw.height = static_cast<std::uint16_t>(height_);
    draw.world = camera_world;
    draw.view = view;
    draw.projection = projection;
    draw.visibility = camera;
    draw.frustum_culling_enabled = settings.frustum_culling_enabled;
    draw.lod_observer = {camera.instance_id(), 0};
    draw.max_anisotropy = config_.max_anisotropy;
    render_detail::ShadowFrame shadow;
    if (!resources.prepare_lighting(resources.frame_data, rendering, config_, draw, settings,
                                    shadow, 2, render_detail::shadow_view, nullptr))
    {
        return false;
    }
    if (!resources.collect_view(resources.frame_data, draw, resources.draw_lists[2]))
    {
        return false;
    }
    resources.targets().surface_pass.begin(view, projection);
    draw.id = render_detail::ambient_occlusion_depth_view;
    if (!resources.draw_meshes(resources.frame_data, resources.draw_lists[2], rendering, draw,
                               render_detail::SceneDrawMode::depth))
    {
        return false;
    }
    draw.id = render_detail::surface_view;
    if (!resources.draw_meshes(resources.frame_data, resources.draw_lists[2], rendering, draw,
                               render_detail::SceneDrawMode::surface))
    {
        return false;
    }
    if (rendering.ao_enabled &&
        !resources.targets().ambient_occlusion.compute(settings, rendering, projection,
                                                       resources.targets().surface_pass.depth()))
    {
        return false;
    }
    draw.id = render_detail::main_view;
    draw.multisampling = config_.msaa_samples > 1;
    draw.ambient_occlusion = rendering.ao_enabled;
    bgfx::setViewTransform(draw.id, view, projection);
    resources.targets().hdr_post_process.begin_scene(rendering.clear_color);
    if (!resources.atmosphere.draw(scene->id(), rendering, draw, 2,
                                   render_detail::atmosphere_main_skyview))
    {
        return false;
    }
    if (!resources.draw_meshes(resources.frame_data, resources.draw_lists[2], rendering, draw,
                               render_detail::SceneDrawMode::opaque))
    {
        return false;
    }
    auto frozen_scene = resources.targets().hdr_post_process.opaque_color();
    if (rendering.ssr_enabled)
    {
        // Match the main PBR pass: disabling IBL selects the analytical DFG
        // fallback even when an earlier bake has left its LUT loaded.
        const bgfx::TextureHandle reflection_lut =
            rendering.ibl_enabled && resources.targets().ibl_textures[2]
                ? resources.targets().ibl_textures[2]->handle
                : bgfx::TextureHandle{bgfx::kInvalidHandle};
        if (!resources.targets().reflections.render(
                view, projection, frozen_scene,
                resources.targets().hdr_post_process.opaque_specular(),
                resources.targets().surface_pass.depth(),
                resources.targets().surface_pass.normal_roughness(),
                resources.targets().surface_pass.specular(), reflection_lut, rendering))
        {
            return false;
        }
        frozen_scene = resources.targets().reflections.texture();
    }
    resources.targets().hdr_post_process.begin_transparency(frozen_scene, view, projection);
    draw.id = render_detail::transparent_view;
    draw.textures[0] = frozen_scene;
    draw.textures[1] = resources.targets().surface_pass.depth();
    if (!resources.draw_meshes(resources.frame_data, resources.draw_lists[2], rendering, draw,
                               render_detail::SceneDrawMode::transparent))
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
        bgfx::setViewFrameBuffer(context.view_id,
                                 resources.targets().hdr_post_process.scene_framebuffer());
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
    {
        ENGINE_PROFILE_ZONE("Render.debugOverlay");
        if (!draw_debug_overlay(camera, view, projection))
        {
            return false;
        }
    }
    if (!resources.targets().hdr_post_process.resolve(rendering))
    {
        return false;
    }
    submit_hdr_capture();
    return true;
}
} // namespace engine
