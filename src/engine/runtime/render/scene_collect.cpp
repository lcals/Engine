#include "runtime/render/scene_gpu_resources.hpp"

#include "runtime/performance/profiling.hpp"
#include "runtime/render/mesh_lod_selection.hpp"
#include "runtime/render/render_math.hpp"
#include "runtime/resource/types/light_probe_resource.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/light_probe_volume_component.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/planar_reflection_component.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/world/world_system.hpp"

#include <algorithm>
#include <bx/math.h>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace engine
{
namespace
{
bool sample_probe(const render_detail::RenderFrameData& frame, const std::array<float, 3>& point,
                  std::array<std::array<float, 4>, 9>& output)
{
    double selected_volume = std::numeric_limits<double>::max();
    std::uint64_t selected_id = UINT64_MAX;
    bool found = false;
    for (const auto& probe : frame.probes)
    {
        const auto* data = probe.data;
        const auto local = render_detail::transform_point(probe.inverse.data(), point);
        const std::array low{data->bounds_min.x(), data->bounds_min.y(), data->bounds_min.z()};
        const std::array high{data->bounds_max.x(), data->bounds_max.y(), data->bounds_max.z()};
        const std::array grid{data->grid_x, data->grid_y, data->grid_z};
        bool inside = true;
        const double size = probe.volume;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            inside = inside && local[axis] >= low[axis] && local[axis] <= high[axis];
        }
        if (!inside || size > selected_volume ||
            (size == selected_volume && probe.node_id >= selected_id))
        {
            continue;
        }
        std::array<std::uint32_t, 3> cell{};
        std::array<float, 3> fraction{};
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            const float position =
                static_cast<float>((local[axis] - low[axis]) / (high[axis] - low[axis]) *
                                   static_cast<float>(grid[axis] - 1));
            cell[axis] =
                std::min(static_cast<std::uint32_t>(position), grid[axis] > 1 ? grid[axis] - 2 : 0);
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
            const auto index = (coordinate[2] * grid[1] + coordinate[1]) * grid[0] + coordinate[0];
            if (weight == 0 || !data->valid[index])
            {
                continue;
            }
            total += weight;
            for (std::size_t band = 0; band < 9; ++band)
            {
                const auto& coefficient = data->coefficients[index * 9 + band];
                candidate[band][0] =
                    static_cast<float>(candidate[band][0] + coefficient.x() * weight);
                candidate[band][1] =
                    static_cast<float>(candidate[band][1] + coefficient.y() * weight);
                candidate[band][2] =
                    static_cast<float>(candidate[band][2] + coefficient.z() * weight);
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
        selected_id = probe.node_id;
        selected_volume = size;
    }
    return found;
}
} // namespace

bool RenderSystem::SceneGpuResources::collect_frame(const WorldSystem& world,
                                                    render_detail::RenderFrameData& output)
{
    ENGINE_PROFILE_ZONE("Render.collectFrame");
    output.meshes.clear();
    output.lights.clear();
    output.probes.clear();
    output.world_epoch = 0;
    const auto fail = [&]()
    {
        output.meshes.clear();
        output.lights.clear();
        output.probes.clear();
        return false;
    };
    auto ecs = flecs::world(world.native_world());
    auto probes =
        ecs.query_builder<const ecs_detail::ComponentStorage<LightProbeVolumeComponent::Data>>()
            .with<ecs_detail::Published>()
            .build();
    probes.each(
        [&](flecs::entity entity, const auto&)
        {
            auto volume = world.find_node(entity.id()).get_component<LightProbeVolumeComponent>();
            if (!volume || !volume.active() || !volume.visible_in_hierarchy())
            {
                return;
            }
            const LightProbeResource* resource = nullptr;
            const auto* loader = world.resource_loader();
            if (!loader || loader->get(volume.resource(), resource) != ResourceError::none)
            {
                return;
            }
            render_detail::RenderFrameData::Probe probe;
            probe.node_id = entity.id();
            probe.data = &resource->data();
            const auto& data = *probe.data;
            bx::mtxInverse(probe.inverse.data(), data.baked_transform.data());
            probe.volume =
                std::abs(render_detail::linear_determinant(data.baked_transform.data())) *
                (data.bounds_max.x() - data.bounds_min.x()) *
                (data.bounds_max.y() - data.bounds_min.y()) *
                (data.bounds_max.z() - data.bounds_min.z());
            output.probes.push_back(probe);
        });
    bool valid = true, uses_skinning = false;
    std::unordered_map<std::size_t, std::vector<const render_detail::MaterialBinding*>> bindings;
    std::size_t next_binding = 0;
    auto query = ecs.query_builder<const ecs_detail::ComponentStorage<MeshRenderer::Data>>()
                     .with<ecs_detail::Published>()
                     .build();
    query.each(
        [&](flecs::entity entity, const auto&)
        {
            auto node = world.find_node(entity.id());
            const auto renderer = node.get_component<MeshRenderer>();
            if (!valid || !renderer || !renderer.active() || !renderer.visible_in_hierarchy())
            {
                return;
            }
            const auto found =
                mesh_index.find({world.epoch(), entity.id(), renderer.instance_id()});
            if (found == mesh_index.end())
            {
                valid = false;
                return;
            }
            auto& gpu = meshes[found->second];
            render_detail::RenderFrameData::Mesh mesh;
            mesh.gpu = &gpu;
            mesh.data = renderer.mesh_data();
            const auto transform = node.get_component<Transform>();
            if (!mesh.data || !transform || gpu.revision != renderer.mesh_revision() ||
                !gpu.update_deformation(renderer, skinning) || gpu.materials.empty() ||
                !transform.world_matrix(mesh.model.data()) ||
                !render_detail::normal_matrix(mesh.model.data(), mesh.normal.data()))
            {
                valid = false;
                return;
            }
            uses_skinning = uses_skinning || bgfx::isValid(gpu.palette);
            for (auto& binding : gpu.materials)
            {
                const bool opaque = binding.blend == schema::BlendMode::opaque;
                mesh.all_opaque = mesh.all_opaque && opaque;
                mesh.any_opaque = mesh.any_opaque || opaque;
                mesh.any_transparent = mesh.any_transparent || !opaque;
                auto& bucket = bindings[binding.content_hash];
                const auto equal = std::find_if(bucket.begin(), bucket.end(), [&](const auto* value)
                                                { return *value == binding; });
                if (equal != bucket.end())
                {
                    binding.binding_id = (*equal)->binding_id;
                }
                else
                {
                    binding.binding_id = next_binding++;
                    bucket.push_back(&binding);
                }
            }
            const auto plane = node.get_component<PlanarReflectionComponent>();
            mesh.plane = static_cast<bool>(plane);
            mesh.active_plane = plane && plane.active() && plane.visible_in_hierarchy();
            mesh.instance_eligible = renderer.mesh_resource().id != 0 && mesh.data->skin.empty() &&
                                     renderer.deformed_vertices().empty() &&
                                     renderer.skinning_palette().empty();
            mesh.instancing = renderer.static_instancing_enabled();
            mesh.lod = renderer.static_lod_enabled();
            mesh.cast_shadows = renderer.cast_shadows();
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                mesh.center[axis] = (gpu.minimum[axis] + gpu.maximum[axis]) * .5F;
            }
            mesh.center = render_detail::transform_point(mesh.model.data(), mesh.center);
            mesh.lightmap = ibl_lut_fallback;
            const auto source = renderer.lightmap_resource();
            if (source.id)
            {
                const auto* texture = render_detail::find_texture(textures, source);
                if (!texture)
                {
                    valid = false;
                    return;
                }
                mesh.lightmap = texture->handle;
                mesh.has_lightmap = true;
            }
            else
            {
                mesh.has_probe = sample_probe(output, mesh.center, mesh.coefficients);
            }
            output.meshes.push_back(mesh);
        });
    if (!valid)
    {
        return fail();
    }
    if (!uses_skinning)
    {
        skinning.release();
    }
    auto lights = ecs.query_builder<const ecs_detail::ComponentStorage<Light::Data>>()
                      .with<ecs_detail::Published>()
                      .build();
    lights.each(
        [&](flecs::entity entity, const auto& stored)
        {
            auto node = world.find_node(entity.id());
            const auto light = node.get_component<Light>();
            if (!valid || !light || !light.active() || !light.visible_in_hierarchy())
            {
                return;
            }
            render_detail::LocalShadowLight value;
            value.node_id = entity.id();
            value.settings = stored.data;
            const auto transform = node.get_component<Transform>();
            if (!transform || !transform.world_matrix(value.world.data()))
            {
                valid = false;
                return;
            }
            output.lights.push_back(value);
        });
    if (!valid)
    {
        return fail();
    }
    output.world_epoch = world.epoch();
    return true;
}

