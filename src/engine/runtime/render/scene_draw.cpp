#include "runtime/render/render_constants.hpp"
#include "runtime/render/render_math.hpp"
#include "runtime/render/scene_gpu_resources.hpp"
#include "runtime/resource/resource.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/light_probe_volume_component.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/planar_reflection_component.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/world/world_system.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <bx/math.h>
#include <cmath>
#include <limits>

namespace engine
{
namespace
{
bool unlit_surface(const Material& material)
{
    const auto* shader = material.shader_data();
    if (!shader)
    {
        return false;
    }
    for (const auto& parameter : shader->parameters)
    {
        if (parameter->uniform_name != "u_material" || parameter->uniform_offset != 2)
        {
            continue;
        }
        const auto* value = parameter->default_value.AsFloatValue();
        if (!value)
        {
            return false;
        }
        float mode = value->value;
        if (parameter->exposed && !material.get_float(parameter->name, mode))
        {
            return false;
        }
        return mode > 0.5F && mode < 1.5F;
    }
    return false;
}

bool sample_probe(const WorldSystem& world, const std::array<float, 3>& point,
                  std::array<std::array<float, 4>, 9>& output)
{
    double selected_volume = std::numeric_limits<double>::max();
    std::uint64_t selected_id = UINT64_MAX;
    bool found = false;
    for (const auto& owner : world.scenes())
    {
        for (const auto& node : owner->nodes())
        {
            const auto* volume = node->get_component<LightProbeVolumeComponent>();
            if (!volume || !volume->enabled())
            {
                continue;
            }
            const auto resource = volume->resource();
            const auto* data = resource ? resource->light_probe() : nullptr;
            if (!data)
            {
                continue;
            }
            float inverse[16];
            bx::mtxInverse(inverse, data->baked_transform.data());
            const auto local = render_detail::transform_point(inverse, point);
            const std::array low{data->bounds_min.x(), data->bounds_min.y(), data->bounds_min.z()};
            const std::array high{data->bounds_max.x(), data->bounds_max.y(), data->bounds_max.z()};
            const std::array grid{data->grid_x, data->grid_y, data->grid_z};
            bool inside = true;
            double size = std::abs(render_detail::linear_determinant(data->baked_transform.data()));
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                inside = inside && local[axis] >= low[axis] && local[axis] <= high[axis];
                size *= high[axis] - low[axis];
            }
            if (!inside || size > selected_volume ||
                (size == selected_volume && node->id() >= selected_id))
            {
                continue;
            }
            std::array<std::uint32_t, 3> cell{};
            std::array<float, 3> fraction{};
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                const float position = (local[axis] - low[axis]) / (high[axis] - low[axis]) *
                                       static_cast<float>(grid[axis] - 1);
                cell[axis] = std::min(static_cast<std::uint32_t>(position),
                                      grid[axis] > 1 ? grid[axis] - 2 : 0);
                fraction[axis] = position - static_cast<float>(cell[axis]);
            }
            std::array<std::array<float, 4>, 9> candidate{};
            float total = 0;
            for (std::uint32_t corner = 0; corner < 8; ++corner)
            {
                std::array<std::uint32_t, 3> coordinate{};
                float weight = 1;
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    const bool upper = (corner & (1U << axis)) != 0;
                    coordinate[axis] =
                        std::min(cell[axis] + static_cast<std::uint32_t>(upper), grid[axis] - 1);
                    weight *= upper ? fraction[axis] : 1 - fraction[axis];
                }
                const auto index =
                    (coordinate[2] * grid[1] + coordinate[1]) * grid[0] + coordinate[0];
                if (weight == 0 || !data->valid[index])
                {
                    continue;
                }
                total += weight;
                for (std::size_t band = 0; band < 9; ++band)
                {
                    const auto& coefficient = data->coefficients[index * 9 + band];
                    candidate[band][0] += coefficient.x() * weight;
                    candidate[band][1] += coefficient.y() * weight;
                    candidate[band][2] += coefficient.z() * weight;
                }
            }
            if (total <= 0)
            {
                continue;
            }
            for (auto& coefficient : candidate)
            {
                for (auto& value : coefficient)
                {
                    value /= total;
                }
            }
            output = candidate;
            found = true;
            selected_id = node->id();
            selected_volume = size;
        }
    }
    return found;
}
} // namespace

