#include "plugins/runtime/destruction/destruction_system.hpp"

#include "plugins/runtime/destruction/destruction_component_runtime.hpp"

#include "plugins/runtime/destruction/destruction_component.hpp"
#include "runtime/performance/profiling.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/resource_binding.hpp"
#include "runtime/scene/rigid_body.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/utils/mesh_utils.hpp"
#include "runtime/world/world_system.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <future>
#include <limits>
#include <map>
#include <string>
#include <utility>

namespace engine::plugins::destruction
{
namespace
{
DestructionSystem* active_system = nullptr;

struct GeneratedGroup
{
    std::vector<std::uint32_t> chunks;
    std::uint64_t node = 0;
    bool anchored = false;
    double dynamic_age_seconds = 0;
    std::uint64_t renderer_instance = 0;
    std::uint64_t collider_instance = 0;
};

bool build_group_geometry(const schema::DestructionAssetDataT& asset,
                          const DestructionGroup& description,
                          const schema::DestructionDataT& settings, schema::MeshDataT& mesh,
                          schema::ColliderDataT& collision)
{
    collision.shape = schema::ColliderShape::compound;
    collision.friction = settings.friction;
    collision.restitution = settings.restitution;
    for (auto chunk_index : description.chunks)
    {
        const auto& source = *asset.chunks[chunk_index]->mesh;
        // Generate missing frames per chunk, after applying the same fallback UVs used
        // by the rendered mesh. Never discard frames supplied by the source asset.
        std::vector<schema::Vec4> tangents;
        if (!source.tangents.empty())
        {
            tangents = source.tangents;
        }
        else
        {
            schema::MeshDataT tangent_source = source;
            if (tangent_source.texcoords.empty())
            {
                for (const auto& vertex : source.vertices)
                {
                    tangent_source.texcoords.emplace_back(vertex.x() * 0.2, vertex.y() * 0.2);
                }
            }
            if (!MeshUtils::build_tangents(tangent_source, tangents))
            {
                return false;
            }
        }
        const auto offset = static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.insert(mesh.vertices.end(), source.vertices.begin(), source.vertices.end());
        mesh.tangents.insert(mesh.tangents.end(), tangents.begin(), tangents.end());
        for (auto vertex_index : source.indices)
        {
            mesh.indices.push_back(offset + vertex_index);
        }
        for (std::size_t vertex_index = 0; vertex_index < source.vertices.size(); ++vertex_index)
        {
            const auto& vertex = source.vertices[vertex_index];
            mesh.texcoords.push_back(source.texcoords.empty()
                                         ? Vec2{vertex.x() * 0.2, vertex.y() * 0.2}
                                         : source.texcoords[vertex_index]);
            mesh.colors.push_back(source.colors.empty() ? Color{1, 1, 1, 1}
                                                        : source.colors[vertex_index]);
        }
        auto hull = std::make_unique<schema::ColliderHullDataT>();
        for (const auto& vertex : source.vertices)
        {
            hull->vertices.emplace_back(vertex.x(), vertex.y(), vertex.z());
        }
        collision.hulls.push_back(std::move(hull));
    }
    return true;
}

bool finite(const Vec3& value)
{
    return std::isfinite(value.x()) && std::isfinite(value.y()) && std::isfinite(value.z());
}

bool active_component(DestructionComponent component)
{
    auto transform = component.node().get_component<Transform>();
    return component.active() && transform.valid() && transform.active();
}

bool valid_transform(const TransformSettings& value)
{
    const auto& scale = value.scale;
    return finite(value.position) && finite(value.rotation) && finite(scale) && scale.x() > 0 &&
           scale.x() <= 100 && std::abs(scale.x() - scale.y()) <= 0.000001 * scale.x() &&
           std::abs(scale.x() - scale.z()) <= 0.000001 * scale.x();
}

Vec3 center_of(const schema::DestructionAssetDataT& asset, const std::vector<std::uint32_t>& chunks,
               double& volume)
{
    volume = 0;
    double x = 0;
    double y = 0;
    double z = 0;
    for (auto index : chunks)
    {
        const auto& chunk = *asset.chunks[index];
        volume += chunk.volume;
        x += chunk.centroid.x() * chunk.volume;
        y += chunk.centroid.y() * chunk.volume;
        z += chunk.centroid.z() * chunk.volume;
    }
    return volume > 0 ? Vec3{x / volume, y / volume, z / volume} : Vec3{};
}

Vec3 world_point(const float* matrix, const Vec3& value)
{
    return {matrix[0] * value.x() + matrix[4] * value.y() + matrix[8] * value.z() + matrix[12],
            matrix[1] * value.x() + matrix[5] * value.y() + matrix[9] * value.z() + matrix[13],
            matrix[2] * value.x() + matrix[6] * value.y() + matrix[10] * value.z() + matrix[14]};
}

Vec3 local_point(const float* matrix, const Vec3& value, double scale)
{
    const double x = value.x() - matrix[12];
    const double y = value.y() - matrix[13];
    const double z = value.z() - matrix[14];
    const double squared = scale * scale;
    // The accepted transform is a rigid rotation and a positive uniform scale.
    return {(matrix[0] * x + matrix[1] * y + matrix[2] * z) / squared,
            (matrix[4] * x + matrix[5] * y + matrix[6] * z) / squared,
            (matrix[8] * x + matrix[9] * y + matrix[10] * z) / squared};
}

bool support_coverage(const schema::DestructionChunkDataT& chunk, const float* matrix,
                      Collider support, const PhysicsSystem& physics, float max_gap,
                      double& coverage, std::string& error)
{
    coverage = 0;
    const auto& mesh = *chunk.mesh;
    double bottom = mesh.vertices.front().y();
    double top = bottom;
    for (const auto& vertex : mesh.vertices)
    {
        bottom = std::min(bottom, static_cast<double>(vertex.y()));
        top = std::max(top, static_cast<double>(vertex.y()));
    }
    const double plane_tolerance = std::max(1.0, top - bottom) * 1.0e-5;
    const double lift = std::clamp(static_cast<double>(max_gap) * 0.01, 0.0001, 0.01);
    const double length = max_gap + 2 * lift;
    double total_area = 0;
    double covered_area = 0;
    for (std::size_t index = 0; index < mesh.indices.size(); index += 3)
    {
        const auto& first = mesh.vertices[mesh.indices[index]];
        const auto& second = mesh.vertices[mesh.indices[index + 1]];
        const auto& third = mesh.vertices[mesh.indices[index + 2]];
        if (first.y() > bottom + plane_tolerance || second.y() > bottom + plane_tolerance ||
            third.y() > bottom + plane_tolerance)
        {
            continue;
        }
        const Vec3 points[] = {world_point(matrix, {first.x(), first.y(), first.z()}),
                               world_point(matrix, {second.x(), second.y(), second.z()}),
                               world_point(matrix, {third.x(), third.y(), third.z()})};
        // Twice the world-XZ projected area; the factor cancels in the coverage ratio.
        const double area =
            std::abs((points[1].x() - points[0].x()) * (points[2].z() - points[0].z()) -
                     (points[1].z() - points[0].z()) * (points[2].x() - points[0].x()));
        if (!std::isfinite(area))
        {
            error = "Destruction foundation projection is not finite";
            return false;
        }
        if (area <= 0)
        {
            continue;
        }
        total_area += area;
        // Three interior barycentric samples avoid counting shared triangle edges twice.
        for (std::size_t sample = 0; sample < 3; ++sample)
        {
            const Vec3 point{
                (points[0].x() + points[1].x() + points[2].x()) / 6 + points[sample].x() / 2,
                (points[0].y() + points[1].y() + points[2].y()) / 6 + points[sample].y() / 2,
                (points[0].z() + points[1].z() + points[2].z()) / 6 + points[sample].z() / 2};
            float fraction = 0;
            if (!physics.cast_ray(support, {point.x(), point.y() + lift, point.z()},
                                  {0, -length, 0}, fraction))
            {
                error = "Cannot query the committed foundation collider";
                return false;
            }
            if (fraction < 1 && fraction * length <= max_gap + lift)
            {
                covered_area += area / 3;
            }
        }
    }
    if (!std::isfinite(total_area) || total_area <= 0)
    {
        error = "Destruction anchor has no planar bottom with a world-down projection";
        return false;
    }
    coverage = std::clamp(covered_area / total_area, 0.0, 1.0);
    return true;
}

bool release_groups(WorldSystem& world, PhysicsSystem& physics,
                    const std::vector<GeneratedGroup>& groups)
{
    std::vector<std::pair<Collider, bool>> disabled;
    // Keep every node alive until the whole batch has released its collision successfully.
    for (const auto& group : groups)
    {
        auto node = world.find_node(group.node);
        if (!node)
        {
            continue;
        }
        if (auto collider = node.get_component<Collider>())
        {
            const bool enabled = collider.enabled();
            if (!collider.set_enabled(false))
            {
                return false;
            }
            if (!physics.set_collider_data(collider, collider.settings()))
            {
                bool restored = collider.set_enabled(enabled);
                for (auto& [previous, previous_enabled] : disabled)
                {
                    if (!previous.set_enabled(previous_enabled) ||
                        !physics.set_collider_data(previous, previous.settings()))
                    {
                        restored = false;
                    }
                }
                if (!restored)
                {
                    std::fputs("Cannot restore destruction collision after failed release\n",
                               stderr);
                }
                return false;
            }
            disabled.emplace_back(collider, enabled);
        }
    }
    bool result = true;
    for (const auto& group : groups)
    {
        if (auto node = world.find_node(group.node))
        {
            result = node.scene()->destroy_node(group.node) && result;
        }
    }
    return result;
}

bool read_group(const Scene& scene, const GeneratedGroup& group,
                schema::DestructionGroupStateT& output)
{
    auto node = scene.find_node(group.node);
    auto transform = node ? node.get_component<Transform>() : Transform{};
    auto body = node ? node.get_component<RigidBody>() : RigidBody{};
    if (!transform || !body || !node.transient())
    {
        return false;
    }
    output.chunks = group.chunks;
    output.transform = std::make_unique<TransformSettings>(transform.settings());
    output.linear_velocity = body.settings().linear_velocity;
    output.angular_velocity = body.settings().angular_velocity;
    return true;
}

} // namespace

struct DestructionSystem::PreparedGroup
{
    schema::MeshDataT mesh;
    schema::ColliderDataT collision;
};

struct DestructionSystem::State
{
    const ecs_query_t* owners_query = nullptr;
    WorldSystem* query_world = nullptr;
    std::uint64_t query_epoch = 0;

