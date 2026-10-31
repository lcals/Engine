#include "runtime/render/render_system.hpp"

#include "runtime/font/font_system.hpp"
#include "runtime/render/render_constants.hpp"
#include "runtime/render/render_math.hpp"
#include "runtime/render/scene_gpu_resources.hpp"
#include "runtime/render/scene_render_query.hpp"
#include "runtime/resource/resource.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/world/world_system.hpp"

#include <SDL3/SDL.h>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace engine
{

bool RenderSystem::draw_scene(const Camera* view_camera, const RenderSettings& rendering)
{
    if (!prepare_scene(view_camera))
    {
        return false;
    }
    const Scene* scene = world_ != nullptr ? world_->scene() : nullptr;
    if (scene == nullptr || scene_resources_ == nullptr ||
        !render_detail::drawable_scene(*scene, view_camera))
    {
        return true;
    }
    const Camera& camera = view_camera != nullptr ? *view_camera : *scene->active_camera();
    float camera_world[16]{};
    const auto* camera_transform = camera.node().get_component<Transform>();
    if (camera_transform == nullptr || !camera_transform->world_matrix(camera_world))
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Camera world matrix is invalid");
        return false;
    }
    float view[16]{};
    float projection[16]{};
    const CameraSettings& settings = camera.settings();
    const float aspect = static_cast<float>(width_) / height_;
    if (settings.projection == schema::Projection::orthographic)
    {
        const float horizontal = settings.orthographic_size * aspect;
        bx::mtxOrtho(projection, -horizontal, horizontal, -settings.orthographic_size,
                     settings.orthographic_size, settings.near_clip, settings.far_clip, 0.0F,
                     bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
    }
    else
    {
        bx::mtxProj(projection, settings.fov_degrees, aspect, settings.near_clip, settings.far_clip,
                    bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
    }
    if (!render_detail::camera_view(camera_world, view) ||
        !render_detail::finite_matrix(projection))
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Camera view or projection matrix is invalid");
        return false;
    }
    bgfx::setViewTransform(render_detail::main_view, view, projection);
    const float camera_position[] = {camera_world[12], camera_world[13], camera_world[14], 1.0F};
    std::array<float, 4> light_direction{0.0F, 0.0F, -1.0F, 0.0F};
    std::array<float, 4> light_color{};
    std::array<std::array<float, 4>, render_detail::maximum_lights> light_position_range{};
    std::array<std::array<float, 4>, render_detail::maximum_lights> light_direction_outer{};
    std::array<std::array<float, 4>, render_detail::maximum_lights> light_color_intensity{};
    std::array<std::array<float, 4>, render_detail::maximum_lights> light_kind_inner{};
    std::size_t light_count = 0;
    std::size_t shadow_light = render_detail::maximum_lights;
    for (const auto& node : scene->nodes())
    {
        const Light* light = node->get_component<Light>();
        if (light == nullptr || !light->enabled())
        {
            continue;
        }
        if (light_count == render_detail::maximum_lights)
        {
            return false;
        }
        float world[16]{};
        const auto* transform = node->get_component<Transform>();
        if (transform == nullptr || !transform->world_matrix(world))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Light world matrix is invalid");
            return false;
        }
        const double length =
            std::hypot(static_cast<double>(world[8]), static_cast<double>(world[9]),
                       static_cast<double>(world[10]));
        if (!(length > 0.0))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Light direction is invalid");
            return false;
        }
        constexpr float degrees_to_radians = 0.01745329251994329577F;
        const LightSettings& lighting = light->settings();
        if (lighting.kind == schema::LightKind::directional && lighting.cast_shadows)
        {
            shadow_light = light_count;
        }
        light_position_range[light_count] = {world[12], world[13], world[14], lighting.range};
        light_direction_outer[light_count] = {static_cast<float>(-world[8] / length),
                                              static_cast<float>(-world[9] / length),
                                              static_cast<float>(-world[10] / length),
                                              std::cos(lighting.outer_angle * degrees_to_radians)};
        light_color_intensity[light_count] = {lighting.color.r(), lighting.color.g(),
                                              lighting.color.b(), lighting.intensity};
        light_kind_inner[light_count] = {static_cast<float>(lighting.kind),
                                         std::cos(lighting.inner_angle * degrees_to_radians), 0.0F,
                                         0.0F};
        if (light_count == 0)
        {
            light_direction = light_direction_outer[0];
            light_direction[3] = 0.0F;
            light_color = {lighting.color.r() * lighting.intensity,
                           lighting.color.g() * lighting.intensity,
                           lighting.color.b() * lighting.intensity, 0.0F};
        }
        ++light_count;
    }
    const float count_value[] = {static_cast<float>(light_count), 0.0F, 0.0F, 0.0F};
    SceneGpuResources& resources = *scene_resources_;
    render_detail::ShadowFrame shadow_frame;
    for (auto& matrix : shadow_frame.matrices)
    {
        bx::mtxIdentity(matrix.data());
    }
    const bool shadow_enabled =
        rendering.directional_shadows && shadow_light < light_count &&
        std::min(settings.far_clip, rendering.shadow_distance) > settings.near_clip;
    if (shadow_enabled)
    {
        if (!resources.shadow.prepare(rendering.shadow_resolution) ||
            !render_detail::prepare_shadow_frame(settings, rendering, aspect, camera_world, view,
                                                 light_direction_outer[shadow_light],
                                                 shadow_frame) ||
            !resources.shadow.draw(*scene, rendering, shadow_frame, resources.meshes,
                                   resources.textures, resources.shadow_fallback,
                                   resources.shadow_depth))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot render directional shadow cascades");
            return false;
        }
    }
    else
    {
        resources.shadow.release();
    }
    const float shadow_settings[] = {rendering.shadow_bias, 1.0F / rendering.shadow_resolution,
                                     static_cast<float>(shadow_light),
                                     shadow_enabled ? 1.0F : 0.0F};
    const float ambient[] = {rendering.ambient_color[0], rendering.ambient_color[1],
                             rendering.ambient_color[2], rendering.ambient_intensity};
    const float ambient_sky[] = {rendering.ambient_sky_color[0], rendering.ambient_sky_color[1],
                                 rendering.ambient_sky_color[2],
                                 rendering.ambient_tricolor ? 1.0F : 0.0F};
    const float ambient_equator[] = {rendering.ambient_equator_color[0],
                                     rendering.ambient_equator_color[1],
                                     rendering.ambient_equator_color[2], 0.0F};
    const float ambient_ground[] = {rendering.ambient_ground_color[0],
                                    rendering.ambient_ground_color[1],
                                    rendering.ambient_ground_color[2], 0.0F};
    const float fog_color[] = {rendering.fog_color[0], rendering.fog_color[1],
                               rendering.fog_color[2], static_cast<float>(rendering.fog_mode)};
    const float fog_range[] = {rendering.fog_start, rendering.fog_end, rendering.fog_density,
                               settings.near_clip};
    const float tonemap[] = {rendering.exposure, rendering.filmic_tonemap ? 1.0F : 0.0F,
                             rendering.hdr_enabled ? 1.0F : 0.0F, 0.0F};
    const bool ibl_ready =
        rendering.ibl_enabled &&
        std::all_of(resources.ibl_textures.begin(), resources.ibl_textures.end(),
                    [](const auto& texture) { return bgfx::isValid(texture.handle); });
    const float ibl_params[] = {
        ibl_ready ? 1.0F : 0.0F, rendering.ibl_intensity,
        ibl_ready ? static_cast<float>(resources.ibl_textures[1].resource->texture()->mip_count - 1)
                  : 0.0F,
        0.0F};
    // 复用完整材质保留顶点变形、裁剪透明与自定义天空的深度语义。
    const auto draw_meshes = [&](std::uint16_t view_id, bool depth_only)
    {
        for (const auto& node : scene->nodes())
        {
            const MeshRenderer* renderer = node->get_component<MeshRenderer>();
            if (renderer == nullptr || !renderer->enabled() || !renderer->visible())
            {
                continue;
            }
            float model[16]{};
            const auto* transform = node->get_component<Transform>();
            if (transform == nullptr || !transform->world_matrix(model))
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Mesh world matrix is invalid");
                return false;
            }
            float normal_transform[16]{};
            if (!render_detail::normal_matrix(model, normal_transform))
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Mesh normal transform is invalid");
                return false;
            }
            const auto mesh =
                std::find_if(resources.meshes.begin(), resources.meshes.end(),
                             [&node](const auto& item) { return item.node_id == node->id(); });
            const auto* data = renderer->mesh_data();
            if (mesh == resources.meshes.end() || data == nullptr)
            {
                return false;
            }
            const auto submit = [&](std::uint32_t first, std::uint32_t count, std::size_t slot)
            {
                const Material* material = renderer->material(slot);
                if (material == nullptr)
                {
                    return false;
                }
                auto program = std::find_if(resources.programs.begin(), resources.programs.end(),
                                            [material](const auto& item)
                                            { return item.path == material->shader_path(); });
                if (program == resources.programs.end() ||
                    !program->bind_material(*material, resources.textures))
                {
                    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                                 "Material parameters do not match its shader");
                    return false;
                }
                bgfx::setUniform(resources.normal_transform, normal_transform);
                bgfx::setUniform(resources.camera_position, camera_position);
                bgfx::setUniform(resources.light_direction, light_direction.data());
                bgfx::setUniform(resources.light_color, light_color.data());
                bgfx::setUniform(resources.light_count, count_value);
                bgfx::setUniform(resources.light_position_range, light_position_range.data(),
                                 render_detail::maximum_lights);
                bgfx::setUniform(resources.light_direction_outer, light_direction_outer.data(),
                                 render_detail::maximum_lights);
                bgfx::setUniform(resources.light_color_intensity, light_color_intensity.data(),
                                 render_detail::maximum_lights);
                bgfx::setUniform(resources.light_kind_inner, light_kind_inner.data(),
                                 render_detail::maximum_lights);
                bgfx::setUniform(resources.ambient, ambient);
                bgfx::setUniform(resources.ambient_sky, ambient_sky);
                bgfx::setUniform(resources.ambient_equator, ambient_equator);
                bgfx::setUniform(resources.ambient_ground, ambient_ground);
                bgfx::setUniform(resources.fog_color, fog_color);
                bgfx::setUniform(resources.fog_range, fog_range);
                bgfx::setUniform(resources.tonemap, tonemap);
                bgfx::setUniform(resources.ibl_params, ibl_params);
                const bool ao_enabled = rendering.ao_enabled && !depth_only;
                const float ao_settings[] = {1.0F / width_, 1.0F / height_,
                                             ao_enabled ? 1.0F : 0.0F, 0.0F};
                bgfx::setUniform(resources.ao_settings, ao_settings);
                bgfx::setTexture(render_detail::ambient_occlusion_stage, resources.ao_sampler,
                                 ao_enabled ? resources.ambient_occlusion.texture()
                                            : resources.shadow_fallback);
                bgfx::setTexture(
                    render_detail::ibl_irradiance_stage, resources.ibl_irradiance_sampler,
                    ibl_ready ? resources.ibl_textures[0].handle : resources.ibl_cube_fallback);
                bgfx::setTexture(
                    render_detail::ibl_prefilter_stage, resources.ibl_prefilter_sampler,
                    ibl_ready ? resources.ibl_textures[1].handle : resources.ibl_cube_fallback);
                bgfx::setTexture(render_detail::ibl_lut_stage, resources.ibl_lut_sampler,
                                 ibl_ready ? resources.ibl_textures[2].handle
                                           : resources.ibl_lut_fallback);
                const float surface[] = {material->cull_mode() != schema::CullMode::back ? 1.0F
                                                                                         : 0.0F,
                                         0.0F, 0.0F, 0.0F};
                bgfx::setUniform(resources.surface, surface);
                bgfx::setUniform(resources.shadow_matrices, shadow_frame.matrices.data(),
                                 render_detail::maximum_shadow_cascades);
                bgfx::setUniform(resources.shadow_rects, shadow_frame.rectangles.data(),
                                 render_detail::maximum_shadow_cascades);
                bgfx::setUniform(resources.shadow_splits, shadow_frame.splits.data());
                bgfx::setUniform(resources.shadow_params, shadow_frame.parameters.data());
                bgfx::setUniform(resources.shadow_settings, shadow_settings);
                bgfx::setTexture(render_detail::shadow_sampler_stage, resources.shadow_sampler,
                                 shadow_enabled ? resources.shadow.texture()
                                                : resources.shadow_fallback);
                bgfx::setTransform(model);
                mesh->bind_vertices();
                bgfx::setIndexBuffer(mesh->indices, first, count);
                const auto state = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                                   render_detail::cull_state(material->cull_mode(), model);
                bgfx::setState(depth_only ? state
                                          : state | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                                                BGFX_STATE_MSAA);
                bgfx::submit(view_id, program->program);
                return true;
            };
            if (data->submeshes.empty())
            {
                if (!submit(0, static_cast<std::uint32_t>(data->indices.size()), 0))
                {
                    return false;
                }
            }
            for (const auto& submesh : data->submeshes)
            {
                if (!submit(submesh.first_index(), submesh.index_count(), submesh.material_slot()))
                {
                    return false;
                }
            }
        }
        return true;
    };
    if (rendering.ao_enabled)
    {
        if (!resources.ambient_occlusion.prepare(static_cast<std::uint16_t>(width_),
                                                 static_cast<std::uint16_t>(height_)))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot prepare ambient occlusion resources");
            return false;
        }
        resources.ambient_occlusion.begin_depth(view, projection);
        if (!draw_meshes(render_detail::ambient_occlusion_depth_view, true) ||
            !resources.ambient_occlusion.compute(settings, rendering, projection))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot render ambient occlusion");
            return false;
        }
    }
    else
    {
        resources.ambient_occlusion.release();
    }
    if (rendering.hdr_enabled)
    {
        if (!resources.hdr_post_process.prepare(static_cast<std::uint16_t>(width_),
                                                static_cast<std::uint16_t>(height_),
                                                rendering.bloom_enabled))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot prepare HDR post-processing");
            return false;
        }
        resources.hdr_post_process.begin_scene(rendering.clear_color);
    }
    else
    {
        resources.hdr_post_process.release();
    }
    if (!draw_meshes(render_detail::main_view, false) ||
        (fonts_ != nullptr &&
         !fonts_->render_scene(*scene, render_detail::main_view, rendering.hdr_enabled)))
    {
        return false;
    }
    return !rendering.hdr_enabled || resources.hdr_post_process.resolve(rendering);
}

} // namespace engine