bool RenderSystem::SceneGpuResources::prepare_lighting(
    const WorldSystem& world, const RenderSettings& rendering, const RenderConfig& config,
    render_detail::SceneDrawView& draw, const CameraSettings& lens,
    render_detail::ShadowFrame& frame, std::size_t cluster_index, bgfx::ViewId base,
    const SceneCaptureRequest* capture)
{
    std::vector<render_detail::ClusterLight> lights;
    std::vector<render_detail::LocalShadowLight> local_lights;
    std::uint32_t shadow_light = UINT32_MAX;
    auto light_settings = rendering;
    light_settings.directional_shadows = false;
    for (const auto& owner : world.scenes())
    {
        for (const auto& node : owner->nodes())
        {
            const auto* light = node->get_component<Light>();
            if (!light || !light->enabled())
            {
                continue;
            }
            if (capture && std::find(capture->light_node_ids.begin(), capture->light_node_ids.end(),
                                     node->id()) == capture->light_node_ids.end())
            {
                continue;
            }
            if (lights.size() >= config.maximum_lights)
            {
                return false;
            }
            std::array<float, 16> matrix{};
            const auto* transform = node->get_component<Transform>();
            if (!transform || !transform->world_matrix(matrix.data()))
            {
                return false;
            }
            const float length = std::hypot(matrix[8], matrix[9], matrix[10]);
            if (!(length > 0))
            {
                return false;
            }
            const auto& lighting = light->settings();
            constexpr float radians = 0.01745329251994329577F;
            const auto index = static_cast<std::uint32_t>(lights.size());
            render_detail::ClusterLight packed;
            packed.position_range = {matrix[12], matrix[13], matrix[14], lighting.range};
            packed.direction_outer = {-matrix[8] / length, -matrix[9] / length,
                                      -matrix[10] / length,
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
                    local_lights.push_back({node->id(), index, lighting, matrix});
                }
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
            const auto mesh = std::find_if(meshes.begin(), meshes.end(),
                                           [id](const auto& value) { return value.node_id == id; });
            const auto* node = world.find_node(id);
            const auto* transform = node ? node->get_component<Transform>() : nullptr;
            std::array<float, 16> model{};
            if (mesh == meshes.end() || !transform || !transform->world_matrix(model.data()))
            {
                return false;
            }
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
    if (!shadow.prepare(config.shadow_atlas_size) ||
        !shadow.draw(world, light_settings, frame, meshes, textures, shadow_fallback, shadow_depth,
                     config, local_lights, lights, draw.world, base,
                     capture ? &caster_ids : nullptr))
    {
        return false;
    }
    auto& cluster = clusters[cluster_index];
    if (!cluster.prepare(config, draw.width, draw.height) ||
        !cluster.cull(static_cast<bgfx::ViewId>(base + 28), lens, draw.view, draw.projection,
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

bool RenderSystem::SceneGpuResources::draw_meshes(const WorldSystem& world,
                                                  const RenderSettings& settings,
                                                  const render_detail::SceneDrawView& draw,
                                                  render_detail::SceneDrawMode mode)
{
    using render_detail::SceneDrawMode;
    const bool depth_only = mode == SceneDrawMode::depth;
    const bool capture = mode == SceneDrawMode::capture;
    const bool transparent = mode == SceneDrawMode::transparent;
    const bool ibl_ready =
        settings.ibl_enabled &&
        std::all_of(ibl_textures.begin(), ibl_textures.end(),
                    [](const auto& texture) { return bgfx::isValid(texture.handle); });
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
        ibl_ready ? static_cast<float>(ibl_textures[1].resource->texture()->mip_count - 1) : 0, 0};
    const float shadow_values[]{draw.shadow_bias, draw.inverse_shadow_size,
                                static_cast<float>(draw.shadow_light),
                                draw.shadow->tile_size ? 1.0F : 0.0F};
    const float capture_value[]{capture ? 1.0F : 0.0F, 0, 0, 0};
    const float zero[4]{};
    const float frame_value[]{1.0F / draw.width, 1.0F / draw.height,
                              static_cast<float>(static_cast<double>(SDL_GetTicks()) / 1000.0),
                              bgfx::getCaps()->originBottomLeft ? 1.0F : 0.0F};
    struct Item
    {
        const MeshRenderer* renderer = nullptr;
        const render_detail::GpuMesh* mesh = nullptr;
        const render_detail::GpuGeometry* geometry = nullptr;
        const Material* material = nullptr;
        std::uint32_t first = 0, count = 0;
        std::array<float, 16> model{}, normal{};
        std::array<float, 3> center{};
        float depth = 0;
        std::size_t capture_index = SIZE_MAX;
    };
    std::vector<Item> items;
    for (const auto& owner : world.scenes())
    {
        for (const auto& node : owner->nodes())
        {
            const auto* renderer = node->get_component<MeshRenderer>();
            if (!renderer || !renderer->enabled() || !renderer->visible() ||
                (draw.visibility && !draw.visibility->mesh_visible(renderer->mesh_instance_id())))
            {
                continue;
            }
            if (draw.capture &&
                std::find(draw.capture->node_ids.begin(), draw.capture->node_ids.end(),
                          node->id()) == draw.capture->node_ids.end())
            {
                continue;
            }
            if (draw.reflection)
            {
                const auto* plane = node->get_component<PlanarReflectionComponent>();
                if (plane && plane->enabled())
                {
                    continue;
                }
            }
            const auto mesh = std::find_if(meshes.begin(), meshes.end(), [&node](const auto& entry)
                                           { return entry.node_id == node->id(); });
            const auto* transform = node->get_component<Transform>();
            const auto* data = renderer->mesh_data();
            Item item;
            item.renderer = renderer;
            if (mesh == meshes.end() || !transform || !data ||
                !transform->world_matrix(item.model.data()) ||
                !render_detail::normal_matrix(item.model.data(), item.normal.data()))
            {
                return false;
            }
            item.mesh = &*mesh;
            item.geometry = mesh->geometry.get();
            if (draw.capture)
            {
                for (std::size_t index = 0; index < draw.capture->indirect.size(); ++index)
                {
                    if (draw.capture->indirect[index].node_id == node->id())
                    {
                        item.capture_index = index;
                        if (capture_meshes[index])
                        {
                            item.geometry = capture_meshes[index].get();
                            data = draw.capture->indirect[index].mesh.get();
                        }
                        break;
                    }
                }
            }
            const auto collect = [&](std::uint32_t first, std::uint32_t count, std::size_t slot)
            {
                const auto* material = renderer->material(slot);
                if (!material)
                {
                    return false;
                }
                const bool blended = material->blend_mode() != schema::BlendMode::opaque;
                if (blended != transparent)
                {
                    return true;
                }
                if (draw.reflection &&
                    std::any_of(
                        material->shader_data()->samplers.begin(),
                        material->shader_data()->samplers.end(), [](const auto& sampler)
                        { return sampler->source != schema::ShaderTextureSource::resource; }))
                {
                    return true;
                }
                item.material = material;
                item.first = first;
                item.count = count;
                std::array<float, 3> low = item.mesh->minimum, high = item.mesh->maximum;
                if (transparent)
                {
                    low.fill(std::numeric_limits<float>::max());
                    high.fill(std::numeric_limits<float>::lowest());
                    for (std::uint32_t offset = first; offset < first + count; ++offset)
                    {
                        const auto& vertex = data->vertices[data->indices[offset]];
                        const std::array point{vertex.x(), vertex.y(), vertex.z()};
                        for (std::size_t axis = 0; axis < 3; ++axis)
                        {
                            low[axis] = std::min(low[axis], point[axis]);
                            high[axis] = std::max(high[axis], point[axis]);
                        }
                    }
                }
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    item.center[axis] = (low[axis] + high[axis]) * 0.5F;
                }
                item.center = render_detail::transform_point(item.model.data(), item.center);
                item.depth = -render_detail::transform_point(draw.view, item.center)[2];
                items.push_back(item);
                return true;
            };
            if (data->submeshes.empty())
            {
                if (!collect(0, static_cast<std::uint32_t>(data->indices.size()), 0))
                {
                    return false;
                }
            }
            else
            {
                for (const auto& submesh : data->submeshes)
                {
                    if (!collect(submesh.first_index(), submesh.index_count(),
                                 submesh.material_slot()))
                    {
                        return false;
                    }
                }
            }
        }
    }
    if (transparent)
    {
        std::stable_sort(items.begin(), items.end(), [](const Item& left, const Item& right)
                         { return left.depth > right.depth; });
    }
    for (const auto& item : items)
    {
        const auto& material = *item.material;
        auto program = std::find_if(programs.begin(), programs.end(), [&material](const auto& entry)
                                    { return entry.path == material.shader_path(); });
        if (program == programs.end())
        {
            return false;
        }
        if (mode == SceneDrawMode::surface && !bgfx::isValid(program->surface_program))
        {
            continue;
        }
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
                if (plane.node_id == item.mesh->node_id && bgfx::isValid(plane.texture()))
                {
                    frame_textures[2] = plane.texture();
                    valid[2] = 1;
                    planar_matrix = plane.view_projection;
                    break;
                }
            }
        }
        if (!program->bind_material(material, textures, frame_textures))
        {
            return false;
        }
        bgfx::setUniform(normal_transform, item.normal.data());
        bgfx::setUniform(camera_position, camera_position_value);
        bgfx::setUniform(light_direction, zero);
        bgfx::setUniform(light_color, zero);
        bgfx::setUniform(ambient, ambient_value);
        bgfx::setUniform(ambient_sky, sky);
        bgfx::setUniform(ambient_equator, equator);
        bgfx::setUniform(ambient_ground, ground);
        bgfx::setUniform(fog_color, fog);
        bgfx::setUniform(fog_range, fog_range_value);
        bgfx::setUniform(tonemap, tone);
        bgfx::setUniform(ibl_params, ibl);
        bgfx::setUniform(capture_mode, capture_value);
        bgfx::setUniform(frame_inputs, frame_value);
        bgfx::setUniform(frame_valid, valid.data());
        bgfx::setUniform(planar_projection, planar_matrix.data());
        const bool use_ao = draw.ambient_occlusion && !depth_only && mode != SceneDrawMode::surface;
        const float ao[]{1.0F / draw.width, 1.0F / draw.height, use_ao ? 1.0F : 0.0F, 0};
        bgfx::setUniform(ao_settings, ao);
        bgfx::setTexture(render_detail::ambient_occlusion_stage, ao_sampler,
                         use_ao ? ambient_occlusion.texture() : shadow_fallback);
        bgfx::setTexture(render_detail::ibl_irradiance_stage, ibl_irradiance_sampler,
                         ibl_ready ? ibl_textures[0].handle : ibl_cube_fallback);
        bgfx::setTexture(render_detail::ibl_prefilter_stage, ibl_prefilter_sampler,
                         ibl_ready ? ibl_textures[1].handle : ibl_cube_fallback);
        bgfx::setTexture(render_detail::ibl_lut_stage, ibl_lut_sampler,
                         ibl_ready ? ibl_textures[2].handle : ibl_lut_fallback);
        const float surface_value[]{material.cull_mode() != schema::CullMode::back ? 1.0F : 0.0F,
                                    static_cast<float>(material.blend_mode()), 0, 0};
        bgfx::setUniform(surface, surface_value);
        bgfx::setUniform(shadow_matrices, draw.shadow->matrices.data(),
                         render_detail::maximum_shadow_cascades);
        bgfx::setUniform(shadow_rects, draw.shadow->rectangles.data(),
                         render_detail::maximum_shadow_cascades);
        bgfx::setUniform(shadow_splits, draw.shadow->splits.data());
        bgfx::setUniform(shadow_params, draw.shadow->parameters.data());
        bgfx::setUniform(shadow_settings, shadow_values);
        bgfx::setTexture(render_detail::shadow_sampler_stage, shadow_sampler,
                         bgfx::isValid(shadow.texture()) ? shadow.texture() : shadow_fallback);
        std::array<std::array<float, 4>, 9> coefficients{};
        bgfx::TextureHandle lightmap = ibl_lut_fallback;
        bool has_lightmap = false;
        if (capture && item.capture_index != SIZE_MAX)
        {
            lightmap = capture_lightmaps[item.capture_index];
            has_lightmap = true;
        }
        else if (!capture)
        {
            const auto source = item.renderer->lightmap_resource();
            if (source)
            {
                const auto texture =
                    std::find_if(textures.begin(), textures.end(),
                                 [&source](const auto& entry) { return entry.resource == source; });
                if (texture == textures.end())
                {
                    return false;
                }
                lightmap = texture->handle;
                has_lightmap = true;
            }
        }
        const bool has_probe =
            !capture && !has_lightmap && sample_probe(world, item.center, coefficients);
        const float lighting[]{has_lightmap ? 1.0F : 0.0F, has_probe ? 1.0F : 0.0F, 0, 0};
        bgfx::setUniform(lighting_params, lighting);
        bgfx::setUniform(probe_sh, coefficients.data(), 9);
        bgfx::setTexture(6, lightmap_sampler, lightmap);
        draw.cluster->bind();
        bgfx::setTransform(item.model.data());
        if (item.geometry == item.mesh->geometry.get())
        {
            item.mesh->bind_vertices();
        }
        else
        {
            bgfx::setVertexBuffer(0, item.geometry->vertices);
        }
        bgfx::setIndexBuffer(item.geometry->indices, item.first, item.count);
        auto cull = capture ? std::uint64_t{0}
                            : render_detail::cull_state(material.cull_mode(), item.model.data());
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
        std::uint64_t state = cull | BGFX_STATE_DEPTH_TEST_LESS;
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
            state = cull | BGFX_STATE_DEPTH_TEST_EQUAL | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
        }
        if (transparent)
        {
            // Color alpha remains coverage. The target alpha tracks tone-mapped lighting,
            // so unlit coverage removes the previous contribution without adding its own.
            const auto alpha_source =
                unlit_surface(material) ? BGFX_STATE_BLEND_ZERO : BGFX_STATE_BLEND_ONE;
            state |=
                material.blend_mode() == schema::BlendMode::premultiplied
                    ? BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_ONE,
                                                     BGFX_STATE_BLEND_INV_SRC_ALPHA, alpha_source,
                                                     BGFX_STATE_BLEND_INV_SRC_ALPHA)
                    : BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA,
                                                     BGFX_STATE_BLEND_INV_SRC_ALPHA, alpha_source,
                                                     BGFX_STATE_BLEND_INV_SRC_ALPHA);
        }
        bgfx::setState(state);
        bgfx::submit(draw.id,
                     mode == SceneDrawMode::surface ? program->surface_program : program->program);
    }
    return true;
}
} // namespace engine