    ~State()
    {
        if (owners_query && query_world && query_world->native_world() &&
            query_world->epoch() == query_epoch)
        {
            ecs_query_fini(const_cast<ecs_query_t*>(owners_query));
        }
    }

    struct SourceGroup
    {
        DestructionGroup group;
        std::array<float, 16> matrix{};
        double scale = 1;
    };
    struct Prepared
    {
        DestructionGraph graph;
        std::vector<SourceGroup> sources;
        std::vector<PreparedGroup> groups;
        std::vector<Vec3> velocity_changes;
        std::string error;
    };
    struct Request
    {
        Vec3 center;
        float radius = 0;
        float damage = 0;
        float impulse = 0;
        std::function<void(bool)> completed;
    };
    struct Owner
    {
        std::uint64_t node = 0;
        std::uint64_t scene = 0;
        std::uint64_t revision = 0;
        std::weak_ptr<const std::uint8_t> lifetime;
        std::deque<Request> requests;
        std::shared_ptr<Prepared> prepared;
        std::future<bool> completion;
    };
    struct Completion
    {
        std::function<void(bool)> callback;
        bool success = false;
        std::uint64_t owner = 0;
    };
    std::map<std::uint64_t, std::vector<GeneratedGroup>> generated;
    std::map<std::uint64_t, Owner> owners;
    std::vector<Completion> ready;
    std::map<std::uint64_t, std::size_t> pending_by_owner;
    std::size_t pending = 0;
    std::uint64_t published_after = 0;
    std::uint64_t submitted_after = 0;
    std::shared_ptr<const std::uint8_t> lifetime = std::make_shared<const std::uint8_t>(0);

    void cancel(Owner& owner)
    {
        for (auto& request : owner.requests)
        {
            ready.push_back({std::move(request.completed), false, owner.node});
        }
    }

