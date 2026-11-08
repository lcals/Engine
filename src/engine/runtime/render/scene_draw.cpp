#include "runtime/render/scene_draw.hpp"

#include "runtime/performance/profiling.hpp"
#include "runtime/render/render_constants.hpp"
#include "runtime/render/render_math.hpp"
#include "runtime/render/scene_gpu_resources.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <bx/math.h>
#include <cmath>
#include <cstring>
#include <map>

namespace engine
{
bool RenderSystem::SceneGpuResources::prepare_lighting(
    const render_detail::RenderFrameData& frame_data, const RenderSettings& rendering,
    const RenderConfig& config, render_detail::SceneDrawView& draw, const CameraSettings& lens,
    render_detail::ShadowFrame& frame, std::size_t cluster_index, bgfx::ViewId base,
    const SceneCaptureRequest* capture)
{
    std::vector<render_detail::ClusterLight> lights;
    std::vector<render_detail::LocalShadowLight> local_lights;
    std::uint32_t shadow_light = UINT32_MAX;
    auto light_settings = rendering;
    light_settings.directional_shadows = false;
    for (const auto& source : frame_data.lights)
    {
        if (capture && std::find(capture->light_node_ids.begin(), capture->light_node_ids.end(),
                                 source.node_id) == capture->light_node_ids.end())
        {
            continue;
        }
        if (lights.size() >= config.maximum_lights)
        {
            return false;
        }
        const auto& matrix = source.world;
        const float length = std::hypot(matrix[8], matrix[9], matrix[10]);
        if (!(length > 0))
        {
            return false;
        }
        const auto& lighting = source.settings;
        constexpr float radians = 0.01745329251994329577F;
        const auto index = static_cast<std::uint32_t>(lights.size());
        render_detail::ClusterLight packed;
        packed.position_range = {matrix[12], matrix[13], matrix[14], lighting.range};
        packed.direction_outer = {-matrix[8] / length, -matrix[9] / length, -matrix[10] / length,
                                  std::cos(lighting.outer_angle * radians)};
        packed.color_intensity = {lighting.color.r(), lighting.color.g(), lighting.color.b(),
                                  lighting.intensity};
        packed.kind_inner = {static_cast<float>(lighting.kind),
                             std::cos(lighting.inner_angle * radians), 0, 0};
        lights.push_back(packed);
        if (lighting.cast_shadows)
        {
            if (lighting.kind == schema::LightKind::directional)
            {
                if (shadow_light != UINT32_MAX)
                {
                    return false;
                }
                shadow_light = index;
                light_settings.directional_shadows = true;
                light_settings.shadow_resolution =
                    std::min(lighting.shadow_resolution, config.shadow_atlas_size);
                light_settings.shadow_bias = lighting.shadow_bias;
                light_settings.shadow_cascades = lighting.shadow_cascades;
                light_settings.shadow_distance = lighting.shadow_distance;
                light_settings.shadow_split_lambda = lighting.shadow_split_lambda;
                light_settings.shadow_cascade_blend = lighting.shadow_cascade_blend;
            }
            else
            {
                local_lights.push_back({source.node_id, index, lighting, matrix});
            }
        }
    }
    for (auto& matrix : frame.matrices)
    {
        bx::mtxIdentity(matrix.data());
    }
    if (capture)
    {
        // Bake every selected surface, even beyond the gameplay light's fade range.
        // Tighten the request frustum to its actual contributors instead of stretching
        // a shadow cascade to an arbitrary hemicube far plane (often 10000 metres).
        float farthest = lens.near_clip;
        for (const auto id : capture->node_ids)
        {
            const auto found =
                std::find_if(frame_data.meshes.begin(), frame_data.meshes.end(),
                             [&](const auto& mesh) { return mesh.gpu->node_id == id; });
            if (found == frame_data.meshes.end())
            {
                return false;
            }
            const auto* mesh = found->gpu;
            const auto& model = found->model;
            for (unsigned corner = 0; corner < 8; ++corner)
            {
                std::array<float, 3> point{};
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    point[axis] =
                        (corner & (1U << axis)) ? mesh->maximum[axis] : mesh->minimum[axis];
                }
                point = render_detail::transform_point(model.data(), point);
                point = render_detail::transform_point(draw.view, point);
                farthest = std::max(farthest, -point[2]);
            }
        }
        light_settings.shadow_distance = std::min(lens.far_clip, farthest);
    }
    const bool csm = light_settings.directional_shadows && shadow_light < lights.size() &&
                     std::min(lens.far_clip, light_settings.shadow_distance) > lens.near_clip;
    if (csm && !render_detail::prepare_shadow_frame(
                   lens, light_settings, static_cast<float>(draw.width) / draw.height, draw.world,
                   draw.view, lights[shadow_light].direction_outer, frame))
    {
        return false;
    }
    std::span<const std::uint64_t> caster_ids;
    if (capture)
    {
        caster_ids = capture->node_ids;
    }
    if (!shadow.draw(frame_data, light_settings, frame, shadow_fallback, shadow_depth, config,
                     local_lights, lights, draw.world, base, capture ? &caster_ids : nullptr))
    {
        return false;
    }
    auto& cluster = clusters[cluster_index];
    if (!cluster.cull(static_cast<bgfx::ViewId>(base + 28), lens, draw.view, draw.projection,
                      lights, frame.local_records))
    {
        return false;
    }
    draw.cluster = &cluster;
    draw.shadow = &frame;
    draw.shadow_light = shadow_light;
    draw.shadow_bias = light_settings.shadow_bias;
    draw.inverse_shadow_size = 1.0F / config.shadow_atlas_size;
    draw.near_clip = lens.near_clip;
    return true;
}