bool RenderSystem::SceneGpuResources::collect_view(const render_detail::RenderFrameData& frame,
                                                   const render_detail::SceneDrawView& draw,
                                                   render_detail::ViewDrawList& output)
{
    ENGINE_PROFILE_ZONE("Render.collectView");
    output.opaque.clear();
    output.transparent.clear();
    output.opaque_stats = {};
    output.transparent_stats = {};
    output.observer = draw.lod_observer;
    const auto fail = [&]()
    {
        output.opaque.clear();
        output.transparent.clear();
        output.opaque_stats = {};
        output.transparent_stats = {};
        return false;
    };
    render_detail::FrustumPlanes planes{};
    const bool frustum_ready =
        draw.frustum_culling_enabled &&
        render_detail::build_frustum_planes(draw.view, draw.projection,
                                            bgfx::getCaps()->homogeneousDepth, planes);
    culling_applied = culling_applied || frustum_ready;
    for (std::size_t index = 0; index < frame.meshes.size(); ++index)
    {
        const auto& mesh = frame.meshes[index];
        const auto& gpu = *mesh.gpu;
        if ((draw.visibility && !draw.visibility.mesh_visible(gpu.instance_id)) ||
            (draw.reflection && mesh.active_plane) ||
            (draw.capture && std::find(draw.capture->node_ids.begin(), draw.capture->node_ids.end(),
                                       gpu.node_id) == draw.capture->node_ids.end()))
        {
            continue;
        }
        // 混合网格归不透明统计；透明独占网格归透明统计，延续主视图只计一次的口径。
        auto& stats = mesh.any_opaque ? output.opaque_stats : output.transparent_stats;
        ++stats.candidate_meshes;
        if (frustum_ready &&
            !render_detail::aabb_visible(planes, mesh.model.data(), gpu.minimum, gpu.maximum))
        {
            ++stats.culled_meshes;
            continue;
        }
        ++stats.visible_meshes;
        const auto* data = mesh.data;
        render_detail::ViewDrawList::Item item;
        item.mesh = index;
        item.geometry = gpu.geometry.get();
        item.lightmap = draw.capture ? ibl_lut_fallback : mesh.lightmap;
        item.has_lightmap = !draw.capture && mesh.has_lightmap;
        item.has_probe = !draw.capture && mesh.has_probe;
        item.coefficients = draw.capture ? decltype(item.coefficients){} : mesh.coefficients;
        if (draw.capture)
        {
            for (std::size_t entry = 0; entry < draw.capture->indirect.size(); ++entry)
            {
                if (draw.capture->indirect[entry].node_id == gpu.node_id)
                {
                    item.lightmap = capture_lightmaps[entry];
                    item.has_lightmap = true;
                    if (capture_meshes[entry])
                    {
                        item.geometry = capture_meshes[entry].get();
                        data = draw.capture->indirect[entry].mesh.get();
                    }
                    break;
                }
            }
        }
        if (!draw.capture && mesh.any_opaque)
        {
            const auto observer =
                static_cast<std::uint16_t>(draw.reflection ? draw.id : render_detail::main_view);
            const auto key = std::pair{gpu.instance_id, observer};
            if (mesh.instance_eligible && mesh.all_opaque && !mesh.has_lightmap && mesh.lod)
            {
                lod_applied = lod_applied || !data->lods.empty();
                std::uint64_t revision = gpu.revision;
                const auto combine = [&](std::uint64_t value)
                { revision ^= value + 0x9e3779b97f4a7c15ULL + (revision << 6) + (revision >> 2); };
                combine(reinterpret_cast<std::uintptr_t>(gpu.geometry->resource.get()));
                combine(draw.lod_observer[0]);
                combine(draw.lod_observer[1]);
                combine(draw.reflection);
                if (selected_lod_revisions[key] != revision)
                {
                    selected_lods.erase(key);
                    selected_lod_revisions[key] = revision;
                }
                const auto height = render_detail::projected_mesh_height(
                    gpu.minimum, gpu.maximum, mesh.model.data(), draw.view, draw.projection,
                    draw.near_clip);
                item.lod = render_detail::select_mesh_lod(*data, height, selected_lods[key]);
            }
            selected_lods[key] = item.lod;
        }
        const auto collect = [&](std::uint32_t first, std::uint32_t count, std::size_t slot)
        {
            if (slot >= gpu.materials.size() || !gpu.materials[slot].program)
            {
                return false;
            }
            const auto& material = gpu.materials[slot];
            if (draw.reflection && material.frame_textures)
            {
                return true;
            }
            item.slot = slot;
            item.first = first;
            item.count = count;
            if (material.blend == schema::BlendMode::opaque)
            {
                output.opaque.push_back(item);
                return true;
            }
            auto transparent = item;
            transparent.lod = 0;
            std::array<float, 3> low, high;
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
            std::array<float, 3> center;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                center[axis] = (low[axis] + high[axis]) * .5F;
            }
            center = render_detail::transform_point(mesh.model.data(), center);
            transparent.depth = -render_detail::transform_point(draw.view, center)[2];
            if (!draw.capture && !mesh.has_lightmap)
            {
                transparent.has_probe = sample_probe(frame, center, transparent.coefficients);
            }
            output.transparent.push_back(transparent);
            return true;
        };
        const auto ranges = draw.capture ? std::span<const schema::MeshSubmesh>(data->submeshes)
                                         : item.geometry->submeshes(item.lod);
        if (ranges.empty())
        {
            const auto count = draw.capture ? static_cast<std::uint32_t>(data->indices.size())
                                            : item.geometry->index_count(item.lod);
            if (!collect(0, count, 0))
            {
                return fail();
            }
        }
        else
        {
            for (const auto& range : ranges)
            {
                if (!collect(range.first_index(), range.index_count(), range.material_slot()))
                {
                    return fail();
                }
            }
        }
    }
    std::stable_sort(output.transparent.begin(), output.transparent.end(),
                     [](const auto& left, const auto& right) { return left.depth > right.depth; });
    return true;
}
} // namespace engine