    static bool prepare(Prepared& prepared, const Vec3& center, float radius, float damage,
                        float impulse, const schema::DestructionDataT& settings)
    {
        ENGINE_PROFILE_ZONE("Destruction.async_prepare");
        for (const auto& source : prepared.sources)
        {
            if (!prepared.graph.damage_sphere(
                    source.group.id, local_point(source.matrix.data(), center, source.scale),
                    static_cast<float>(radius / source.scale), damage, prepared.error))
            {
                return false;
            }
        }
        prepared.groups.resize(prepared.graph.groups().size());
        prepared.velocity_changes.resize(prepared.groups.size());
        for (std::size_t index = 0; index < prepared.groups.size(); ++index)
        {
            const auto& group = prepared.graph.groups()[index];
            const auto source = std::find_if(
                prepared.sources.begin(), prepared.sources.end(),
                [&](const auto& item)
                {
                    return std::includes(item.group.chunks.begin(), item.group.chunks.end(),
                                         group.chunks.begin(), group.chunks.end());
                });
            if (source == prepared.sources.end())
            {
                prepared.error = "Asynchronous destruction group lost its source";
                return false;
            }
            if (source->group.chunks != group.chunks || source->group.anchored != group.anchored)
            {
                auto& geometry = prepared.groups[index];
                if (!build_group_geometry(prepared.graph.data(), group, settings, geometry.mesh,
                                          geometry.collision))
                {
                    prepared.error = "Cannot prepare destruction fragment tangent frames";
                    return false;
                }
            }
            if (group.anchored || impulse == 0)
            {
                continue;
            }
            double volume = 0;
            const auto pivot = world_point(source->matrix.data(),
                                           center_of(prepared.graph.data(), group.chunks, volume));
            const Vec3 offset{pivot.x() - center.x(), pivot.y() - center.y(),
                              pivot.z() - center.z()};
            const double distance = std::hypot(offset.x(), offset.y(), offset.z());
            if (distance >= radius)
            {
                continue;
            }
            const double mass =
                volume * source->scale * source->scale * source->scale * settings.density;
            if (!std::isfinite(mass) || mass < 0.0001 || mass > 1000000)
            {
                prepared.error = "Asynchronous destruction mass is outside the physics range";
                return false;
            }
            const double amount = impulse * (1 - distance / radius) / mass;
            const Vec3 direction =
                distance > 0.0001
                    ? Vec3{offset.x() / distance, offset.y() / distance, offset.z() / distance}
                    : Vec3{0, 1, 0};
            prepared.velocity_changes[index] = {direction.x() * amount, direction.y() * amount,
                                                direction.z() * amount};
        }
        return true;
    }
};

DestructionSystem::DestructionSystem(WorldSystem& world, PhysicsSystem& physics, TaskSystem* tasks,
                                     std::size_t max_pending_requests,
                                     std::size_t max_commits_per_tick) noexcept
    : world_(world), physics_(physics), tasks_(tasks), max_pending_requests_(max_pending_requests),
      max_commits_per_tick_(max_commits_per_tick)
{
}
DestructionSystem::~DestructionSystem() noexcept
{
    shutdown();
}
DestructionSystem* DestructionSystem::instance() noexcept
{
    return active_system;
}

bool DestructionSystem::on_initialize(const EngineConfig&)
{
    if (active_system || (tasks_ != nullptr && !tasks_->is_initialized()) ||
        max_pending_requests_ == 0 || max_commits_per_tick_ == 0)
    {
        return false;
    }
    auto& registry = world_.component_registry();
    if (!registry.contains(DestructionComponent::component_type) &&
        !registry.add<DestructionComponent>())
    {
        return false;
    }
    state_ = std::make_unique<State>();
    flecs::world ecs(world_.native_world());
    state_->query_world = &world_;
    state_->query_epoch = world_.epoch();
    state_->owners_query =
        ecs.query_builder<const ecs_detail::ComponentStorage<DestructionComponent::Data>>(
               ecs.entity())
            .with<ecs_detail::Published>()
            .build()
            .c_ptr();
    active_system = this;
    return true;
}

bool DestructionSystem::publish(DestructionComponent component, DestructionGraph candidate,
                                const std::vector<schema::DestructionGroupStateT>& groups,
                                bool reuse_groups, const std::vector<PreparedGroup>* prepared)
{
    if (!component || component.node().world() != &world_ || !world_.can_modify())
    {
        return false;
    }
    ENGINE_PROFILE_ZONE("Destruction.publish_fragments");
    auto* scene = component.node().scene();
    if (!state_ || world_.find_node(component.node().id()) != component.node() ||
        component.node().parent() || groups.size() != candidate.groups().size() ||
        (prepared != nullptr && prepared->size() != groups.size()) ||
        engine::ecs_detail::component_runtime(component)->revision_ ==
            std::numeric_limits<std::uint64_t>::max())
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Destruction requires a root node in the active world";
        return false;
    }
    const auto& material_resource = engine::ecs_detail::component_runtime(component)->material_;
    if (!material_resource)
    {
        return false;
    }
    ecs_detail::ResourceBindingScope binding(world_, material_resource.handle());
    std::string material = material_resource->info().path;
    if (!component.settings().material_guid.empty() &&
        !ecs_detail::resource_path(component.node(), component.settings().material_guid, material))
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Cannot resolve destruction material";
        return false;
    }
    const auto& asset = candidate.data();
    const bool enabled = active_component(component);
    auto& previous = state_->generated[component.node().id()];
    std::map<std::uint32_t, const GeneratedGroup*> previous_by_chunk;
    for (const auto& group : previous)
    {
        previous_by_chunk.emplace(group.chunks.front(), &group);
    }
    std::vector<GeneratedGroup> created;
    std::vector<GeneratedGroup> published;
    std::vector<std::pair<RigidBody, schema::RigidBodyDataT>> velocity_updates;
    bool changed =
        !reuse_groups || groups.size() != previous.size() ||
        candidate.healths() != engine::ecs_detail::component_runtime(component)->graph_.healths();
    const auto fail = [&](std::string error)
    {
        if (!release_groups(world_, physics_, created))
        {
            error += "; failed to clean candidate bodies";
        }
        engine::ecs_detail::component_runtime(component)->last_error_ = std::move(error);
        return false;
    };
    for (std::size_t index = 0; index < groups.size(); ++index)
    {
        const auto& description = candidate.groups()[index];
        const auto& saved = groups[index];
        if (saved.chunks != description.chunks || !saved.transform ||
            !valid_transform(*saved.transform) || !finite(saved.linear_velocity) ||
            !finite(saved.angular_velocity) || !std::isfinite(saved.dynamic_age_seconds) ||
            saved.dynamic_age_seconds < 0 ||
            (description.anchored && saved.dynamic_age_seconds != 0))
        {
            return fail("Invalid destruction group pose or identity");
        }
        // Blast actor IDs may change during a clone/split; the sorted source chunks are stable.
        const auto found = previous_by_chunk.find(description.chunks.front());
        const auto* old =
            reuse_groups && found != previous_by_chunk.end() ? found->second : nullptr;
        if (old && old->chunks == description.chunks && old->anchored == description.anchored)
        {
            auto node = world_.find_node(old->node);
            auto transform = node ? node.get_component<Transform>() : Transform{};
            auto renderer = node ? node.get_component<MeshRenderer>() : MeshRenderer{};
            auto collider = node ? node.get_component<Collider>() : Collider{};
            auto body = node ? node.get_component<RigidBody>() : RigidBody{};
            if (!node || !node.transient() || !transform || !renderer || !collider || !body)
            {
                return fail("A generated destruction fragment cannot be reused");
            }
            if (transform.settings() != *saved.transform)
            {
                return fail("Preserved destruction group pose changed during preparation");
            }
            // Preserve damping, gravity and pending impulses on an unchanged physical group.
            auto settings = body.settings();
            settings.linear_velocity = saved.linear_velocity;
            settings.angular_velocity = saved.angular_velocity;
            if (!RigidBody::valid_settings(settings) ||
                settings.motion != (description.anchored ? schema::BodyMotion::static_body
                                                         : schema::BodyMotion::dynamic) ||
                collider.settings().shape != schema::ColliderShape::compound ||
                renderer.enabled() != enabled || collider.enabled() != enabled || !body.enabled())
            {
                return fail("Preserved destruction group has invalid body settings");
            }
            if (settings != body.settings())
            {
                velocity_updates.emplace_back(body, std::move(settings));
                changed = true;
            }
            published.push_back(*old);
            previous_by_chunk.erase(found);
            continue;
        }
        changed = true;
        PreparedGroup synchronous;
        if (prepared == nullptr)
        {
            if (!build_group_geometry(asset, description, component.settings(), synchronous.mesh,
                                      synchronous.collision))
            {
                return fail("Cannot prepare destruction fragment tangent frames");
            }
        }
        const auto& geometry = prepared != nullptr ? (*prepared)[index] : synchronous;
        double volume = 0;
        (void)center_of(asset, description.chunks, volume);
        const double scale = saved.transform->scale.x();
        const double mass = volume * scale * scale * scale * component.settings().density;
        if (!std::isfinite(mass) || mass < 0.0001 || mass > 1000000)
        {
            return fail("Destruction group mass is outside the physics range");
        }
        auto node = scene->create_node("destruction-fragment");
        if (!node)
        {
            return fail("Cannot create destruction fragment node");
        }
        created.push_back(
            {description.chunks, node.id(), description.anchored, saved.dynamic_age_seconds});
        auto renderer = node.add_component<MeshRenderer>();
        auto collider = node.add_component<Collider>();
        auto body = node.add_component<RigidBody>();
        schema::RigidBodyDataT settings;
        settings.motion =
            description.anchored ? schema::BodyMotion::static_body : schema::BodyMotion::dynamic;
        settings.mass = static_cast<float>(mass);
        settings.linear_velocity = saved.linear_velocity;
        settings.angular_velocity = saved.angular_velocity;
        if (description.anchored)
        {
            settings.linear_velocity = {};
            settings.angular_velocity = {};
        }
        if (!node.set_transient(true) || !renderer || !collider || !body ||
            !node.get_component<Transform>().set_settings(*saved.transform) ||
            !renderer.material().set_type(material) || !renderer.set_mesh_data(geometry.mesh) ||
            !body.set_settings(settings))
        {
            return fail("Cannot prepare destruction fragment render or body settings");
        }
        if (!renderer.set_enabled(enabled) ||
            !renderer.set_visible(component.visible_in_hierarchy()) ||
            !collider.set_enabled(enabled))
        {
            return fail("Cannot set destruction fragment state");
        }
        if (!physics_.set_collider_data(collider, geometry.collision))
        {
            return fail("Cannot prepare destruction convex compound collision");
        }
        auto& record = created.back();
        record.renderer_instance = renderer.instance_id();
        record.collider_instance = collider.instance_id();
        published.push_back(created.back());
    }
    if (!changed)
    {
        engine::ecs_detail::component_runtime(component)->last_error_.clear();
        return true;
    }
    std::vector<GeneratedGroup> retired;
    for (const auto& [chunk, group] : previous_by_chunk)
    {
        (void)chunk;
        retired.push_back(*group);
    }
    // No physics step or query occurs between preparation and this publication point.
    if (!release_groups(world_, physics_, retired))
    {
        return fail("Cannot release previous destruction groups");
    }
    // All setters have been validated above; the next physics synchronization consumes these
    // velocities. No collider reset is needed for groups whose shape/motion did not change.
    for (auto& [body, settings] : velocity_updates)
    {
        if (!body.set_settings(settings))
        {
            return fail("Cannot publish validated destruction velocity");
        }
    }
    previous = std::move(published);
    engine::ecs_detail::component_runtime(component)->graph_ = std::move(candidate);
    engine::ecs_detail::component_runtime(component)->ready_ = true;
    engine::ecs_detail::component_runtime(component)->pending_ = false;
    engine::ecs_detail::component_runtime(component)->last_error_.clear();
    ++engine::ecs_detail::component_runtime(component)->revision_;
    return true;
}

