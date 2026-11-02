#include "plugins/runtime/destruction/destruction_system.hpp"

#include "plugins/runtime/destruction/destruction_component.hpp"
#include "runtime/performance/profiling.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/rigid_body.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/world/world_system.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
};

bool finite(const Vec3& value)
{
    return std::isfinite(value.x()) && std::isfinite(value.y()) && std::isfinite(value.z());
}

bool active_component(const DestructionComponent& component)
{
    const auto* transform = component.node().get_component<Transform>();
    return component.enabled() && transform != nullptr && transform->enabled();
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
                      const Collider& support, const PhysicsSystem& physics, float max_gap,
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

bool release_groups(Scene& scene, PhysicsSystem& physics, const std::vector<GeneratedGroup>& groups)
{
    std::vector<std::pair<Collider*, bool>> disabled;
    // Keep every node alive until the whole batch has released its collision successfully.
    for (const auto& group : groups)
    {
        auto* node = scene.find_node(group.node);
        if (!node)
        {
            continue;
        }
        if (auto* collider = node->get_component<Collider>())
        {
            const bool enabled = collider->enabled();
            collider->set_enabled(false);
            if (!physics.set_collider_data(*collider, collider->settings()))
            {
                collider->set_enabled(enabled);
                bool restored = true;
                for (const auto& [previous, previous_enabled] : disabled)
                {
                    previous->set_enabled(previous_enabled);
                    if (!physics.set_collider_data(*previous, previous->settings()))
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
        if (scene.find_node(group.node))
        {
            result = scene.destroy_node(group.node) && result;
        }
    }
    return result;
}

bool read_group(const Scene& scene, const GeneratedGroup& group,
                schema::DestructionGroupStateT& output)
{
    const auto* node = scene.find_node(group.node);
    const auto* transform = node ? node->get_component<Transform>() : nullptr;
    const auto* body = node ? node->get_component<RigidBody>() : nullptr;
    if (!transform || !body || !node->transient())
    {
        return false;
    }
    output.chunks = group.chunks;
    output.transform = std::make_unique<TransformSettings>(transform->settings());
    output.linear_velocity = body->settings().linear_velocity;
    output.angular_velocity = body->settings().angular_velocity;
    return true;
}

} // namespace

struct DestructionSystem::State
{
    std::uint64_t scene_id = 0;
    std::map<std::uint64_t, std::vector<GeneratedGroup>> generated;
};

DestructionSystem::DestructionSystem(WorldSystem& world, PhysicsSystem& physics) noexcept
    : world_(world), physics_(physics)
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
    if (active_system)
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
    active_system = this;
    return true;
}

bool DestructionSystem::publish(DestructionComponent& component, DestructionGraph candidate,
                                const std::vector<schema::DestructionGroupStateT>& groups,
                                bool reuse_groups)
{
    ENGINE_PROFILE_ZONE("Destruction.publish_fragments");
    auto* scene = world_.scene();
    if (!state_ || !scene || scene != &component.node().scene() || component.node().parent() ||
        groups.size() != candidate.groups().size() ||
        component.revision_ == std::numeric_limits<std::uint64_t>::max())
    {
        component.last_error_ = "Destruction requires a root node in the active world";
        return false;
    }
    if (state_->scene_id != scene->id())
    {
        state_->generated.clear();
        state_->scene_id = scene->id();
    }
    std::string material = "builtin://materials/default-pbr";
    if (!component.settings().material_guid.empty() &&
        !ResourceSystem::guid_to_path(component.settings().material_guid, material))
    {
        component.last_error_ = "Cannot resolve destruction material";
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
    std::vector<std::pair<RigidBody*, schema::RigidBodyDataT>> velocity_updates;
    bool changed = !reuse_groups || candidate.healths() != component.graph_.healths();
    const auto fail = [&](std::string error)
    {
        if (!release_groups(*scene, physics_, created))
        {
            error += "; failed to clean candidate bodies";
        }
        component.last_error_ = std::move(error);
        return false;
    };
    for (std::size_t index = 0; index < groups.size(); ++index)
    {
        const auto& description = candidate.groups()[index];
        const auto& saved = groups[index];
        if (saved.chunks != description.chunks || !saved.transform ||
            !valid_transform(*saved.transform) || !finite(saved.linear_velocity) ||
            !finite(saved.angular_velocity))
        {
            return fail("Invalid destruction group pose or identity");
        }
        // Blast actor IDs may change during a clone/split; the sorted source chunks are stable.
        const auto found = previous_by_chunk.find(description.chunks.front());
        const auto* old =
            reuse_groups && found != previous_by_chunk.end() ? found->second : nullptr;
        if (old && old->chunks == description.chunks && old->anchored == description.anchored)
        {
            auto* node = scene->find_node(old->node);
            auto* transform = node ? node->get_component<Transform>() : nullptr;
            auto* renderer = node ? node->get_component<MeshRenderer>() : nullptr;
            auto* collider = node ? node->get_component<Collider>() : nullptr;
            auto* body = node ? node->get_component<RigidBody>() : nullptr;
            if (!node || !node->transient() || !transform || !renderer || !collider || !body)
            {
                return fail("A generated destruction fragment cannot be reused");
            }
            if (transform->settings() != *saved.transform)
            {
                return fail("Preserved destruction group pose changed during preparation");
            }
            // Preserve damping, gravity and pending impulses on an unchanged physical group.
            auto settings = body->settings();
            settings.linear_velocity = saved.linear_velocity;
            settings.angular_velocity = saved.angular_velocity;
            if (!RigidBody::valid_settings(settings) ||
                settings.motion != (description.anchored ? schema::BodyMotion::static_body
                                                         : schema::BodyMotion::dynamic) ||
                collider->settings().shape != schema::ColliderShape::compound ||
                renderer->enabled() != enabled || collider->enabled() != enabled ||
                !body->enabled())
            {
                return fail("Preserved destruction group has invalid body settings");
            }
            if (settings != body->settings())
            {
                velocity_updates.emplace_back(body, std::move(settings));
                changed = true;
            }
            published.push_back(*old);
            previous_by_chunk.erase(found);
            continue;
        }
        changed = true;
        schema::MeshDataT mesh;
        schema::ColliderDataT collision;
        collision.shape = schema::ColliderShape::compound;
        collision.friction = component.settings().friction;
        collision.restitution = component.settings().restitution;
        for (auto chunk_index : description.chunks)
        {
            const auto& source = *asset.chunks[chunk_index]->mesh;
            const auto offset = static_cast<std::uint32_t>(mesh.vertices.size());
            mesh.vertices.insert(mesh.vertices.end(), source.vertices.begin(),
                                 source.vertices.end());
            for (auto vertex_index : source.indices)
            {
                mesh.indices.push_back(offset + vertex_index);
            }
            for (std::size_t vertex_index = 0; vertex_index < source.vertices.size();
                 ++vertex_index)
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
        double volume = 0;
        (void)center_of(asset, description.chunks, volume);
        const double scale = saved.transform->scale.x();
        const double mass = volume * scale * scale * scale * component.settings().density;
        if (!std::isfinite(mass) || mass < 0.0001 || mass > 1000000)
        {
            return fail("Destruction group mass is outside the physics range");
        }
        auto* node = scene->create_node("destruction-fragment");
        if (!node)
        {
            return fail("Cannot create destruction fragment node");
        }
        node->set_transient(true);
        created.push_back({description.chunks, node->id(), description.anchored});
        auto* renderer = node->add_component<MeshRenderer>();
        auto* collider = node->add_component<Collider>();
        auto* body = node->add_component<RigidBody>();
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
        if (!renderer || !collider || !body ||
            !node->get_component<Transform>()->set_settings(*saved.transform) ||
            !renderer->material().set_type(material) || !renderer->set_mesh_data(mesh) ||
            !body->set_settings(settings))
        {
            return fail("Cannot prepare destruction fragment render or body settings");
        }
        renderer->set_enabled(enabled);
        collider->set_enabled(enabled);
        if (!physics_.set_collider_data(*collider, collision))
        {
            return fail("Cannot prepare destruction convex compound collision");
        }
        published.push_back(created.back());
    }
    if (!changed)
    {
        component.last_error_.clear();
        return true;
    }
    std::vector<GeneratedGroup> retired;
    for (const auto& [chunk, group] : previous_by_chunk)
    {
        (void)chunk;
        retired.push_back(*group);
    }
    // No physics step or query occurs between preparation and this publication point.
    if (!release_groups(*scene, physics_, retired))
    {
        return fail("Cannot release previous destruction groups");
    }
    // All setters have been validated above; the next physics synchronization consumes these
    // velocities. No collider reset is needed for groups whose shape/motion did not change.
    for (const auto& [body, settings] : velocity_updates)
    {
        if (!body->set_settings(settings))
        {
            return fail("Cannot publish validated destruction velocity");
        }
    }
    previous = std::move(published);
    component.graph_ = std::move(candidate);
    component.ready_ = true;
    component.pending_ = false;
    component.last_error_.clear();
    ++component.revision_;
    return true;
}

bool DestructionSystem::prepare_groups(DestructionComponent& component,
                                       const DestructionGraph& candidate, bool preserve,
                                       std::vector<schema::DestructionGroupStateT>& states)
{
    ENGINE_PROFILE_ZONE("Destruction.prepare_groups");
    const auto* transform = component.node().get_component<Transform>();
    if (!transform || !valid_transform(transform->settings()))
    {
        component.last_error_ = "Destruction requires a positive uniform scale";
        return false;
    }
    schema::DestructionStateDataT previous;
    if (preserve && !snapshot(component, previous))
    {
        component.last_error_ = "Cannot capture destruction groups before replacement";
        return false;
    }
    states.clear();
    for (const auto& group : candidate.groups())
    {
        schema::DestructionGroupStateT value;
        value.chunks = group.chunks;
        value.transform = std::make_unique<TransformSettings>(transform->settings());
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
                component.last_error_ = "Replacement destruction group has no source pose";
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
            auto* source_node = generated != record->second.end()
                                    ? component.node().scene().find_node(generated->node)
                                    : nullptr;
            float matrix[16];
            if (!source_node || !source_node->get_component<Transform>()->world_matrix(matrix))
            {
                component.last_error_ = "Cannot resolve source destruction transform";
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

bool DestructionSystem::rebuild(DestructionComponent& component, DestructionGraph candidate,
                                bool preserve)
{
    std::vector<schema::DestructionGroupStateT> states;
    return prepare_groups(component, candidate, preserve, states) &&
           publish(component, std::move(candidate), states, false);
}

bool DestructionSystem::damage(DestructionComponent& component, const Vec3& center, float radius,
                               float damage_value, float impulse)
{
    ENGINE_PROFILE_ZONE("Destruction.damage");
    if (!state_ || !component.ready_ || !active_component(component) || !finite(center) ||
        !std::isfinite(radius) || radius <= 0 || radius > 100000 || !std::isfinite(damage_value) ||
        damage_value < 0 || damage_value > 1000000 || !std::isfinite(impulse) || impulse < 0 ||
        impulse > 1000000)
    {
        component.last_error_ = "Invalid destruction sphere, damage or impulse";
        return false;
    }
    const auto found = state_->generated.find(component.node().id());
    DestructionGraph candidate;
    if (found == state_->generated.end() ||
        !component.graph_.clone(candidate, component.last_error_))
    {
        component.last_error_ = "Cannot prepare destruction damage candidate";
        return false;
    }
    for (const auto& group : component.graph_.groups())
    {
        const auto generated =
            std::find_if(found->second.begin(), found->second.end(),
                         [&](const auto& item) { return item.chunks == group.chunks; });
        auto* node = generated != found->second.end()
                         ? component.node().scene().find_node(generated->node)
                         : nullptr;
        auto* transform = node ? node->get_component<Transform>() : nullptr;
        float matrix[16];
        if (!transform || !valid_transform(transform->settings()) ||
            !transform->world_matrix(matrix))
        {
            component.last_error_ = "Cannot locate destruction group transform";
            return false;
        }
        const double scale = transform->settings().scale.x();
        if (!candidate.damage_sphere(group.id, local_point(matrix, center, scale),
                                     static_cast<float>(radius / scale), damage_value,
                                     component.last_error_))
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
        auto* node = source == found->second.end()
                         ? nullptr
                         : component.node().scene().find_node(source->node);
        float matrix[16];
        double volume = 0;
        if (!node || !node->get_component<Transform>()->world_matrix(matrix))
        {
            component.last_error_ = "Cannot prepare destruction impulse";
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

bool DestructionSystem::reset(DestructionComponent& component)
{
    DestructionGraph candidate;
    return component.resource_ &&
           candidate.initialize(component.graph_.data(), component.last_error_) &&
           rebuild(component, std::move(candidate), false);
}

bool DestructionSystem::apply_stress(DestructionComponent& component, const Vec3& gravity,
                                     float elastic_limit, float fatal_limit,
                                     std::uint32_t iterations, std::uint32_t& broken_bonds)
{
    ENGINE_PROFILE_ZONE("Destruction.apply_stress");
    broken_bonds = 0;
    if (!state_ || !component.ready_ || !active_component(component) || !finite(gravity) ||
        std::abs(gravity.x()) > 100000 || std::abs(gravity.y()) > 100000 ||
        std::abs(gravity.z()) > 100000 || !std::isfinite(elastic_limit) || elastic_limit <= 0 ||
        !std::isfinite(fatal_limit) || fatal_limit < elastic_limit || iterations == 0 ||
        iterations > 1000)
    {
        component.last_error_ = "Invalid destruction gravity stress parameters";
        return false;
    }
    const auto found = state_->generated.find(component.node().id());
    DestructionGraph candidate;
    if (found == state_->generated.end() ||
        !component.graph_.clone(candidate, component.last_error_))
    {
        component.last_error_ = "Cannot prepare destruction stress candidate";
        return false;
    }
    std::uint32_t count = 0;
    for (const auto& group : component.graph_.groups())
    {
        if (!group.anchored)
        {
            continue;
        }
        const auto generated =
            std::find_if(found->second.begin(), found->second.end(),
                         [&](const auto& item) { return item.chunks == group.chunks; });
        auto* node = generated != found->second.end()
                         ? component.node().scene().find_node(generated->node)
                         : nullptr;
        const auto* transform = node ? node->get_component<Transform>() : nullptr;
        float matrix[16];
        if (!transform || !valid_transform(transform->settings()) ||
            !transform->world_matrix(matrix))
        {
            component.last_error_ = "Cannot locate destruction stress group transform";
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
                                    component.last_error_))
        {
            return false;
        }
        count += group_broken;
    }
    if (candidate.healths() == component.graph_.healths())
    {
        component.last_error_.clear();
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

bool DestructionSystem::owns_fragment(const DestructionComponent& component,
                                      std::uint64_t node_id) const noexcept
{
    const auto* scene = world_.scene();
    if (!state_ || !component.ready_ || !scene || scene != &component.node().scene() ||
        state_->scene_id != scene->id() ||
        component.node().get_component<DestructionComponent>() != &component)
    {
        return false;
    }
    const auto found = state_->generated.find(component.node().id());
    const auto* node = scene->find_node(node_id);
    return found != state_->generated.end() && node && node->transient() &&
           std::any_of(found->second.begin(), found->second.end(),
                       [node_id](const auto& group) { return group.node == node_id; });
}

bool DestructionSystem::refresh_support(DestructionComponent& component, const Collider& support,
                                        float max_gap, float min_coverage)
{
    ENGINE_PROFILE_ZONE("Destruction.refresh_support");
    auto* scene = world_.scene();
    if (!state_ || !component.ready_ || !active_component(component) || !scene ||
        scene != &component.node().scene() || scene != &support.node().scene() ||
        state_->scene_id != scene->id() ||
        component.node().get_component<DestructionComponent>() != &component ||
        support.node().get_component<Collider>() != &support || !std::isfinite(max_gap) ||
        max_gap <= 0 || max_gap > 1000 || !std::isfinite(min_coverage) || min_coverage <= 0 ||
        min_coverage > 1)
    {
        component.last_error_ = "Invalid destruction foundation query, component or scene";
        return false;
    }
    const auto* support_body = support.node().get_component<RigidBody>();
    if (support.settings().is_trigger || &support.node() == &component.node() ||
        owns_fragment(component, support.node().id()) ||
        (support_body && support_body->settings().motion != schema::BodyMotion::static_body))
    {
        component.last_error_ = "Destruction support must be an external static solid collider";
        return false;
    }
    const auto found = state_->generated.find(component.node().id());
    if (found == state_->generated.end())
    {
        component.last_error_ = "Cannot locate generated destruction groups";
        return false;
    }
    const auto& asset = component.graph_.data();
    const auto health = component.graph_.healths();
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
        const auto* node = group != found->second.end() ? scene->find_node(group->node) : nullptr;
        const auto* transform = node ? node->get_component<Transform>() : nullptr;
        float matrix[16];
        if (!transform || !valid_transform(transform->settings()) ||
            !transform->world_matrix(matrix))
        {
            component.last_error_ = "Cannot locate the current foundation group transform";
            return false;
        }
        double coverage = 0;
        if (!support_coverage(chunk, matrix, support, physics_, max_gap, coverage,
                              component.last_error_))
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
        component.last_error_.clear();
        return true;
    }
    DestructionGraph candidate;
    std::vector<schema::DestructionGroupStateT> states;
    return component.graph_.clone(candidate, component.last_error_) &&
           candidate.release_anchors(released, component.last_error_) &&
           prepare_groups(component, candidate, true, states) &&
           publish(component, std::move(candidate), states, true);
}

bool DestructionSystem::snapshot(const DestructionComponent& component,
                                 schema::DestructionStateDataT& output) const
{
    output = {};
    if (!state_ || !component.ready_ || !component.resource_ || !world_.scene() ||
        world_.scene() != &component.node().scene() || state_->scene_id != world_.scene()->id())
    {
        return false;
    }
    const auto found = state_->generated.find(component.node().id());
    if (found == state_->generated.end())
    {
        return false;
    }
    schema::DestructionStateDataT state;
    state.base_guid = component.resource_->info().guid;
    state.base_hash = component.resource_->info().content_hash;
    state.bond_health = component.graph_.healths();
    for (const auto& group : found->second)
    {
        auto saved = std::make_unique<schema::DestructionGroupStateT>();
        if (!read_group(*world_.scene(), group, *saved))
        {
            return false;
        }
        state.groups.push_back(std::move(saved));
    }
    output = std::move(state);
    return true;
}

bool DestructionSystem::restore(DestructionComponent& component,
                                const schema::DestructionStateDataT& state)
{
    if (!component.resource_ || state.schema_version != 1 ||
        state.base_guid != component.resource_->info().guid ||
        state.base_hash != component.resource_->info().content_hash)
    {
        component.last_error_ = "Destruction state does not match its base resource";
        return false;
    }
    DestructionGraph candidate;
    if (!candidate.initialize(component.graph_.data(), component.last_error_) ||
        !candidate.restore_health(state.bond_health, component.last_error_))
    {
        return false;
    }
    if (state.groups.size() != candidate.groups().size())
    {
        component.last_error_ = "Destruction state group count does not match its bond state";
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
            component.last_error_ = "Destruction state contains a mismatched group";
            return false;
        }
        groups.push_back(**found);
    }
    return publish(component, std::move(candidate), groups, false);
}

bool DestructionSystem::on_tick(double)
{
    return world_.paused() || synchronize();
}

bool DestructionSystem::synchronize()
{
    ENGINE_PROFILE_ZONE("Destruction.synchronize");
    if (!state_ || !is_initialized())
    {
        return false;
    }
    auto* scene = world_.scene();
    if (!scene || scene->id() != state_->scene_id)
    {
        state_->generated.clear();
        state_->scene_id = scene ? scene->id() : 0;
    }
    if (!scene)
    {
        return true;
    }
    for (auto found = state_->generated.begin(); found != state_->generated.end();)
    {
        auto* node = scene->find_node(found->first);
        auto* component = node ? node->get_component<DestructionComponent>() : nullptr;
        if (!component || !component->resource_)
        {
            if (!release_groups(*scene, physics_, found->second))
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
    for (const auto& node : scene->nodes())
    {
        if (node->get_component<DestructionComponent>())
        {
            owners.push_back(node->id());
        }
    }
    for (auto id : owners)
    {
        auto* node = scene->find_node(id);
        if (!node || !synchronize(*node))
        {
            return false;
        }
    }
    return true;
}

bool DestructionSystem::synchronize(Node& owner)
{
    auto* scene = world_.scene();
    if (!state_ || !is_initialized() || !scene || scene != &owner.scene())
    {
        return false;
    }
    if (state_->scene_id != scene->id())
    {
        state_->generated.clear();
        state_->scene_id = scene->id();
    }
    auto* node = &owner;
    auto* component = node->get_component<DestructionComponent>();
    if (!component || !component->resource_)
    {
        const auto found = state_->generated.find(node->id());
        if (found != state_->generated.end())
        {
            if (!release_groups(*scene, physics_, found->second))
            {
                return false;
            }
            state_->generated.erase(found);
        }
        return true;
    }
    if (component->pending_ && component->enabled())
    {
        DestructionGraph candidate;
        if (!component->graph_.clone(candidate, component->last_error_) ||
            !rebuild(*component, std::move(candidate), false))
        {
            return false;
        }
    }
    const auto found = state_->generated.find(node->id());
    if (found == state_->generated.end())
    {
        return true;
    }
    const auto* transform = node->get_component<Transform>();
    if (!transform || node->parent() || !valid_transform(transform->settings()))
    {
        component->last_error_ = "Destruction requires a root node with positive uniform scale";
        return false;
    }
    const bool enabled = active_component(*component);
    for (const auto& group : found->second)
    {
        auto* fragment = scene->find_node(group.node);
        if (!fragment)
        {
            component->last_error_ = "A generated destruction fragment was removed externally";
            return false;
        }
        auto* renderer = fragment->get_component<MeshRenderer>();
        auto* collider = fragment->get_component<Collider>();
        auto* fragment_transform = fragment->get_component<Transform>();
        auto* body = fragment->get_component<RigidBody>();
        if (!renderer || !collider || !fragment_transform || !body)
        {
            component->last_error_ = "A generated destruction component was removed externally";
            return false;
        }
        const bool previous_enabled = collider->enabled();
        const auto previous_transform = fragment_transform->settings();
        const auto previous_body = body->settings();
        const bool moved = group.anchored && previous_transform != transform->settings();
        if (previous_enabled != enabled || moved)
        {
            collider->set_enabled(enabled);
            auto body_settings = previous_body;
            double volume = 0;
            (void)center_of(component->graph_.data(), group.chunks, volume);
            const double scale =
                group.anchored ? transform->settings().scale.x() : previous_transform.scale.x();
            body_settings.mass =
                static_cast<float>(volume * scale * scale * scale * component->settings().density);
            if ((moved && !fragment_transform->set_settings(transform->settings())) ||
                !body->set_settings(body_settings) ||
                !physics_.set_collider_data(*collider, collider->settings()))
            {
                collider->set_enabled(previous_enabled);
                const bool restored = fragment_transform->set_settings(previous_transform) &&
                                      body->set_settings(previous_body);
                component->last_error_ = restored ? "Cannot synchronize destruction collision"
                                                  : "Cannot restore destruction fragment settings";
                return false;
            }
        }
        renderer->set_enabled(enabled);
    }
    return true;
}

void DestructionSystem::on_shutdown() noexcept
{
    if (active_system == this)
    {
        active_system = nullptr;
    }
    auto* scene = world_.scene();
    if (state_ && scene && scene->id() == state_->scene_id)
    {
        for (const auto& [id, groups] : state_->generated)
        {
            if (auto* node = scene->find_node(id))
            {
                if (auto* component = node->get_component<DestructionComponent>())
                {
                    component->ready_ = false;
                    component->pending_ = component->resource_ != nullptr;
                }
            }
            (void)release_groups(*scene, physics_, groups);
        }
    }
    state_.reset();
}

} // namespace engine::plugins::destruction
