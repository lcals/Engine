#include "runtime/render/mesh_lod_selection.hpp"
#include "runtime/render/render_constants.hpp"
#include "runtime/render/render_math.hpp"
#include "runtime/render/scene_gpu_resources.hpp"
#include "runtime/resource/types/light_probe_resource.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/ecs_scene.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/light_probe_volume_component.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/planar_reflection_component.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/utils/hash_utils.hpp"
#include "runtime/world/world_system.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <bx/math.h>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>

namespace engine
{
namespace
{
template <typename T> void append_key(std::string& key, const T& value)
{
    key.append(reinterpret_cast<const char*>(&value), sizeof(value));
}

bool append_material_key(ResourceLoader* loader, const Material& material, std::string& key)
{
    const auto* shader = material.shader_data();
    if (!shader || shader->instanced_vertex_code.empty())
    {
        return false;
    }
    const auto shader_identity = reinterpret_cast<std::uintptr_t>(shader);
    append_key(key, shader_identity);
    append_key(key, material.cull_mode());
    append_key(key, material.blend_mode());
    for (const auto& parameter : shader->parameters)
    {
        switch (parameter->default_value.type)
        {
        case schema::MaterialValue::FloatValue:
        {
            float value = parameter->default_value.AsFloatValue()->value;
            if (parameter->exposed && !material.get_float(parameter->name, value))
            {
                return false;
            }
            append_key(key, value);
            break;
        }
        case schema::MaterialValue::Vec2Value:
        {
            Vec2 value = parameter->default_value.AsVec2Value()->value;
            if (parameter->exposed && !material.get_v2(parameter->name, value))
            {
                return false;
            }
            append_key(key, value.x());
            append_key(key, value.y());
            break;
        }
        case schema::MaterialValue::Vec3Value:
        {
            Vec3 value = parameter->default_value.AsVec3Value()->value;
            if (parameter->exposed && !material.get_v3(parameter->name, value))
            {
                return false;
            }
            append_key(key, value.x());
            append_key(key, value.y());
            append_key(key, value.z());
            break;
        }
        case schema::MaterialValue::Vec4Value:
        {
            Vec4 value = parameter->default_value.AsVec4Value()->value;
            if (parameter->exposed && !material.get_v4(parameter->name, value))
            {
                return false;
            }
            append_key(key, value.x());
            append_key(key, value.y());
            append_key(key, value.z());
            append_key(key, value.w());
            break;
        }
        default:
            return false;
        }
    }
    for (const auto& sampler : shader->samplers)
    {
        if (sampler->source != schema::ShaderTextureSource::resource)
        {
            return false;
        }
        const auto resource = material.texture(sampler->name);
        const Resource* value = nullptr;
        if (!loader || loader->get(resource, value) != ResourceError::none)
        {
            return false;
        }
        const auto identity = reinterpret_cast<std::uintptr_t>(value);
        append_key(key, identity);
    }
    return true;
}

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
    {
        const auto process =
            [&](Node node,
                const ecs_detail::ComponentStorage<LightProbeVolumeComponent::Data>& stored) -> bool
        {
            const auto* owner = node.scene();
            (void)owner;
            (void)stored;

            const auto volume = node.get_component<LightProbeVolumeComponent>();
            if (!volume || !volume.active() || !volume.visible_in_hierarchy())
            {
                return true;
            }
            const auto resource = volume.resource();
            const LightProbeResource* probe = nullptr;
            const auto* loader = world.resource_loader();
            const auto* data = loader && loader->get(resource, probe) == ResourceError::none
                                   ? &probe->data()
                                   : nullptr;
            if (!data)
            {
                return true;
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
                (size == selected_volume && node.id() >= selected_id))
            {
                return true;
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
                return true;
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
            selected_id = node.id();
            selected_volume = size;

            return true;
        };
        auto query = flecs::world(world.native_world())
                         .query_builder<
                             const ecs_detail::ComponentStorage<LightProbeVolumeComponent::Data>>()
                         .with<ecs_detail::Published>()
                         .build();
        bool query_success = true;
        query.each(
            [&](flecs::entity entity,
                const ecs_detail::ComponentStorage<LightProbeVolumeComponent::Data>& stored)
            {
                if (query_success)
                {
                    query_success = process(world.find_node(entity.id()), stored);
                }
            });
        (void)query_success;
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
    {
        const auto process = [&](Node node,
                                 const ecs_detail::ComponentStorage<Light::Data>& stored) -> bool
        {
            const auto* owner = node.scene();
            (void)owner;
            (void)stored;

            const auto light = node.get_component<Light>();
            if (!light || !light.active() || !light.visible_in_hierarchy())
            {
                return true;
            }
            if (capture && std::find(capture->light_node_ids.begin(), capture->light_node_ids.end(),
                                     node.id()) == capture->light_node_ids.end())
            {
                return true;
            }
            if (lights.size() >= config.maximum_lights)
            {
                return false;
            }
            std::array<float, 16> matrix{};
            const auto transform = node.get_component<Transform>();
            if (!transform || !transform.world_matrix(matrix.data()))
            {
                return false;
            }
            const float length = std::hypot(matrix[8], matrix[9], matrix[10]);
            if (!(length > 0))
            {
                return false;
            }
            const auto& lighting = stored.data;
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
                    local_lights.push_back({node.id(), index, lighting, matrix});
                }
            }

            return true;
        };
        auto query = flecs::world(world.native_world())
                         .query_builder<const ecs_detail::ComponentStorage<Light::Data>>()
                         .with<ecs_detail::Published>()
                         .build();
        bool query_success = true;
        query.each(
            [&](flecs::entity entity, const ecs_detail::ComponentStorage<Light::Data>& stored)
            {
                if (query_success)
                {
                    query_success = process(world.find_node(entity.id()), stored);
                }
            });
        if (!query_success)
        {
            return false;
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
            const auto node = world.find_node(id);
            const auto transform = node ? node.get_component<Transform>() : Transform{};
            std::array<float, 16> model{};
            if (mesh == meshes.end() || !transform || !transform.world_matrix(model.data()))
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
    const bool culling = draw.frustum_culling_enabled;
    const bool instancing = bgfx::getCaps()->limits.maxInstanceData >= 7;
    render_detail::FrustumPlanes planes{};
    const bool frustum_ready =
        culling && render_detail::build_frustum_planes(draw.view, draw.projection,
                                                       bgfx::getCaps()->homogeneousDepth, planes);
    RenderViewStats stats{};
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
        ibl_ready
            ? static_cast<float>(ibl_textures[1].resource.data<TextureResource>()->mip_count - 1)
            : 0,
        0};
    const float shadow_values[]{draw.shadow_bias, draw.inverse_shadow_size,
                                static_cast<float>(draw.shadow_light),
                                draw.shadow->tile_size ? 1.0F : 0.0F};
    const float capture_value[]{capture ? 1.0F : 0.0F, 0, 0, 0};
    const float zero[4]{};
    const float frame_value[]{1.0F / draw.width, 1.0F / draw.height, frame_time[0],
                              bgfx::getCaps()->originBottomLeft ? 1.0F : 0.0F};
    struct Item
    {
        MeshRenderer renderer = {};
        const render_detail::GpuMesh* mesh = nullptr;
        const render_detail::GpuGeometry* geometry = nullptr;
        Material material = {};
        std::uint32_t first = 0, count = 0;
        std::size_t lod = 0;
        bool instance_eligible = false;
        std::array<float, 16> model{}, normal{};
        std::array<float, 3> center{};
        float depth = 0;
        std::size_t capture_index = SIZE_MAX;
        std::array<std::array<float, 4>, 9> coefficients{};
        bgfx::TextureHandle lightmap = BGFX_INVALID_HANDLE;
        bool has_lightmap = false, has_probe = false;
        std::string batch_key;
    };
    std::vector<Item> items;
    {
        const auto process =
            [&](Node node, const ecs_detail::ComponentStorage<MeshRenderer::Data>& stored) -> bool
        {
            const auto* owner = node.scene();
            (void)owner;
            (void)stored;

            const auto renderer = node.get_component<MeshRenderer>();
            if (!renderer || !renderer.active() || !renderer.visible_in_hierarchy() ||
                (draw.visibility && !draw.visibility.mesh_visible(renderer.instance_id())))
            {
                return true;
            }
            if (draw.capture &&
                std::find(draw.capture->node_ids.begin(), draw.capture->node_ids.end(),
                          node.id()) == draw.capture->node_ids.end())
            {
                return true;
            }
            if (draw.reflection)
            {
                const auto plane = node.get_component<PlanarReflectionComponent>();
                if (plane && plane.active() && plane.visible_in_hierarchy())
                {
                    return true;
                }
            }
            const auto mesh = std::find_if(meshes.begin(), meshes.end(), [&node](const auto& entry)
                                           { return entry.node_id == node.id(); });
            const auto transform = node.get_component<Transform>();
            const auto* data = renderer.mesh_data();
            Item item;
            item.renderer = renderer;
            if (mesh == meshes.end() || !transform || !data ||
                !transform.world_matrix(item.model.data()) ||
                !render_detail::normal_matrix(item.model.data(), item.normal.data()))
            {
                return false;
            }
            bool all_opaque = true, any_opaque = false, any_transparent = false;
            for (std::size_t slot = 0; slot < renderer.material_count(); ++slot)
            {
                const auto material = renderer.material(slot);
                if (!material)
                {
                    return false;
                }
                const bool opaque = material.blend_mode() == schema::BlendMode::opaque;
                all_opaque = all_opaque && opaque;
                any_opaque = any_opaque || opaque;
                any_transparent = any_transparent || !opaque;
            }
            if (!(transparent ? any_transparent : any_opaque))
            {
                return true;
            }
            // The main color view runs opaque and transparent passes. Count a mixed
            // mesh once while retaining both passes' actual draw/triangle counts.
            const bool count_mesh = !transparent || !any_opaque;
            stats.candidate_meshes += count_mesh;
            culling_applied = culling_applied || frustum_ready;
            const bool hidden =
                frustum_ready && !render_detail::aabb_visible(planes, item.model.data(),
                                                              mesh->minimum, mesh->maximum);
            if (hidden)
            {
                stats.culled_meshes += count_mesh;
                return true;
            }
            stats.visible_meshes += count_mesh;
            item.mesh = &*mesh;
            item.geometry = mesh->geometry.get();
            if (draw.capture)
            {
                for (std::size_t index = 0; index < draw.capture->indirect.size(); ++index)
                {
                    if (draw.capture->indirect[index].node_id == node.id())
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
            const auto observer =
                static_cast<std::uint16_t>(draw.reflection ? draw.id : render_detail::main_view);
            const auto selection_key = std::pair{renderer.instance_id(), observer};
            item.instance_eligible = renderer.mesh_resource().id != 0 && data->skin.empty() &&
                                     renderer.deformed_vertices().empty() &&
                                     renderer.skinning_palette().empty() && !draw.capture;
            if (!transparent && !draw.capture)
            {
                if (item.instance_eligible && all_opaque && renderer.lightmap_resource().id == 0 &&
                    renderer.static_lod_enabled())
                {
                    lod_applied = lod_applied || !data->lods.empty();
                    // Keep main passes together. A temporary reflection view id may be
                    // reused by another plane, but camera movement must retain hysteresis.
                    std::string revision;
                    append_key(revision, renderer.mesh_revision());
                    append_key(revision,
                               reinterpret_cast<std::uintptr_t>(mesh->geometry->resource.get()));
                    append_key(revision, draw.lod_observer);
                    append_key(revision, draw.reflection);
                    const auto identity = hash::fnv1a(revision);
                    if (selected_lod_revisions[selection_key] != identity)
                    {
                        selected_lods.erase(selection_key);
                        selected_lod_revisions[selection_key] = identity;
                    }
                    const auto height = render_detail::projected_mesh_height(
                        mesh->minimum, mesh->maximum, item.model.data(), draw.view, draw.projection,
                        draw.near_clip);
                    item.lod =
                        render_detail::select_mesh_lod(*data, height, selected_lods[selection_key]);
                }
                selected_lods[selection_key] = item.lod;
            }
            const auto collect = [&](std::uint32_t first, std::uint32_t count, std::size_t slot)
            {
                const auto material = renderer.material(slot);
                if (!material)
                {
                    return false;
                }
                const bool blended = material.blend_mode() != schema::BlendMode::opaque;
                if (blended != transparent)
                {
                    return true;
                }
                if (draw.reflection &&
                    std::any_of(
                        material.shader_data()->samplers.begin(),
                        material.shader_data()->samplers.end(), [](const auto& sampler)
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
            const auto ranges = capture ? std::span<const schema::MeshSubmesh>(data->submeshes)
                                        : item.geometry->submeshes(item.lod);
            if (ranges.empty())
            {
                const auto index_count = capture ? static_cast<std::uint32_t>(data->indices.size())
                                                 : item.geometry->index_count(item.lod);
                if (!collect(0, index_count, 0))
                {
                    return false;
                }
            }
            else
            {
                for (const auto& submesh : ranges)
                {
                    if (!collect(submesh.first_index(), submesh.index_count(),
                                 submesh.material_slot()))
                    {
                        return false;
                    }
                }
            }

            return true;
        };
        auto query = flecs::world(world.native_world())
                         .query_builder<const ecs_detail::ComponentStorage<MeshRenderer::Data>>()
                         .with<ecs_detail::Published>()
                         .build();
        bool query_success = true;
        query.each(
            [&](flecs::entity entity,
                const ecs_detail::ComponentStorage<MeshRenderer::Data>& stored)
            {
                if (query_success)
                {
                    query_success = process(world.find_node(entity.id()), stored);
                }
            });
        if (!query_success)
        {
            return false;
        }
    }
    if (transparent)
    {
        std::stable_sort(items.begin(), items.end(), [](const Item& left, const Item& right)
                         { return left.depth > right.depth; });
    }
    std::map<std::string, std::vector<std::size_t>> batches;
    for (std::size_t index = 0; index < items.size(); ++index)
    {
        auto& item = items[index];
        item.lightmap = ibl_lut_fallback;
        if (capture && item.capture_index != SIZE_MAX)
        {
            item.lightmap = capture_lightmaps[item.capture_index];
            item.has_lightmap = true;
        }
        else if (!capture)
        {
            const auto source = item.renderer.lightmap_resource();
            if (source.id)
            {
                const auto texture =
                    std::find_if(textures.begin(), textures.end(),
                                 [&source](const auto& entry) { return entry.matches(source); });
                if (texture == textures.end())
                {
                    return false;
                }
                item.lightmap = texture->handle;
                item.has_lightmap = true;
            }
            else
            {
                item.has_probe = sample_probe(world, item.center, item.coefficients);
            }
        }
        const auto plane = item.renderer.node().get_component<PlanarReflectionComponent>();
        if (instancing && item.renderer.static_instancing_enabled() && !transparent && !capture &&
            item.instance_eligible && item.geometry == item.mesh->geometry.get() && !plane &&
            append_material_key(world.resource_loader(), item.material, item.batch_key))
        {
            const auto geometry = reinterpret_cast<std::uintptr_t>(item.geometry);
            append_key(item.batch_key, geometry);
            append_key(item.batch_key, item.first);
            append_key(item.batch_key, item.count);
            append_key(item.batch_key, item.lod);
            const auto cull =
                render_detail::cull_state(item.material.cull_mode(), item.model.data());
            append_key(item.batch_key, cull);
            append_key(item.batch_key, item.lightmap.idx);
            append_key(item.batch_key, item.has_lightmap);
            append_key(item.batch_key, item.has_probe);
            if (item.has_probe)
            {
                append_key(item.batch_key, item.coefficients);
            }
            batches[item.batch_key].push_back(index);
        }
        else
        {
            item.batch_key.clear();
        }
    }
    std::vector<bool> submitted(items.size(), false);
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
    for (std::size_t item_index = 0; item_index < items.size(); ++item_index)
    {
        if (submitted[item_index])
        {
            continue;
        }
        const auto& item = items[item_index];
        const auto material = item.material;
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
        if (!program->bind_material(material, textures, frame_textures, draw.max_anisotropy))
        {
            return false;
        }
        bgfx::setUniform(normal_transform, item.normal.data());
        bgfx::setUniform(frame_valid, valid.data());
        bgfx::setUniform(planar_projection, planar_matrix.data());
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
        std::vector<std::size_t> instance_items;
        bgfx::InstanceDataBuffer instance_buffer{};
        constexpr std::uint16_t instance_stride = 7 * 4 * sizeof(float);
        const auto instance_program = mode == SceneDrawMode::surface
                                          ? program->instanced_surface_program
                                          : program->instanced_program;
        if (!item.batch_key.empty() && bgfx::isValid(instance_program))
        {
            const auto& group = batches.find(item.batch_key)->second;
            const auto available = bgfx::getAvailInstanceDataBuffer(
                static_cast<std::uint32_t>(std::min<std::size_t>(group.size(), UINT32_MAX)),
                instance_stride);
            if (available >= 2)
            {
                for (const auto candidate : group)
                {
                    if (!submitted[candidate])
                    {
                        instance_items.push_back(candidate);
                    }
                    if (instance_items.size() == available)
                    {
                        break;
                    }
                }
                if (instance_items.size() >= 2)
                {
                    bgfx::allocInstanceDataBuffer(&instance_buffer,
                                                  static_cast<std::uint32_t>(instance_items.size()),
                                                  instance_stride);
                    for (std::size_t index = 0; index < instance_items.size(); ++index)
                    {
                        const auto& instance = items[instance_items[index]];
                        auto* destination = instance_buffer.data + index * instance_stride;
                        std::memcpy(destination, instance.model.data(), 16 * sizeof(float));
                        std::memcpy(destination + 16 * sizeof(float), instance.normal.data(),
                                    12 * sizeof(float));
                        submitted[instance_items[index]] = true;
                    }
                    bgfx::setInstanceDataBuffer(&instance_buffer);
                }
                else
                {
                    instance_items.clear();
                }
            }
        }
        submitted[item_index] = true;
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
        bgfx::setIndexBuffer(item.geometry->index_buffer(item.lod), item.first, item.count);
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
        if (draw.multisampling)
        {
            state |= BGFX_STATE_MSAA;
        }
        bgfx::setState(state);
        const auto instance_count =
            static_cast<std::uint32_t>(std::max<std::size_t>(1, instance_items.size()));
        ++stats.draw_calls;
        stats.submitted_instances += instance_count;
        stats.triangles += static_cast<std::uint64_t>(item.count / 3) * instance_count;
        if (!instance_items.empty())
        {
            ++stats.instance_batches;
        }
        bgfx::submit(draw.id, !instance_items.empty()          ? instance_program
                              : mode == SceneDrawMode::surface ? program->surface_program
                                                               : program->program);
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