bool DestructionSystem::prepare_groups(DestructionComponent component,
                                       const DestructionGraph& candidate, bool preserve,
                                       std::vector<schema::DestructionGroupStateT>& states)
{
    states.clear();
    if (!component || component.node().world() != &world_ || !world_.can_modify())
    {
        return false;
    }
    ENGINE_PROFILE_ZONE("Destruction.prepare_groups");
    auto transform = component.node().get_component<Transform>();
    if (!transform || !valid_transform(transform.settings()))
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Destruction requires a positive uniform scale";
        return false;
    }
    schema::DestructionStateDataT previous;
    if (preserve && !snapshot(component, previous))
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Cannot capture destruction groups before replacement";
        return false;
    }
    for (const auto& group : candidate.groups())
    {
        schema::DestructionGroupStateT value;
        value.chunks = group.chunks;
        value.transform = std::make_unique<TransformSettings>(transform.settings());
        if (preserve)
        {
            const auto found =
                std::find_if(previous.groups.begin(), previous.groups.end(),
                             [&](const auto& old)
                             {
                                 return std::includes(old->chunks.begin(), old->chunks.end(),
                                                      group.chunks.begin(), group.chunks.end());
                             });
            if (found == previous.groups.end())
            {
                engine::ecs_detail::component_runtime(component)->last_error_ =
                    "Replacement destruction group has no source pose";
                return false;
            }
            value = **found;
            value.chunks = group.chunks;
            // Linear velocity belongs to the center of mass. Preserve rigid rotation on split.
            double unused = 0;
            const auto old_center = center_of(candidate.data(), (*found)->chunks, unused);
            const auto new_center = center_of(candidate.data(), group.chunks, unused);
            const auto record = state_->generated.find(component.node().id());
            const auto generated =
                std::find_if(record->second.begin(), record->second.end(),
                             [&](const auto& item) { return item.chunks == (*found)->chunks; });
            auto source_node = generated != record->second.end()
                                   ? component.node().scene()->find_node(generated->node)
                                   : Node{};
            float matrix[16];
            if (!source_node || !source_node.get_component<Transform>().world_matrix(matrix))
            {
                engine::ecs_detail::component_runtime(component)->last_error_ =
                    "Cannot resolve source destruction transform";
                return false;
            }
            const auto a = world_point(matrix, old_center);
            const auto b = world_point(matrix, new_center);
            const Vec3 offset{b.x() - a.x(), b.y() - a.y(), b.z() - a.z()};
            const auto& omega = value.angular_velocity;
            const auto& velocity = value.linear_velocity;
            value.linear_velocity = {velocity.x() + omega.y() * offset.z() - omega.z() * offset.y(),
                                     velocity.y() + omega.z() * offset.x() - omega.x() * offset.z(),
                                     velocity.z() + omega.x() * offset.y() -
                                         omega.y() * offset.x()};
        }
        states.push_back(std::move(value));
    }
    return true;
}

bool DestructionSystem::rebuild(DestructionComponent component, DestructionGraph candidate,
                                bool preserve)
{
    if (!component || component.node().world() != &world_ || !world_.can_modify())
    {
        return false;
    }
    std::vector<schema::DestructionGroupStateT> states;
    return prepare_groups(component, candidate, preserve, states) &&
           publish(component, std::move(candidate), states, false);
}

bool DestructionSystem::damage(DestructionComponent component, const Vec3& center, float radius,
                               float damage_value, float impulse)
{
    if (!component || component.node().world() != &world_ || !world_.can_modify())
    {
        return false;
    }
    ENGINE_PROFILE_ZONE("Destruction.damage");
    if (!state_ || !engine::ecs_detail::component_runtime(component)->ready_ ||
        !active_component(component) || !finite(center) || !std::isfinite(radius) || radius <= 0 ||
        radius > 100000 || !std::isfinite(damage_value) || damage_value < 0 ||
        damage_value > 1000000 || !std::isfinite(impulse) || impulse < 0 || impulse > 1000000)
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Invalid destruction sphere, damage or impulse";
        return false;
    }
    const auto found = state_->generated.find(component.node().id());
    DestructionGraph candidate;
    if (found == state_->generated.end() ||
        !engine::ecs_detail::component_runtime(component)->graph_.clone(
            candidate, engine::ecs_detail::component_runtime(component)->last_error_))
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Cannot prepare destruction damage candidate";
        return false;
    }
    for (const auto& group : engine::ecs_detail::component_runtime(component)->graph_.groups())
    {
        const auto generated =
            std::find_if(found->second.begin(), found->second.end(),
                         [&](const auto& item) { return item.chunks == group.chunks; });
        auto node = generated != found->second.end()
                        ? component.node().scene()->find_node(generated->node)
                        : Node{};
        auto transform = node ? node.get_component<Transform>() : Transform{};
        float matrix[16];
        if (!transform || !valid_transform(transform.settings()) || !transform.world_matrix(matrix))
        {
            engine::ecs_detail::component_runtime(component)->last_error_ =
                "Cannot locate destruction group transform";
            return false;
        }
        const double scale = transform.settings().scale.x();
        if (!candidate.damage_sphere(group.id, local_point(matrix, center, scale),
                                     static_cast<float>(radius / scale), damage_value,
                                     engine::ecs_detail::component_runtime(component)->last_error_))
        {
            return false;
        }
    }
    std::vector<schema::DestructionGroupStateT> states;
    if (!prepare_groups(component, candidate, true, states))
    {
        return false;
    }
    // Prepare impulses as velocity changes before publication; snapshots include them immediately.
    for (std::size_t index = 0; index < candidate.groups().size(); ++index)
    {
        const auto& group = candidate.groups()[index];
        if (group.anchored || impulse == 0)
        {
            continue;
        }
        const auto source =
            std::find_if(found->second.begin(), found->second.end(),
                         [&](const auto& old)
                         {
                             return std::includes(old.chunks.begin(), old.chunks.end(),
                                                  group.chunks.begin(), group.chunks.end());
                         });
        auto node = source == found->second.end()
                        ? Node{}
                        : component.node().scene()->find_node(source->node);
        float matrix[16];
        double volume = 0;
        if (!node || !node.get_component<Transform>().world_matrix(matrix))
        {
            engine::ecs_detail::component_runtime(component)->last_error_ =
                "Cannot prepare destruction impulse";
            return false;
        }
        const auto pivot = world_point(matrix, center_of(candidate.data(), group.chunks, volume));
        const double x = pivot.x() - center.x();
        const double y = pivot.y() - center.y();
        const double z = pivot.z() - center.z();
        const double distance = std::sqrt(x * x + y * y + z * z);
        if (distance >= radius)
        {
            continue;
        }
        auto& state = states[index];
        const double scale = state.transform->scale.x();
        const double mass = volume * scale * scale * scale * component.settings().density;
        const double amount = impulse * (1 - distance / radius) / mass;
        const Vec3 direction =
            distance > 0.0001 ? Vec3{x / distance, y / distance, z / distance} : Vec3{0, 1, 0};
        state.linear_velocity = {state.linear_velocity.x() + direction.x() * amount,
                                 state.linear_velocity.y() + direction.y() * amount,
                                 state.linear_velocity.z() + direction.z() * amount};
    }
    return publish(component, std::move(candidate), states, true);
}