bool RenderSystem::SceneGpuResources::draw_meshes(const render_detail::RenderFrameData& frame_data,
                                                  const render_detail::ViewDrawList& list,
                                                  const RenderSettings& settings,
                                                  const render_detail::SceneDrawView& draw,
                                                  render_detail::SceneDrawMode mode)
{
    using render_detail::SceneDrawMode;
    ENGINE_PROFILE_ZONE("Render.submitMeshes");
    const bool instancing = bgfx::getCaps()->limits.maxInstanceData >= 7;
    RenderViewStats stats =
        mode == SceneDrawMode::transparent ? list.transparent_stats : list.opaque_stats;
    stats.view_id = draw.id;
    const auto frame = draw.renderer ? draw.renderer->frame_number() : 0;
    if (frame_time_stamp != frame)
    {
        frame_time_stamp = frame;
        frame_time = {static_cast<float>(static_cast<double>(SDL_GetTicks()) / 1000.0), 0, 0, 0};
        bgfx::setFrameUniform(frame_time_uniform, frame_time.data());
    }
    const bool depth_only = mode == SceneDrawMode::depth;
    const bool capture = mode == SceneDrawMode::capture;
    const bool transparent = mode == SceneDrawMode::transparent;
    const bool ibl_ready =
        settings.ibl_enabled &&
        std::all_of(ibl_textures.begin(), ibl_textures.end(),
                    [](const auto* texture) { return texture && bgfx::isValid(texture->handle); });
    const float camera_position_value[]{draw.world[12], draw.world[13], draw.world[14], 1};
    const float ambient_value[]{settings.ambient_color[0], settings.ambient_color[1],
                                settings.ambient_color[2], settings.ambient_intensity};
    const float sky[]{settings.ambient_sky_color[0], settings.ambient_sky_color[1],
                      settings.ambient_sky_color[2], settings.ambient_tricolor ? 1.0F : 0.0F};
    const float equator[]{settings.ambient_equator_color[0], settings.ambient_equator_color[1],
                          settings.ambient_equator_color[2], 0};
    const float ground[]{settings.ambient_ground_color[0], settings.ambient_ground_color[1],
                         settings.ambient_ground_color[2], 0};
    const float fog[]{settings.fog_color[0], settings.fog_color[1], settings.fog_color[2],
                      static_cast<float>(settings.fog_mode)};
    const float fog_range_value[]{settings.fog_start, settings.fog_end, settings.fog_density,
                                  draw.near_clip};
    const float tone[]{settings.exposure, settings.filmic_tonemap ? 1.0F : 0.0F, 1, 0};
    const float ibl[]{
        ibl_ready ? 1.0F : 0.0F, settings.ibl_intensity,
        ibl_ready
            ? static_cast<float>(ibl_textures[1]->resource.data<TextureResource>()->mip_count - 1)
            : 0,
        0};
    const float shadow_values[]{draw.shadow_bias, draw.inverse_shadow_size,
                                static_cast<float>(draw.shadow_light),
                                draw.shadow->tile_size ? 1.0F : 0.0F};
    const float capture_value[]{capture ? 1.0F : 0.0F, 0, 0, 0};
    const float zero[4]{};
    const float frame_value[]{1.0F / draw.width, 1.0F / draw.height, frame_time[0],
                              bgfx::getCaps()->originBottomLeft ? 1.0F : 0.0F};
    const auto& items = transparent ? list.transparent : list.opaque;
    std::map<render_detail::DrawBatchKey, std::size_t> batch_index;
    std::vector<render_detail::DrawBatch> batches;
    {
        ENGINE_PROFILE_ZONE("Render.buildBatches");
        for (std::size_t index = 0; index < items.size(); ++index)
        {
            const auto& item = items[index];
            const auto& mesh = frame_data.meshes[item.mesh];
            const auto& material = mesh.gpu->materials[item.slot];
            const auto* program = material.program;
            if (!program)
            {
                return false;
            }
            if (mode == SceneDrawMode::surface && !bgfx::isValid(program->surface_program))
            {
                continue;
            }
            auto cull = capture ? std::uint64_t{0}
                                : render_detail::cull_state(material.cull, mesh.model.data());
            if (draw.reflection)
            {
                if (cull == BGFX_STATE_CULL_CW)
                {
                    cull = BGFX_STATE_CULL_CCW;
                }
                else if (cull == BGFX_STATE_CULL_CCW)
                {
                    cull = BGFX_STATE_CULL_CW;
                }
            }
            auto state = cull | BGFX_STATE_DEPTH_TEST_LESS;
            if (!transparent)
            {
                state |= BGFX_STATE_WRITE_Z;
            }
            if (!depth_only)
            {
                state |= BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
            }
            if (mode == SceneDrawMode::surface)
            {
                state =
                    cull | BGFX_STATE_DEPTH_TEST_EQUAL | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
            }
            if (transparent)
            {
                const auto alpha_source =
                    material.unlit ? BGFX_STATE_BLEND_ZERO : BGFX_STATE_BLEND_ONE;
                state |= material.blend == schema::BlendMode::premultiplied
                             ? BGFX_STATE_BLEND_FUNC_SEPARATE(
                                   BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA,
                                   alpha_source, BGFX_STATE_BLEND_INV_SRC_ALPHA)
                             : BGFX_STATE_BLEND_FUNC_SEPARATE(
                                   BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA,
                                   alpha_source, BGFX_STATE_BLEND_INV_SRC_ALPHA);
            }
            if (draw.multisampling)
            {
                state |= BGFX_STATE_MSAA;
            }
            render_detail::DrawBatchKey key;
            key.geometry = reinterpret_cast<std::uintptr_t>(item.geometry);
            key.state = state;
            key.binding = material.binding_id;
            key.lod = item.lod;
            key.first = item.first;
            key.count = item.count;
            key.program = (mode == SceneDrawMode::surface ? program->instanced_surface_program
                                                          : program->instanced_program)
                              .idx;
            key.lightmap = item.lightmap.idx;
            key.has_lightmap = item.has_lightmap;
            key.has_probe = item.has_probe;
            if (item.has_probe)
            {
                key.coefficients = item.coefficients;
            }
            const bool eligible = instancing && mesh.instancing && mesh.instance_eligible &&
                                  !mesh.plane && !material.frame_textures && !transparent &&
                                  !capture && item.geometry == mesh.gpu->geometry.get() &&
                                  key.program != bgfx::kInvalidHandle;
            if (eligible)
            {
                auto [position, inserted] = batch_index.try_emplace(key, batches.size());
                if (inserted)
                {
                    batches.push_back({key, {}, 0});
                }
                batches[position->second].items.push_back(index);
            }
            else
            {
                batches.push_back({key, {index}, 0});
            }
        }
    }
    const bool use_ao = draw.ambient_occlusion && !depth_only && mode != SceneDrawMode::surface;
    const float ao[]{1.0F / draw.width, 1.0F / draw.height, use_ao ? 1.0F : 0.0F, 0};
    bgfx::setViewUniform(draw.id, camera_position, camera_position_value);
    bgfx::setViewUniform(draw.id, light_direction, zero);
    bgfx::setViewUniform(draw.id, light_color, zero);
    bgfx::setViewUniform(draw.id, ambient, ambient_value);
    bgfx::setViewUniform(draw.id, ambient_sky, sky);
    bgfx::setViewUniform(draw.id, ambient_equator, equator);
    bgfx::setViewUniform(draw.id, ambient_ground, ground);
    bgfx::setViewUniform(draw.id, fog_color, fog);
    bgfx::setViewUniform(draw.id, fog_range, fog_range_value);
    bgfx::setViewUniform(draw.id, tonemap, tone);
    bgfx::setViewUniform(draw.id, ibl_params, ibl);
    bgfx::setViewUniform(draw.id, capture_mode, capture_value);
    bgfx::setViewUniform(draw.id, frame_inputs, frame_value);
    bgfx::setViewUniform(draw.id, ao_settings, ao);
    bgfx::setViewUniform(draw.id, shadow_matrices, draw.shadow->matrices.data(),
                         render_detail::maximum_shadow_cascades);
    bgfx::setViewUniform(draw.id, shadow_rects, draw.shadow->rectangles.data(),
                         render_detail::maximum_shadow_cascades);
    bgfx::setViewUniform(draw.id, shadow_splits, draw.shadow->splits.data());
    bgfx::setViewUniform(draw.id, shadow_params, draw.shadow->parameters.data());
    bgfx::setViewUniform(draw.id, shadow_settings, shadow_values);
    draw.cluster->bind_view(draw.id);
    for (auto& batch : batches)
    {
        while (batch.cursor < batch.items.size())
        {
            const auto& item = items[batch.items[batch.cursor]];
            const auto& mesh = frame_data.meshes[item.mesh];
            const auto& material = mesh.gpu->materials[item.slot];
            const auto* program = material.program;
            auto frame_textures = draw.textures;
            std::array<float, 4> valid{};
            for (std::size_t index = 0; index < frame_textures.size(); ++index)
            {
                valid[index] = bgfx::isValid(frame_textures[index]) ? 1.0F : 0.0F;
                if (!bgfx::isValid(frame_textures[index]))
                {
                    frame_textures[index] = ibl_lut_fallback;
                }
            }
            std::array<float, 16> planar_matrix{};
            bx::mtxIdentity(planar_matrix.data());
            if (!draw.reflection && !capture)
            {
                for (const auto& plane : planar)
                {
                    if (plane.node_id == mesh.gpu->node_id && bgfx::isValid(plane.texture()))
                    {
                        frame_textures[2] = plane.texture();
                        valid[2] = 1;
                        planar_matrix = plane.view_projection;
                        break;
                    }
                }
            }
            if (!program->bind_material(material, frame_textures, draw.max_anisotropy))
            {
                return false;
            }
            bgfx::setUniform(normal_transform, mesh.normal.data());
            bgfx::setUniform(frame_valid, valid.data());
            bgfx::setUniform(planar_projection, planar_matrix.data());
            bgfx::setTexture(render_detail::ambient_occlusion_stage, ao_sampler,
                             use_ao ? ambient_occlusion.texture() : shadow_fallback);
            bgfx::setTexture(render_detail::ibl_irradiance_stage, ibl_irradiance_sampler,
                             ibl_ready ? ibl_textures[0]->handle : ibl_cube_fallback);
            bgfx::setTexture(render_detail::ibl_prefilter_stage, ibl_prefilter_sampler,
                             ibl_ready ? ibl_textures[1]->handle : ibl_cube_fallback);
            bgfx::setTexture(render_detail::ibl_lut_stage, ibl_lut_sampler,
                             ibl_ready ? ibl_textures[2]->handle : ibl_lut_fallback);
            const float surface_value[]{material.cull != schema::CullMode::back ? 1.0F : 0.0F,
                                        static_cast<float>(material.blend), 0, 0};
            bgfx::setUniform(surface, surface_value);
            bgfx::setTexture(render_detail::shadow_sampler_stage, shadow_sampler,
                             bgfx::isValid(shadow.texture()) ? shadow.texture() : shadow_fallback);
            const auto& coefficients = item.coefficients;
            const auto lightmap = item.lightmap;
            const bool has_lightmap = item.has_lightmap, has_probe = item.has_probe;
            const float lighting[]{has_lightmap ? 1.0F : 0.0F, has_probe ? 1.0F : 0.0F, 0, 0};
            bgfx::setUniform(lighting_params, lighting);
            bgfx::setUniform(probe_sh, coefficients.data(), 9);
            bgfx::setTexture(6, lightmap_sampler, lightmap,
                             draw.max_anisotropy
                                 ? BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_MIN_ANISOTROPIC |
                                       BGFX_SAMPLER_MAG_ANISOTROPIC
                                 : UINT32_MAX);
            bgfx::InstanceDataBuffer instance_buffer{};
            constexpr std::uint16_t instance_stride = 7 * 4 * sizeof(float);
            const auto remaining = static_cast<std::uint32_t>(batch.items.size() - batch.cursor);
            const auto available =
                remaining > 1 ? bgfx::getAvailInstanceDataBuffer(remaining, instance_stride) : 0;
            const auto count = available >= 2 ? available : 1U;
            if (count > 1)
            {
                ENGINE_PROFILE_ZONE("Render.instanceData");
                bgfx::allocInstanceDataBuffer(&instance_buffer, count, instance_stride);
                for (std::uint32_t index = 0; index < count; ++index)
                {
                    const auto& instance =
                        frame_data.meshes[items[batch.items[batch.cursor + index]].mesh];
                    auto* destination = instance_buffer.data + index * instance_stride;
                    std::memcpy(destination, instance.model.data(), 16 * sizeof(float));
                    std::memcpy(destination + 16 * sizeof(float), instance.normal.data(),
                                12 * sizeof(float));
                }
                bgfx::setInstanceDataBuffer(&instance_buffer);
            }
            batch.cursor += count;
            draw.cluster->bind();
            bgfx::setTransform(mesh.model.data());
            if (item.geometry == mesh.gpu->geometry.get())
            {
                mesh.gpu->bind_vertices();
            }
            else
            {
                bgfx::setVertexBuffer(0, item.geometry->vertices);
            }
            bgfx::setIndexBuffer(item.geometry->index_buffer(item.lod), item.first, item.count);
            bgfx::setState(batch.key.state);
            ++stats.draw_calls;
            stats.submitted_instances += count;
            stats.triangles += static_cast<std::uint64_t>(item.count / 3) * count;
            stats.instance_batches += count > 1;
            bgfx::submit(draw.id, count > 1 ? bgfx::ProgramHandle{batch.key.program}
                                  : mode == SceneDrawMode::surface ? program->surface_program
                                                                   : program->program);
        }
    }
    const auto previous =
        std::find_if(view_stats.begin(), view_stats.end(),
                     [&stats](const auto& row) { return row.view_id == stats.view_id; });
    if (previous == view_stats.end())
    {
        view_stats.push_back(stats);
    }
    else
    {
        previous->candidate_meshes += stats.candidate_meshes;
        previous->culled_meshes += stats.culled_meshes;
        previous->visible_meshes += stats.visible_meshes;
        previous->draw_calls += stats.draw_calls;
        previous->instance_batches += stats.instance_batches;
        previous->submitted_instances += stats.submitted_instances;
        previous->triangles += stats.triangles;
    }
    return true;
}
} // namespace engine