bool DestructionSystem::reset(DestructionComponent component)
{
    if (!component || component.node().world() != &world_ || !world_.can_modify())
    {
        return false;
    }
    DestructionGraph candidate;
    return engine::ecs_detail::component_runtime(component)->resource_ &&
           candidate.initialize(engine::ecs_detail::component_runtime(component)->graph_.data(),
                                engine::ecs_detail::component_runtime(component)->last_error_) &&
           rebuild(component, std::move(candidate), false);
}

bool DestructionSystem::apply_stress(DestructionComponent component, const Vec3& gravity,
                                     float elastic_limit, float fatal_limit,
                                     std::uint32_t iterations, std::uint32_t& broken_bonds)
{
    broken_bonds = 0;
    if (!component || component.node().world() != &world_ || !world_.can_modify())
    {
        return false;
    }
    ENGINE_PROFILE_ZONE("Destruction.apply_stress");
    if (!state_ || !engine::ecs_detail::component_runtime(component)->ready_ ||
        !active_component(component) || !finite(gravity) || std::abs(gravity.x()) > 100000 ||
        std::abs(gravity.y()) > 100000 || std::abs(gravity.z()) > 100000 ||
        !std::isfinite(elastic_limit) || elastic_limit <= 0 || !std::isfinite(fatal_limit) ||
        fatal_limit < elastic_limit || iterations == 0 || iterations > 1000)
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Invalid destruction gravity stress parameters";
        return false;
    }
    const auto found = state_->generated.find(component.node().id());
    DestructionGraph candidate;
    if (found == state_->generated.end() ||
        !engine::ecs_detail::component_runtime(component)->graph_.clone(
            candidate, engine::ecs_detail::component_runtime(component)->last_error_))
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Cannot prepare destruction stress candidate";
        return false;
    }
    std::uint32_t count = 0;
    for (const auto& group : engine::ecs_detail::component_runtime(component)->graph_.groups())
    {
        if (!group.anchored)
        {
            continue;
        }
        const auto generated =
            std::find_if(found->second.begin(), found->second.end(),
                         [&](const auto& item) { return item.chunks == group.chunks; });
        auto node = generated != found->second.end()
                        ? component.node().scene()->find_node(generated->node)
                        : Node{};
        auto transform = node ? node.get_component<Transform>() : Transform{};
        float matrix[16];
        if (!transform || !valid_transform(transform.settings()) || !transform.world_matrix(matrix))
        {
            engine::ecs_detail::component_runtime(component)->last_error_ =
                "Cannot locate destruction stress group transform";
            return false;
        }
        // For uniform scale s: weight/area grows by s. Transpose(s*R) rotates gravity
        // back into asset coordinates and applies this pressure scaling in one operation.
        const Vec3 local_gravity{
            matrix[0] * gravity.x() + matrix[1] * gravity.y() + matrix[2] * gravity.z(),
            matrix[4] * gravity.x() + matrix[5] * gravity.y() + matrix[6] * gravity.z(),
            matrix[8] * gravity.x() + matrix[9] * gravity.y() + matrix[10] * gravity.z()};
        std::uint32_t group_broken = 0;
        if (!candidate.apply_stress(group.id, local_gravity, component.settings().density,
                                    elastic_limit, fatal_limit, iterations, group_broken,
                                    engine::ecs_detail::component_runtime(component)->last_error_))
        {
            return false;
        }
        count += group_broken;
    }
    if (candidate.healths() == engine::ecs_detail::component_runtime(component)->graph_.healths())
    {
        engine::ecs_detail::component_runtime(component)->last_error_.clear();
        return true;
    }
    std::vector<schema::DestructionGroupStateT> states;
    if (!prepare_groups(component, candidate, true, states) ||
        !publish(component, std::move(candidate), states, true))
    {
        return false;
    }
    broken_bonds = count;
    return true;
}

bool DestructionSystem::request_damage(DestructionComponent component, const Vec3& center,
                                       float radius, float damage_value, float impulse,
                                       std::function<void(bool)> completed)
{
    if (!component || component.node().world() != &world_ || !world_.can_modify())
    {
        return false;
    }
    if (!state_ || !is_initialized() || tasks_ == nullptr || !tasks_->is_initialized() ||
        !completed || !engine::ecs_detail::component_runtime(component)->ready_ ||
        !active_component(component) ||
        world_.find_node(component.node().id()) != component.node() || !finite(center) ||
        !std::isfinite(radius) || radius <= 0 || radius > 100000 || !std::isfinite(damage_value) ||
        damage_value < 0 || damage_value > 1000000 || !std::isfinite(impulse) || impulse < 0 ||
        impulse > 1000000)
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Invalid asynchronous destruction request or task domain";
        return false;
    }
    const auto id = component.node().id();
    auto found = state_->owners.find(id);
    if (found != state_->owners.end() &&
        (found->second.scene != component.node().scene()->id() ||
         found->second.lifetime.lock() !=
             engine::ecs_detail::component_runtime(component)->lifetime_ ||
         found->second.revision != engine::ecs_detail::component_runtime(component)->revision_))
    {
        state_->cancel(found->second);
        state_->owners.erase(found);
    }
    if (state_->pending >= max_pending_requests_)
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Destruction asynchronous request budget is full";
        return false;
    }
    auto& owner = state_->owners[id];
    owner.node = id;
    owner.scene = component.node().scene()->id();
    owner.revision = engine::ecs_detail::component_runtime(component)->revision_;
    owner.lifetime = engine::ecs_detail::component_runtime(component)->lifetime_;
    owner.requests.push_back({center, radius, damage_value, impulse, std::move(completed)});
    ++state_->pending;
    ++state_->pending_by_owner[id];
    engine::ecs_detail::component_runtime(component)->last_error_.clear();
    return true;
}

void DestructionSystem::process_requests()
{
    ENGINE_PROFILE_ZONE("Destruction.async_publish");
    if (world_.paused())
    {
        return;
    }
    std::size_t commits = 0;
    auto item = state_->owners.upper_bound(state_->published_after);
    for (std::size_t remaining = state_->owners.size(); remaining != 0; --remaining)
    {
        if (item == state_->owners.end())
        {
            item = state_->owners.begin();
        }
        const auto current = item++;
        auto& owner = current->second;
        auto node = world_.find_node(current->first);
        if (node && node.scene()->id() != owner.scene)
        {
            node = {};
        }
        auto component = node ? node.get_component<DestructionComponent>() : DestructionComponent{};
        if (!component || !engine::ecs_detail::component_runtime(component)->ready_ ||
            !active_component(component) ||
            owner.lifetime.lock() != engine::ecs_detail::component_runtime(component)->lifetime_ ||
            owner.revision != engine::ecs_detail::component_runtime(component)->revision_)
        {
            state_->cancel(owner);
            state_->owners.erase(current);
            continue;
        }
        if (owner.completion.valid() && commits < max_commits_per_tick_ &&
            owner.completion.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            ++commits;
            state_->published_after = current->first;
            bool success = owner.completion.get();
            auto& prepared = *owner.prepared;
            std::vector<schema::DestructionGroupStateT> groups;
            if (success)
            {
                const auto generated = state_->generated.find(node.id());
                success = generated != state_->generated.end();
                for (const auto& source : prepared.sources)
                {
                    if (!success)
                    {
                        break;
                    }
                    const auto found = std::find_if(
                        generated->second.begin(), generated->second.end(),
                        [&](const auto& value) { return value.chunks == source.group.chunks; });
                    auto fragment =
                        found != generated->second.end() ? world_.find_node(found->node) : Node{};
                    auto transform = fragment ? fragment.get_component<Transform>() : Transform{};
                    success = transform && valid_transform(transform.settings()) &&
                              transform.settings().scale.x() == source.scale;
                }
                if (!success)
                {
                    engine::ecs_detail::component_runtime(component)->last_error_ =
                        "Destruction source identity or scale changed during preparation";
                }
            }
            if (success)
            {
                success = prepare_groups(component, prepared.graph, true, groups);
                if (success)
                {
                    for (std::size_t index = 0; index < groups.size(); ++index)
                    {
                        const auto& velocity = groups[index].linear_velocity;
                        const auto& delta = prepared.velocity_changes[index];
                        groups[index].linear_velocity = {velocity.x() + delta.x(),
                                                         velocity.y() + delta.y(),
                                                         velocity.z() + delta.z()};
                    }
                    success = publish(component, std::move(prepared.graph), groups, true,
                                      &prepared.groups);
                }
                owner.revision = engine::ecs_detail::component_runtime(component)->revision_;
            }
            else if (!prepared.error.empty())
            {
                engine::ecs_detail::component_runtime(component)->last_error_ = prepared.error;
            }
            owner.prepared.reset();
            state_->ready.push_back(
                {std::move(owner.requests.front().completed), success, owner.node});
            owner.requests.pop_front();
        }
        if (owner.requests.empty())
        {
            state_->owners.erase(current);
        }
    }
    std::size_t running = static_cast<std::size_t>(
        std::count_if(state_->owners.begin(), state_->owners.end(),
                      [](const auto& item) { return item.second.completion.valid(); }));
    item = state_->owners.upper_bound(state_->submitted_after);
    for (std::size_t remaining = state_->owners.size(); remaining != 0; --remaining)
    {
        if (tasks_ == nullptr || running >= tasks_->max_concurrency())
        {
            break;
        }
        if (item == state_->owners.end())
        {
            item = state_->owners.begin();
        }
        const auto current = item++;
        const auto id = current->first;
        auto& owner = current->second;
        if (owner.completion.valid() || owner.requests.empty())
        {
            continue;
        }
        auto component = world_.find_node(id).get_component<DestructionComponent>();
        if (!owner.prepared)
        {
            auto prepared = std::make_shared<State::Prepared>();
            const auto found = state_->generated.find(id);
            bool valid =
                found != state_->generated.end() &&
                engine::ecs_detail::component_runtime(component)->graph_.clone(
                    prepared->graph, engine::ecs_detail::component_runtime(component)->last_error_);
            for (const auto& group :
                 engine::ecs_detail::component_runtime(component)->graph_.groups())
            {
                if (!valid)
                {
                    break;
                }
                const auto generated =
                    std::find_if(found->second.begin(), found->second.end(),
                                 [&](const auto& value) { return value.chunks == group.chunks; });
                auto fragment =
                    generated != found->second.end() ? world_.find_node(generated->node) : Node{};
                auto transform = fragment ? fragment.get_component<Transform>() : Transform{};
                State::SourceGroup source;
                valid = transform && valid_transform(transform.settings()) &&
                        transform.world_matrix(source.matrix.data());
                if (valid)
                {
                    source.group = group;
                    source.scale = transform.settings().scale.x();
                    prepared->sources.push_back(std::move(source));
                }
            }
            if (!valid)
            {
                engine::ecs_detail::component_runtime(component)->last_error_ =
                    "Cannot capture asynchronous destruction input";
                state_->ready.push_back({std::move(owner.requests.front().completed), false, id});
                owner.requests.pop_front();
                continue;
            }
            owner.prepared = std::move(prepared);
        }
        const auto& request = owner.requests.front();
        owner.completion = tasks_->submit(
            "Destruction.prepare", [prepared = owner.prepared, center = request.center,
                                    radius = request.radius, damage_value = request.damage,
                                    impulse = request.impulse, settings = component.settings()]
            { return State::prepare(*prepared, center, radius, damage_value, impulse, settings); });
        if (!owner.completion.valid())
        {
            break;
        }
        state_->submitted_after = id;
        ++running;
    }
    auto ready = std::move(state_->ready);
    state_->ready.clear();
    state_->pending -= ready.size();
    const std::weak_ptr<const std::uint8_t> lifetime = state_->lifetime;
    for (auto& completion : ready)
    {
        if (lifetime.expired())
        {
            return;
        }
        const auto pending = state_->pending_by_owner.find(completion.owner);
        if (pending != state_->pending_by_owner.end() && --pending->second == 0)
        {
            state_->pending_by_owner.erase(pending);
        }
        completion.callback(completion.success);
    }
}

bool DestructionSystem::owns_fragment(DestructionComponent component,
                                      std::uint64_t node_id) const noexcept
{
    if (!component || component.node().world() != &world_)
    {
        return false;
    }
    if (!state_ || !engine::ecs_detail::component_runtime(component)->ready_ ||
        world_.find_node(component.node().id()) != component.node() ||
        component.node().get_component<DestructionComponent>() != component)
    {
        return false;
    }
    const auto found = state_->generated.find(component.node().id());
    auto node = world_.find_node(node_id);
    return found != state_->generated.end() && node && node.transient() &&
           std::any_of(found->second.begin(), found->second.end(),
                       [node_id](const auto& group) { return group.node == node_id; });
}

bool DestructionSystem::retire_fragments(DestructionComponent component, std::uint32_t max_count,
                                         double max_age_seconds, std::uint32_t& retired_count)
{
    retired_count = 0;
    if (!component || component.node().world() != &world_ || !world_.can_modify())
    {
        return false;
    }
    ENGINE_PROFILE_ZONE("Destruction.retire_fragments");
    const auto id = component.node().id();
    if (!state_ || !is_initialized() || !engine::ecs_detail::component_runtime(component)->ready_ ||
        world_.find_node(id) != component.node() ||
        component.node().get_component<DestructionComponent>() != component ||
        !std::isfinite(max_age_seconds) || max_age_seconds < 0)
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Invalid destruction retirement owner or budget";
        return false;
    }
    if (state_->pending_by_owner.contains(id))
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Destruction retirement requires all owner requests to complete";
        return false;
    }
    const auto found = state_->generated.find(id);
    if (found == state_->generated.end() ||
        found->second.size() !=
            engine::ecs_detail::component_runtime(component)->graph_.groups().size())
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Cannot locate destruction groups for retirement";
        return false;
    }
    std::vector<const GeneratedGroup*> dynamic;
    for (const auto& group : found->second)
    {
        auto node = world_.find_node(group.node);
        auto transform = node ? node.get_component<Transform>() : Transform{};
        auto renderer = node ? node.get_component<MeshRenderer>() : MeshRenderer{};
        auto collider = node ? node.get_component<Collider>() : Collider{};
        auto body = node ? node.get_component<RigidBody>() : RigidBody{};
        if (!node || !node.transient() || node.parent() || !transform || !renderer || !collider ||
            !body || renderer.instance_id() != group.renderer_instance ||
            collider.instance_id() != group.collider_instance ||
            !valid_transform(transform.settings()) ||
            !Collider::valid_settings(collider.settings()) ||
            collider.settings().shape != schema::ColliderShape::compound ||
            !RigidBody::valid_settings(body.settings()) ||
            body.settings().motion !=
                (group.anchored ? schema::BodyMotion::static_body : schema::BodyMotion::dynamic))
        {
            engine::ecs_detail::component_runtime(component)->last_error_ =
                "A generated destruction fragment was removed or replaced";
            return false;
        }
        if (!group.anchored)
        {
            dynamic.push_back(&group);
        }
    }
    std::sort(dynamic.begin(), dynamic.end(),
              [](const auto* a, const auto* b)
              {
                  return a->dynamic_age_seconds != b->dynamic_age_seconds
                             ? a->dynamic_age_seconds > b->dynamic_age_seconds
                             : a->chunks.front() < b->chunks.front();
              });
    const auto expired = max_age_seconds > 0
                             ? static_cast<std::size_t>(std::count_if(
                                   dynamic.begin(), dynamic.end(), [&](const auto* group)
                                   { return group->dynamic_age_seconds >= max_age_seconds; }))
                             : 0;
    const auto excess = dynamic.size() > max_count ? dynamic.size() - max_count : 0;
    const auto count = std::max(expired, excess);
    if (count == 0)
    {
        engine::ecs_detail::component_runtime(component)->last_error_.clear();
        return true;
    }
    std::vector<std::uint32_t> groups;
    for (std::size_t index = 0; index < count; ++index)
    {
        groups.push_back(dynamic[index]->chunks.front());
    }
    DestructionGraph candidate;
    std::vector<schema::DestructionGroupStateT> states;
    if (!engine::ecs_detail::component_runtime(component)->graph_.clone(
            candidate, engine::ecs_detail::component_runtime(component)->last_error_) ||
        !candidate.retire_groups(groups,
                                 engine::ecs_detail::component_runtime(component)->last_error_) ||
        !prepare_groups(component, candidate, true, states) ||
        !publish(component, std::move(candidate), states, true))
    {
        return false;
    }
    retired_count = static_cast<std::uint32_t>(count);
    return true;
}

bool DestructionSystem::refresh_support(DestructionComponent component, Collider support,
                                        float max_gap, float min_coverage)
{
    if (!component || component.node().world() != &world_ || !world_.can_modify())
    {
        return false;
    }
    ENGINE_PROFILE_ZONE("Destruction.refresh_support");
    if (!support || !state_ || !engine::ecs_detail::component_runtime(component)->ready_ ||
        !active_component(component) ||
        world_.find_node(component.node().id()) != component.node() ||
        world_.find_node(support.node().id()) != support.node() ||
        component.node().get_component<DestructionComponent>() != component ||
        support.node().get_component<Collider>() != support || !std::isfinite(max_gap) ||
        max_gap <= 0 || max_gap > 1000 || !std::isfinite(min_coverage) || min_coverage <= 0 ||
        min_coverage > 1)
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Invalid destruction foundation query, component or scene";
        return false;
    }
    auto support_body = support.node().get_component<RigidBody>();
    if (support.settings().is_trigger || support.node() == component.node() ||
        owns_fragment(component, support.node().id()) ||
        (support_body && support_body.settings().motion != schema::BodyMotion::static_body))
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Destruction support must be an external static solid collider";
        return false;
    }
    const auto found = state_->generated.find(component.node().id());
    if (found == state_->generated.end())
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Cannot locate generated destruction groups";
        return false;
    }
    const auto& asset = engine::ecs_detail::component_runtime(component)->graph_.data();
    const auto health = engine::ecs_detail::component_runtime(component)->graph_.healths();
    std::size_t world_bond = asset.bonds.size();
    std::vector<std::uint32_t> released;
    for (std::uint32_t chunk_id = 0; chunk_id < asset.chunks.size(); ++chunk_id)
    {
        const auto& chunk = *asset.chunks[chunk_id];
        if (!chunk.anchored)
        {
            continue;
        }
        if (health[world_bond++] <= 0)
        {
            continue;
        }
        const auto group = std::find_if(
            found->second.begin(), found->second.end(), [chunk_id](const auto& value)
            { return std::binary_search(value.chunks.begin(), value.chunks.end(), chunk_id); });
        auto node = group != found->second.end() ? world_.find_node(group->node) : Node{};
        auto transform = node ? node.get_component<Transform>() : Transform{};
        float matrix[16];
        if (!transform || !valid_transform(transform.settings()) || !transform.world_matrix(matrix))
        {
            engine::ecs_detail::component_runtime(component)->last_error_ =
                "Cannot locate the current foundation group transform";
            return false;
        }
        double coverage = 0;
        if (!support_coverage(chunk, matrix, support, physics_, max_gap, coverage,
                              engine::ecs_detail::component_runtime(component)->last_error_))
        {
            return false;
        }
        if (coverage + 1.0e-9 < min_coverage)
        {
            released.push_back(chunk_id);
        }
    }
    if (released.empty())
    {
        engine::ecs_detail::component_runtime(component)->last_error_.clear();
        return true;
    }
    DestructionGraph candidate;
    std::vector<schema::DestructionGroupStateT> states;
    return engine::ecs_detail::component_runtime(component)->graph_.clone(
               candidate, engine::ecs_detail::component_runtime(component)->last_error_) &&
           candidate.release_anchors(
               released, engine::ecs_detail::component_runtime(component)->last_error_) &&
           prepare_groups(component, candidate, true, states) &&
           publish(component, std::move(candidate), states, true);
}

bool DestructionSystem::snapshot(DestructionComponent component,
                                 schema::DestructionStateDataT& output) const
{
    output = {};
    if (!component || component.node().world() != &world_)
    {
        return false;
    }
    if (!state_ || !engine::ecs_detail::component_runtime(component)->ready_ ||
        !engine::ecs_detail::component_runtime(component)->resource_ ||
        world_.find_node(component.node().id()) != component.node())
    {
        return false;
    }
    const auto found = state_->generated.find(component.node().id());
    if (found == state_->generated.end())
    {
        return false;
    }
    schema::DestructionStateDataT state;
    state.base_guid = engine::ecs_detail::component_runtime(component)->resource_->info().guid;
    state.base_hash =
        engine::ecs_detail::component_runtime(component)->resource_->info().content_hash;
    state.bond_health = engine::ecs_detail::component_runtime(component)->graph_.healths();
    for (const auto& group : found->second)
    {
        auto saved = std::make_unique<schema::DestructionGroupStateT>();
        if (!read_group(*component.node().scene(), group, *saved))
        {
            return false;
        }
        saved->dynamic_age_seconds = group.dynamic_age_seconds;
        state.groups.push_back(std::move(saved));
    }
    std::vector<bool> active(
        engine::ecs_detail::component_runtime(component)->graph_.data().chunks.size(), false);
    for (const auto& group : engine::ecs_detail::component_runtime(component)->graph_.groups())
    {
        for (const auto chunk : group.chunks)
        {
            active[chunk] = true;
        }
    }
    for (std::size_t chunk = 0; chunk < active.size(); ++chunk)
    {
        if (!active[chunk])
        {
            state.retired_chunks.push_back(static_cast<std::uint32_t>(chunk));
        }
    }
    output = std::move(state);
    return true;
}

bool DestructionSystem::restore(DestructionComponent component,
                                const schema::DestructionStateDataT& state)
{
    if (!component || component.node().world() != &world_ || !world_.can_modify())
    {
        return false;
    }
    if (!engine::ecs_detail::component_runtime(component)->resource_ || state.schema_version != 2 ||
        state.base_guid !=
            engine::ecs_detail::component_runtime(component)->resource_->info().guid ||
        state.base_hash !=
            engine::ecs_detail::component_runtime(component)->resource_->info().content_hash)
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Destruction state does not match its base resource";
        return false;
    }
    DestructionGraph candidate;
    if (!candidate.initialize(engine::ecs_detail::component_runtime(component)->graph_.data(),
                              engine::ecs_detail::component_runtime(component)->last_error_) ||
        !candidate.restore_health(state.bond_health,
                                  engine::ecs_detail::component_runtime(component)->last_error_))
    {
        return false;
    }
    if (!std::is_sorted(state.retired_chunks.begin(), state.retired_chunks.end()) ||
        std::adjacent_find(state.retired_chunks.begin(), state.retired_chunks.end()) !=
            state.retired_chunks.end() ||
        (!state.retired_chunks.empty() &&
         state.retired_chunks.back() >= candidate.data().chunks.size()))
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Invalid retired destruction chunk identities";
        return false;
    }
    std::vector<std::uint32_t> retired_groups;
    for (const auto& group : candidate.groups())
    {
        const auto count =
            std::count_if(group.chunks.begin(), group.chunks.end(),
                          [&](std::uint32_t chunk)
                          {
                              return std::binary_search(state.retired_chunks.begin(),
                                                        state.retired_chunks.end(), chunk);
                          });
        if (count == 0)
        {
            continue;
        }
        if (group.anchored || static_cast<std::size_t>(count) != group.chunks.size())
        {
            engine::ecs_detail::component_runtime(component)->last_error_ =
                "Retired chunks do not match complete detached groups";
            return false;
        }
        retired_groups.push_back(group.id);
    }
    if (!candidate.retire_groups(retired_groups,
                                 engine::ecs_detail::component_runtime(component)->last_error_))
    {
        return false;
    }
    if (state.groups.size() != candidate.groups().size())
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Destruction state group count does not match its bond state";
        return false;
    }
    std::vector<schema::DestructionGroupStateT> groups;
    for (const auto& group : candidate.groups())
    {
        const auto found =
            std::find_if(state.groups.begin(), state.groups.end(),
                         [&](const auto& saved) { return saved && saved->chunks == group.chunks; });
        if (found == state.groups.end())
        {
            engine::ecs_detail::component_runtime(component)->last_error_ =
                "Destruction state contains a mismatched group";
            return false;
        }
        groups.push_back(**found);
    }
    return publish(component, std::move(candidate), groups, false);
}

bool DestructionSystem::on_tick(double delta_time)
{
    if (world_.paused())
    {
        return synchronize();
    }
    if (!state_ || !std::isfinite(delta_time) || delta_time < 0)
    {
        return false;
    }
    for (auto& [id, groups] : state_->generated)
    {
        auto node = world_.find_node(id);
        auto component = node ? node.get_component<DestructionComponent>() : DestructionComponent{};
        if (!component || !active_component(component))
        {
            continue;
        }
        for (auto& group : groups)
        {
            if (!group.anchored)
            {
                group.dynamic_age_seconds =
                    std::min(group.dynamic_age_seconds,
                             std::numeric_limits<double>::max() - delta_time) +
                    delta_time;
            }
        }
    }
    return synchronize();
}

bool DestructionSystem::synchronize()
{
    ENGINE_PROFILE_ZONE("Destruction.synchronize");
    if (!state_ || !is_initialized() || !world_.is_initialized() ||
        state_->query_epoch != world_.epoch())
    {
        return false;
    }
    for (auto found = state_->generated.begin(); found != state_->generated.end();)
    {
        auto node = world_.find_node(found->first);
        auto component = node ? node.get_component<DestructionComponent>() : DestructionComponent{};
        if (!component || !engine::ecs_detail::component_runtime(component)->resource_)
        {
            if (!release_groups(world_, physics_, found->second))
            {
                return false;
            }
            found = state_->generated.erase(found);
        }
        else
        {
            ++found;
        }
    }
    std::vector<std::uint64_t> owners;
    flecs::query<const ecs_detail::ComponentStorage<DestructionComponent::Data>>(
        state_->owners_query)
        .each([&](flecs::entity entity,
                  const ecs_detail::ComponentStorage<DestructionComponent::Data>&)
              { owners.push_back(entity.id()); });
    for (auto id : owners)
    {
        auto node = world_.find_node(id);
        if (!node || !synchronize(node))
        {
            return false;
        }
    }
    if (!world_.paused())
    {
        process_requests();
    }
    return true;
}

bool DestructionSystem::synchronize(Node owner)
{
    if (!owner || !state_ || !is_initialized() || world_.find_node(owner.id()) != owner)
    {
        return false;
    }
    auto node = owner;
    auto component = node.get_component<DestructionComponent>();
    if (!component || !engine::ecs_detail::component_runtime(component)->resource_)
    {
        const auto found = state_->generated.find(node.id());
        if (found != state_->generated.end())
        {
            if (!release_groups(world_, physics_, found->second))
            {
                return false;
            }
            state_->generated.erase(found);
        }
        return true;
    }
    if (const auto generated = state_->generated.find(node.id());
        generated != state_->generated.end() &&
        std::any_of(generated->second.begin(), generated->second.end(),
                    [&](const auto& group) { return !world_.find_node(group.node).valid(); }))
    {
        if (!release_groups(world_, physics_, generated->second))
        {
            return false;
        }
        state_->generated.erase(generated);
        engine::ecs_detail::component_runtime(component)->ready_ = false;
        engine::ecs_detail::component_runtime(component)->pending_ = false;
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Generated destruction nodes were unloaded; Reset recreates the assembly";
        return true;
    }
    if (!world_.paused() && engine::ecs_detail::component_runtime(component)->pending_ &&
        component.active())
    {
        DestructionGraph candidate;
        if (!engine::ecs_detail::component_runtime(component)->graph_.clone(
                candidate, engine::ecs_detail::component_runtime(component)->last_error_) ||
            !rebuild(component, std::move(candidate), false))
        {
            return false;
        }
    }
    const auto found = state_->generated.find(node.id());
    if (found == state_->generated.end())
    {
        return true;
    }
    auto transform = node.get_component<Transform>();
    if (!transform || node.parent() || !valid_transform(transform.settings()))
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Destruction requires a root node with positive uniform scale";
        return false;
    }
    const bool enabled = active_component(component);
    for (const auto& group : found->second)
    {
        auto fragment = world_.find_node(group.node);
        if (!fragment)
        {
            engine::ecs_detail::component_runtime(component)->last_error_ =
                "A generated destruction fragment was removed externally";
            return false;
        }
        auto renderer = fragment.get_component<MeshRenderer>();
        auto collider = fragment.get_component<Collider>();
        auto fragment_transform = fragment.get_component<Transform>();
        auto body = fragment.get_component<RigidBody>();
        if (!renderer || !collider || !fragment_transform || !body)
        {
            engine::ecs_detail::component_runtime(component)->last_error_ =
                "A generated destruction component was removed externally";
            return false;
        }
        const bool previous_enabled = collider.enabled();
        const auto previous_transform = fragment_transform.settings();
        const auto previous_body = body.settings();
        const bool moved = group.anchored && previous_transform != transform.settings();
        if (previous_enabled != enabled || moved)
        {
            if (!collider.set_enabled(enabled))
            {
                return false;
            }
            auto body_settings = previous_body;
            double volume = 0;
            (void)center_of(engine::ecs_detail::component_runtime(component)->graph_.data(),
                            group.chunks, volume);
            const double scale =
                group.anchored ? transform.settings().scale.x() : previous_transform.scale.x();
            body_settings.mass =
                static_cast<float>(volume * scale * scale * scale * component.settings().density);
            if ((moved && !fragment_transform.set_settings(transform.settings())) ||
                !body.set_settings(body_settings) ||
                !physics_.set_collider_data(collider, collider.settings()))
            {
                const bool restored = collider.set_enabled(previous_enabled) &&
                                      fragment_transform.set_settings(previous_transform) &&
                                      body.set_settings(previous_body);
                engine::ecs_detail::component_runtime(component)->last_error_ =
                    restored ? "Cannot synchronize destruction collision"
                             : "Cannot restore destruction fragment settings";
                return false;
            }
        }
        if (!renderer.set_enabled(enabled) ||
            !renderer.set_visible(component.visible_in_hierarchy()))
        {
            return false;
        }
    }
    return true;
}

void DestructionSystem::on_shutdown() noexcept
{
    if (active_system == this)
    {
        active_system = nullptr;
    }
    if (state_)
    {
        for (const auto& [id, groups] : state_->generated)
        {
            if (auto node = world_.find_node(id))
            {
                if (auto component = node.get_component<DestructionComponent>())
                {
                    engine::ecs_detail::component_runtime(component)->ready_ = false;
                    engine::ecs_detail::component_runtime(component)->pending_ = static_cast<bool>(
                        engine::ecs_detail::component_runtime(component)->resource_);
                }
            }
            (void)release_groups(world_, physics_, groups);
        }
    }
    state_.reset();
}

} // namespace engine::plugins::destruction
