#include "plugins/runtime/terrain/terrain_system.hpp"

#include "plugins/runtime/terrain/terrain_component_runtime.hpp"
#include "runtime/scene/ecs_scene.hpp"
#include "runtime/scene/ecs_storage.hpp"

#include "plugins/runtime/terrain/terrain_component.hpp"
#include "runtime/performance/profiling.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/resource_binding.hpp"
#include "runtime/scene/rigid_body.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/world/world_system.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <deque>
#include <future>
#include <limits>
#include <string>
#include <utility>

namespace engine::plugins::terrain
{
namespace
{
TerrainSystem* active_system = nullptr;

void release_fragment(WorldSystem& world, PhysicsSystem& physics, std::uint64_t id) noexcept
{
    auto node = world.find_node(id);
    if (!node)
    {
        return;
    }
    if (auto collider = node.get_component<Collider>())
    {
        (void)collider.set_enabled(false);
        // These are our transient nodes, about to be destroyed. Restore a valid pose for
        // removal even if a caller has since edited the fragment's transform or body.
        if (auto transform = node.get_component<Transform>())
        {
            (void)transform.set_settings(TransformSettings{});
        }
        if (auto body = node.get_component<RigidBody>())
        {
            (void)body.set_settings(schema::RigidBodyDataT{});
        }
        // Default shape is enough to remove an inactive body without allocating a shape.
        if (!physics.set_collider_data(collider, schema::ColliderDataT{}))
        {
            // Deleting the node still lets the next physics synchronization discard it.
            (void)collider.set_enabled(false);
        }
    }
    (void)node.scene()->destroy_node(id);
}

bool collapse_pose(const Node& node, TransformSettings& pose)
{
    const auto transform = node.get_component<Transform>();
    if (node.parent() || !transform.get_settings(pose) || !transform.enabled())
    {
        return false;
    }
    const double scale = pose.scale.x();
    return std::isfinite(scale) && scale > 0 && scale <= 100 &&
           std::abs(scale - pose.scale.y()) <= 0.000001 * scale &&
           std::abs(scale - pose.scale.z()) <= 0.000001 * scale;
}

void decorate(schema::MeshDataT& mesh, const schema::TerrainDataT& settings)
{
    mesh.colors.reserve(mesh.vertices.size());
    mesh.texcoords.reserve(mesh.vertices.size());
    for (const auto& v : mesh.vertices)
    {
        const float t = std::clamp((v.ny() - 0.35F) / 0.5F, 0.0F, 1.0F);
        const auto& a = settings.wall_color;
        const auto& b = settings.surface_color;
        mesh.colors.emplace_back(a.r() + (b.r() - a.r()) * t, a.g() + (b.g() - a.g()) * t,
                                 a.b() + (b.b() - a.b()) * t, 1.0F);
        // A simple planar UV channel; material/shader remains project selectable.
        mesh.texcoords.emplace_back(v.x() * settings.uv_scale, v.z() * settings.uv_scale);
    }
}
} // namespace

struct TerrainSystem::AsyncState
{
    struct Prepared
    {
        TerrainVolume volume;
        schema::MeshDataT mesh;
        std::string error;
    };
    struct Request
    {
        Vec3 center;
        float radius = 0;
        bool fill = false;
        std::function<void(bool)> completed;
    };
    struct Owner
    {
        std::uint64_t scene = 0;
        std::uint64_t revision = 0;
        std::weak_ptr<const std::uint8_t> lifetime;
        std::deque<Request> requests;
        std::shared_ptr<Prepared> prepared;
        std::future<bool> completion;
    };
    struct Completion
    {
        std::uint64_t owner = 0;
        std::function<void(bool)> callback;
        bool success = false;
    };
    std::map<std::uint64_t, Owner> owners;
    std::map<std::uint64_t, std::size_t> pending_owners;
    std::vector<Completion> ready;
    std::size_t pending = 0;
    std::uint64_t published_after = 0;
    std::uint64_t submitted_after = 0;
    std::shared_ptr<const std::uint8_t> lifetime = std::make_shared<const std::uint8_t>(0);

    void cancel(std::uint64_t id, Owner& owner)
    {
        for (auto& request : owner.requests)
        {
            ready.push_back({id, std::move(request.completed), false});
        }
    }
};

TerrainSystem::TerrainSystem(WorldSystem& world, PhysicsSystem& physics, TaskSystem* tasks,
                             std::size_t max_pending_requests,
                             std::size_t max_commits_per_tick) noexcept
    : world_(world), physics_(physics), tasks_(tasks), max_pending_requests_(max_pending_requests),
      max_commits_per_tick_(max_commits_per_tick)
{
}

TerrainSystem::~TerrainSystem() noexcept
{
    shutdown();
}
TerrainSystem* TerrainSystem::instance() noexcept
{
    return active_system;
}

bool TerrainSystem::on_initialize(const EngineConfig&)
{
    if (active_system != nullptr || (tasks_ != nullptr && !tasks_->is_initialized()) ||
        max_pending_requests_ == 0 || max_commits_per_tick_ == 0)
    {
        return false;
    }
    auto& registry = world_.component_registry();
    if (!registry.contains(TerrainComponent::component_type) && !registry.add<TerrainComponent>())
    {
        return false;
    }
    async_ = std::make_unique<AsyncState>();
    active_system = this;
    return true;
}

bool TerrainSystem::commit(TerrainComponent component, TerrainVolume candidate)
{
    TerrainComponent::Data component_settings;
    if (!component.get_settings(component_settings))
    {
        return false;
    }
    if (!component || component.node().world() != &world_)
    {
        return false;
    }
    ENGINE_PROFILE_ZONE("Terrain.commit");
    schema::MeshDataT mesh;
    if (!candidate.build_mesh(mesh, engine::ecs_detail::component_runtime(component)->last_error_))
    {
        return false;
    }
    decorate(mesh, component_settings);
    return commit_prepared(component, std::move(candidate), std::move(mesh));
}

bool TerrainSystem::commit_prepared(TerrainComponent component, TerrainVolume candidate,
                                    schema::MeshDataT mesh)
{
    TerrainComponent::Data component_settings;
    if (!component.get_settings(component_settings))
    {
        return false;
    }
    if (!component || component.node().world() != &world_)
    {
        return false;
    }
    ENGINE_PROFILE_ZONE("Terrain.publish");
    auto node = component.node();
    if (world_.find_node(node.id()) != node ||
        engine::ecs_detail::component_runtime(component)->revision_ ==
            std::numeric_limits<std::uint64_t>::max())
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Terrain is not in the active world";
        return false;
    }
    auto renderer = node.get_component<MeshRenderer>();
    auto collider = node.get_component<Collider>();
    const bool owns = generated_.contains(node.id());
    if ((renderer || collider) && !owns)
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Terrain requires its own generated renderer and collider";
        return false;
    }
    if ((renderer && !renderer.transient()) || (collider && !collider.transient()))
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Terrain generated components were replaced";
        return false;
    }
    const bool new_renderer = !renderer;
    const bool new_collider = !collider;
    if (!renderer)
    {
        renderer = node.add_component<MeshRenderer>();
    }
    if (!collider)
    {
        collider = node.add_component<Collider>();
    }
    const auto remove_new = [&]()
    {
        if (new_renderer && renderer)
        {
            (void)node.remove_component(MeshRenderer::component_type);
        }
        if (new_collider && collider)
        {
            (void)node.remove_component(Collider::component_type);
        }
    };
    if (!renderer || !collider)
    {
        remove_new();
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Cannot create terrain output components";
        return false;
    }
    if (!renderer.set_transient(true) || !collider.set_transient(true))
    {
        remove_new();
        return false;
    }
    const auto previous_mesh = *renderer.mesh_data();
    const auto previous_material = std::string(renderer.material().type_path());
    const auto& material_resource = engine::ecs_detail::component_runtime(component)->material_;
    if (!material_resource)
    {
        return false;
    }
    ecs_detail::ResourceBindingScope binding(world_, material_resource.handle());
    std::string material = material_resource->info().path;
    if ((!component_settings.material_guid.empty() &&
         !ecs_detail::resource_path(component.node(), component_settings.material_guid,
                                    material)) ||
        !renderer.material().set_type(material))
    {
        remove_new();
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Cannot apply terrain material";
        return false;
    }
    const bool empty = mesh.indices.empty();
    if (!empty && !renderer.set_mesh_data(mesh))
    {
        (void)renderer.material().set_type(previous_material);
        remove_new();
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Cannot prepare terrain rendering mesh";
        return false;
    }
    const MeshCollisionData no_collision;
    const bool previous_collision_enabled = collider.enabled();
    if (!collider.set_enabled(component.enabled() && component_settings.collision && !empty) ||
        !physics_.set_mesh_parts(collider, component_settings.collision ? candidate.collision_data()
                                                                        : no_collision))
    {
        (void)collider.set_enabled(previous_collision_enabled);
        const bool restored = renderer.set_mesh_data(previous_mesh) &&
                              renderer.material().set_type(previous_material);
        remove_new();
        engine::ecs_detail::component_runtime(component)->last_error_ =
            restored ? "Cannot prepare terrain collision mesh"
                     : "Cannot restore terrain rendering mesh";
        return false;
    }
    if (!renderer.set_visible(component.visible() && !empty) ||
        !renderer.set_enabled(component.enabled()))
    {
        return false;
    }
    engine::ecs_detail::component_runtime(component)->triangles_ = mesh.indices.size() / 3;
    {
        // 包含旧碰撞几何最后引用的释放，不将其析构成本藏到提交计时之外。
        ENGINE_PROFILE_ZONE("Terrain.release_previous_volume");
        engine::ecs_detail::component_runtime(component)->volume_ = std::move(candidate);
    }
    engine::ecs_detail::component_runtime(component)->pending_ = false;
    engine::ecs_detail::component_runtime(component)->ready_ = true;
    engine::ecs_detail::component_runtime(component)->last_error_.clear();
    ++engine::ecs_detail::component_runtime(component)->revision_;
    generated_.insert(node.id());
    return true;
}

bool TerrainSystem::request_apply_sphere(TerrainComponent component, const Vec3& center,
                                         float radius, bool fill,
                                         std::function<void(bool)> completed)
{
    if (!component || component.node().world() != &world_)
    {
        return false;
    }
    if (!async_ || !is_initialized() || tasks_ == nullptr || !tasks_->is_initialized() ||
        !completed || !engine::ecs_detail::component_runtime(component)->ready_ ||
        !component.active() || world_.find_node(component.node().id()) != component.node() ||
        !std::isfinite(center.x()) || !std::isfinite(center.y()) || !std::isfinite(center.z()) ||
        !std::isfinite(radius) || radius <= 0 || radius > 10000)
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Invalid asynchronous terrain request or task domain";
        return false;
    }
    const auto id = component.node().id();
    auto found = async_->owners.find(id);
    if (found != async_->owners.end() &&
        (found->second.scene != component.node().scene()->id() ||
         found->second.lifetime.lock() !=
             engine::ecs_detail::component_runtime(component)->lifetime_ ||
         found->second.revision != engine::ecs_detail::component_runtime(component)->revision_))
    {
        async_->cancel(id, found->second);
        async_->owners.erase(found);
    }
    if (async_->pending >= max_pending_requests_)
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Terrain asynchronous request budget is full";
        return false;
    }
    auto& owner = async_->owners[id];
    owner.scene = component.node().scene()->id();
    owner.revision = engine::ecs_detail::component_runtime(component)->revision_;
    owner.lifetime = engine::ecs_detail::component_runtime(component)->lifetime_;
    owner.requests.push_back({center, radius, fill, std::move(completed)});
    ++async_->pending;
    ++async_->pending_owners[id];
    engine::ecs_detail::component_runtime(component)->last_error_.clear();
    return true;
}

void TerrainSystem::process_requests()
{
    ENGINE_PROFILE_ZONE("Terrain.async_publish");
    if (world_.paused())
    {
        return;
    }
    std::size_t commits = 0;
    auto item = async_->owners.upper_bound(async_->published_after);
    for (std::size_t remaining = async_->owners.size(); remaining != 0; --remaining)
    {
        if (item == async_->owners.end())
        {
            item = async_->owners.begin();
        }
        const auto current = item++;
        auto& owner = current->second;
        auto node = world_.find_node(current->first);
        if (node && node.scene()->id() != owner.scene)
        {
            node = {};
        }
        auto component = node ? node.get_component<TerrainComponent>() : TerrainComponent{};
        if (!component || !engine::ecs_detail::component_runtime(component)->ready_ ||
            !component.active() ||
            owner.lifetime.lock() != engine::ecs_detail::component_runtime(component)->lifetime_ ||
            owner.revision != engine::ecs_detail::component_runtime(component)->revision_)
        {
            async_->cancel(current->first, owner);
            async_->owners.erase(current);
            continue;
        }
        if (owner.completion.valid() && commits < max_commits_per_tick_ &&
            owner.completion.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            ++commits;
            async_->published_after = current->first;
            bool success = owner.completion.get();
            if (success)
            {
                success = commit_prepared(component, std::move(owner.prepared->volume),
                                          std::move(owner.prepared->mesh));
                owner.revision = engine::ecs_detail::component_runtime(component)->revision_;
            }
            else
            {
                engine::ecs_detail::component_runtime(component)->last_error_ =
                    owner.prepared->error.empty() ? "Terrain asynchronous preparation failed"
                                                  : owner.prepared->error;
            }
            owner.prepared.reset();
            async_->ready.push_back(
                {current->first, std::move(owner.requests.front().completed), success});
            owner.requests.pop_front();
        }
        if (owner.requests.empty())
        {
            async_->owners.erase(current);
        }
    }

    std::size_t running = static_cast<std::size_t>(
        std::count_if(async_->owners.begin(), async_->owners.end(),
                      [](const auto& item) { return item.second.completion.valid(); }));
    item = async_->owners.upper_bound(async_->submitted_after);
    for (std::size_t remaining = async_->owners.size(); remaining != 0; --remaining)
    {
        if (tasks_ == nullptr || running >= tasks_->max_concurrency())
        {
            break;
        }
        if (item == async_->owners.end())
        {
            item = async_->owners.begin();
        }
        const auto current = item++;
        const auto id = current->first;
        auto& owner = current->second;
        if (owner.completion.valid())
        {
            continue;
        }
        auto component = world_.find_node(id).get_component<TerrainComponent>();
        if (!owner.prepared)
        {
            owner.prepared = std::make_shared<AsyncState::Prepared>();
            owner.prepared->volume = engine::ecs_detail::component_runtime(component)->volume_;
        }
        TerrainComponent::Data component_settings;
        if (!component.get_settings(component_settings))
        {
            owner.requests.clear();
            continue;
        }
        const auto& request = owner.requests.front();
        owner.completion = tasks_->submit(
            "Terrain.prepare",
            [prepared = owner.prepared, center = request.center, radius = request.radius,
             fill = request.fill, settings = std::move(component_settings)]
            {
                ENGINE_PROFILE_ZONE("Terrain.async_prepare");
                if (!prepared->volume.apply_sphere(center, radius, fill, prepared->error) ||
                    !prepared->volume.build_mesh(prepared->mesh, prepared->error))
                {
                    return false;
                }
                decorate(prepared->mesh, settings);
                return true;
            });
        if (!owner.completion.valid())
        {
            break;
        }
        async_->submitted_after = id;
        ++running;
    }
    // Callbacks can submit new work or mutate the world; no owner references survive this point.
    auto ready = std::move(async_->ready);
    async_->ready.clear();
    async_->pending -= ready.size();
    const std::weak_ptr<const std::uint8_t> lifetime = async_->lifetime;
    for (auto& completion : ready)
    {
        if (lifetime.expired())
        {
            return;
        }
        const auto pending = async_->pending_owners.find(completion.owner);
        assert(pending != async_->pending_owners.end() && pending->second != 0);
        if (--pending->second == 0)
        {
            async_->pending_owners.erase(pending);
        }
        completion.callback(completion.success);
    }
}

bool TerrainSystem::on_tick(double delta_time)
{
    if (world_.paused())
    {
        return synchronize();
    }
    if (!std::isfinite(simulation_time_ + delta_time))
    {
        return false;
    }
    simulation_time_ += delta_time;
    return synchronize();
}

std::size_t TerrainSystem::fragment_count(TerrainComponent component) const noexcept
{
    if (!component || component.node().world() != &world_)
    {
        return 0;
    }
    if (world_.find_node(component.node().id()) != component.node())
    {
        return 0;
    }
    const auto found = fragments_.find(component.node().id());
    if (found == fragments_.end())
    {
        return 0;
    }
    if (found->second.owner.lock() != engine::ecs_detail::component_runtime(component)->lifetime_)
    {
        return 0;
    }
    return static_cast<std::size_t>(std::count_if(
        found->second.items.begin(), found->second.items.end(),
        [&](const Fragment& fragment) { return world_.find_node(fragment.node).valid(); }));
}

void TerrainSystem::clear_fragments(TerrainComponent component) noexcept
{
    if (!component || component.node().world() != &world_)
    {
        return;
    }
    if (world_.find_node(component.node().id()) != component.node())
    {
        return;
    }
    const auto found = fragments_.find(component.node().id());
    if (found != fragments_.end())
    {
        for (const auto& fragment : found->second.items)
        {
            release_fragment(world_, physics_, fragment.node);
        }
        fragments_.erase(found);
    }
}

bool TerrainSystem::retire_fragments(TerrainComponent component, std::uint32_t max_count,
                                     double max_age_seconds, std::uint32_t& retired_count)
{
    retired_count = 0;
    if (!component || component.node().world() != &world_)
    {
        return false;
    }
    ENGINE_PROFILE_ZONE("Terrain.retire_fragments");
    retired_count = 0;
    auto source = component.node();
    if (!is_initialized() || !physics_.is_initialized() ||
        !engine::ecs_detail::component_runtime(component)->ready_ ||
        engine::ecs_detail::component_runtime(component)->pending_ ||
        !engine::ecs_detail::component_runtime(component)->resource_ ||
        world_.find_node(source.id()) != source ||
        source.get_component<TerrainComponent>() != component || !std::isfinite(max_age_seconds) ||
        max_age_seconds < 0)
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Invalid terrain retirement request or owner";
        return false;
    }
    if (async_->pending_owners.contains(source.id()))
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Terrain has an unfinished asynchronous request";
        return false;
    }
    const auto source_renderer = source.get_component<MeshRenderer>();
    const auto source_collider = source.get_component<Collider>();
    if (!generated_.contains(source.id()) || !source_renderer || !source_collider ||
        !source_renderer.transient() || !source_collider.transient())
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Terrain generated components are missing or replaced";
        return false;
    }
    const auto found = fragments_.find(source.id());
    if (found == fragments_.end())
    {
        engine::ecs_detail::component_runtime(component)->last_error_.clear();
        return true;
    }
    if (found->second.owner.lock() != engine::ecs_detail::component_runtime(component)->lifetime_)
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Terrain fragments belong to a replaced component";
        return false;
    }
    auto& items = found->second.items;
    std::vector<const Fragment*> ordered;
    ordered.reserve(items.size());
    for (const auto& fragment : items)
    {
        const auto node = world_.find_node(fragment.node);
        const auto transform = node ? node.get_component<Transform>() : Transform{};
        const auto renderer = node ? node.get_component<MeshRenderer>() : MeshRenderer{};
        const auto collider = node ? node.get_component<Collider>() : Collider{};
        const auto body = node ? node.get_component<RigidBody>() : RigidBody{};
        std::vector<Component> components;
        RigidBody::Data body_settings;
        if (!node || !node.collect_components(components) || node.scene() != source.scene() ||
            node.parent() || !node.transient() || components.size() != 4 || !transform ||
            !transform.transient() || !renderer || !renderer.transient() || !collider ||
            !collider.transient() || collider.instance_id() != fragment.collider_instance ||
            !body.get_settings(body_settings) || !body.transient() ||
            body_settings.motion != schema::BodyMotion::dynamic)
        {
            engine::ecs_detail::component_runtime(component)->last_error_ =
                "Terrain fragment components are missing or replaced";
            return false;
        }
        ordered.push_back(&fragment);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const Fragment* left, const Fragment* right)
              {
                  return left->published_at != right->published_at
                             ? left->published_at < right->published_at
                             : left->node < right->node;
              });
    const auto expired = static_cast<std::size_t>(
        std::count_if(ordered.begin(), ordered.end(),
                      [&](const Fragment* fragment)
                      {
                          return max_age_seconds > 0 &&
                                 simulation_time_ - fragment->published_at >= max_age_seconds;
                      }));
    const auto excess = items.size() > max_count ? items.size() - max_count : 0;
    const auto count = std::max(expired, excess);
    if (count == 0)
    {
        engine::ecs_detail::component_runtime(component)->last_error_.clear();
        return true;
    }
    if (engine::ecs_detail::component_runtime(component)->revision_ ==
        std::numeric_limits<std::uint64_t>::max())
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Terrain revision is exhausted";
        return false;
    }
    std::vector<std::uint64_t> selected;
    selected.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        selected.push_back(ordered[index]->node);
    }
    std::sort(selected.begin(), selected.end());
    for (const auto id : selected)
    {
        bool has_children = false;
        flecs::world(world_.native_world())
            .entity(id)
            .children([&](flecs::entity) { has_children = true; });
        if (has_children)
        {
            engine::ecs_detail::component_runtime(component)->last_error_ =
                "Terrain fragment owns an unexpected child node";
            return false;
        }
    }
    // All identities are checked before mutation. The existing removal path uses a disabled
    // default shape and a valid root pose: no geometry allocation or simulation can fail here.
    for (auto id : selected)
    {
        release_fragment(world_, physics_, id);
    }
    std::erase_if(items, [&](const Fragment& fragment)
                  { return std::binary_search(selected.begin(), selected.end(), fragment.node); });
    ++engine::ecs_detail::component_runtime(component)->revision_;
    engine::ecs_detail::component_runtime(component)->last_error_.clear();
    retired_count = static_cast<std::uint32_t>(count);
    return true;
}

bool TerrainSystem::collapse_detached(TerrainComponent component, std::uint32_t min_samples,
                                      std::uint32_t max_fragments, std::uint32_t fragment_cells,
                                      float density)
{
    TerrainComponent::Data component_settings;
    if (!component.get_settings(component_settings))
    {
        return false;
    }
    if (!component || component.node().world() != &world_)
    {
        return false;
    }
    ENGINE_PROFILE_ZONE("Terrain.collapse");
    auto* scene = component.node().scene();
    TransformSettings pose;
    if (!is_initialized() || world_.find_node(component.node().id()) != component.node() ||
        !engine::ecs_detail::component_runtime(component)->ready_ || !component.active() ||
        !component_settings.collision || !collapse_pose(component.node(), pose) ||
        !std::isfinite(density) || density <= 0 || density > 100000 || max_fragments == 0 ||
        max_fragments > 128)
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Terrain collapse requires an active root, positive uniform scale, "
            "collision, valid density and a 1..128 fragment budget";
        return false;
    }
    const auto current = fragment_count(component);
    if (current >= max_fragments)
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Terrain has reached its total active fragment budget";
        return false;
    }
    auto candidate = engine::ecs_detail::component_runtime(component)->volume_;
    std::vector<TerrainFragment> fragments;
    if (!candidate.extract_detached(
            min_samples, max_fragments - static_cast<std::uint32_t>(current), fragment_cells,
            fragments, engine::ecs_detail::component_runtime(component)->last_error_))
    {
        return false;
    }
    if (fragments.empty())
    {
        engine::ecs_detail::component_runtime(component)->last_error_.clear();
        return true;
    }
    const auto& material_resource = engine::ecs_detail::component_runtime(component)->material_;
    if (!material_resource)
    {
        return false;
    }
    ecs_detail::ResourceBindingScope binding(world_, material_resource.handle());
    std::string material = material_resource->info().path;
    if (!component_settings.material_guid.empty() &&
        !ecs_detail::resource_path(component.node(), component_settings.material_guid, material))
    {
        engine::ecs_detail::component_runtime(component)->last_error_ =
            "Cannot resolve terrain fragment material";
        return false;
    }
    std::vector<std::uint64_t> created;
    const auto fail = [&](const char* message)
    {
        for (auto id : created)
        {
            release_fragment(world_, physics_, id);
        }
        engine::ecs_detail::component_runtime(component)->last_error_ = message;
        return false;
    };
    {
        ENGINE_PROFILE_ZONE("Terrain.publish_fragments");
        for (auto& fragment : fragments)
        {
            const double scale = pose.scale.x();
            const double mass = fragment.volume * density * scale * scale * scale;
            if (!std::isfinite(mass) || mass < 0.0001 || mass > 1000000)
            {
                return fail("Terrain fragment mass is outside the supported physics range");
            }
            auto node = scene->create_node("terrain-fragment");
            if (!node)
            {
                return fail("Cannot create terrain fragment node");
            }
            created.push_back(node.id());
            if (!node.set_transient(true))
            {
                return fail("Cannot mark terrain fragment transient");
            }
            auto renderer = node.add_component<MeshRenderer>();
            auto collider = node.add_component<Collider>();
            auto body = node.add_component<RigidBody>();
            schema::RigidBodyDataT body_settings;
            body_settings.mass = static_cast<float>(mass);
            decorate(fragment.mesh, component_settings);
            if (!renderer || !collider || !body ||
                !node.get_component<Transform>().set_settings(pose) ||
                !renderer.material().set_type(material) || !renderer.set_mesh_data(fragment.mesh) ||
                !body.set_settings(body_settings) ||
                !physics_.set_collider_data(collider, fragment.collision))
            {
                return fail(
                    "Cannot prepare terrain fragment rendering or convex compound collision");
            }
            if (!node.get_component<Transform>().set_transient(true) ||
                !renderer.set_transient(true) ||
                !renderer.set_visible(component.visible_in_hierarchy()) ||
                !collider.set_transient(true) || !body.set_transient(true))
            {
                return fail("Cannot finish terrain fragment state");
            }
        }
    }
    // No simulation/query runs between preparing candidate bodies and committing the source.
    if (!commit(component, std::move(candidate)))
    {
        for (auto id : created)
        {
            release_fragment(world_, physics_, id);
        }
        return false;
    }
    auto& owned = fragments_[component.node().id()];
    owned.owner = engine::ecs_detail::component_runtime(component)->lifetime_;
    std::erase_if(owned.items,
                  [&](const Fragment& fragment) { return !world_.find_node(fragment.node); });
    for (auto id : created)
    {
        owned.items.push_back(
            {id, world_.find_node(id).get_component<Collider>().instance_id(), simulation_time_});
    }
    engine::ecs_detail::component_runtime(component)->detached_ = true;
    return true;
}

bool TerrainSystem::synchronize()
{
    ENGINE_PROFILE_ZONE("Terrain.synchronize");
    if (!is_initialized())
    {
        return false;
    }
    for (auto item = fragments_.begin(); item != fragments_.end();)
    {
        auto node = world_.find_node(item->first);
        auto component = node ? node.get_component<TerrainComponent>() : TerrainComponent{};
        if (!component || !engine::ecs_detail::component_runtime(component)->resource_ ||
            item->second.owner.lock() !=
                engine::ecs_detail::component_runtime(component)->lifetime_)
        {
            for (const auto& fragment : item->second.items)
            {
                release_fragment(world_, physics_, fragment.node);
            }
            item = fragments_.erase(item);
            continue;
        }
        std::erase_if(item->second.items,
                      [&](const Fragment& fragment) { return !world_.find_node(fragment.node); });
        const auto transform = node.get_component<Transform>();
        const bool enabled = component.active() && transform && transform.active();
        for (const auto& entry : item->second.items)
        {
            auto fragment = world_.find_node(entry.node);
            if (auto renderer = fragment.get_component<MeshRenderer>())
            {
                if (!renderer.set_enabled(enabled) ||
                    !renderer.set_visible(component.visible_in_hierarchy()))
                {
                    return false;
                }
            }
            if (auto collider = fragment.get_component<Collider>())
            {
                if (collider.enabled() != enabled)
                {
                    const bool previous = collider.enabled();
                    if (!collider.set_enabled(enabled))
                    {
                        return false;
                    }
                    Collider::Data settings;
                    if (!collider.get_settings(settings) ||
                        !physics_.set_collider_data(collider, settings))
                    {
                        (void)collider.set_enabled(previous);
                        engine::ecs_detail::component_runtime(component)->last_error_ =
                            "Cannot synchronize terrain fragment collision";
                        return false;
                    }
                }
            }
        }
        ++item;
    }
    for (auto item = generated_.begin(); item != generated_.end();)
    {
        auto node = world_.find_node(*item);
        auto component = node ? node.get_component<TerrainComponent>() : TerrainComponent{};
        if (!component || !engine::ecs_detail::component_runtime(component)->resource_)
        {
            if (node)
            {
                auto renderer = node.get_component<MeshRenderer>();
                auto collider = node.get_component<Collider>();
                if (renderer && renderer.transient())
                {
                    (void)node.remove_component(MeshRenderer::component_type);
                }
                if (collider && collider.transient())
                {
                    (void)node.remove_component(Collider::component_type);
                }
            }
            item = generated_.erase(item);
        }
        else
        {
            ++item;
        }
    }
    // Commit can add or remove generated components. Finish the native query first,
    // then resolve the stable identities at the structural-change safe point.
    std::vector<Node> terrain_nodes;
    auto query = flecs::world(world_.native_world())
                     .query_builder<const ecs_detail::ComponentStorage<TerrainComponent::Data>>()
                     .with<ecs_detail::Published>()
                     .build();
    query.each([&](flecs::entity entity, const auto&)
               { terrain_nodes.push_back(world_.find_node(entity.id())); });
    for (auto node : terrain_nodes)
    {
        auto component = node.get_component<TerrainComponent>();
        if (!component)
        {
            continue;
        }
        if (!world_.paused() && engine::ecs_detail::component_runtime(component)->pending_ &&
            component.active() &&
            !commit(component, engine::ecs_detail::component_runtime(component)->volume_))
        {
            return false;
        }
        if (generated_.contains(node.id()))
        {
            if (auto renderer = node.get_component<MeshRenderer>())
            {
                if (!renderer.set_enabled(component.enabled()) ||
                    !renderer.set_visible(
                        component.visible() &&
                        engine::ecs_detail::component_runtime(component)->triangles_ != 0))
                {
                    return false;
                }
            }
            if (auto collider = node.get_component<Collider>())
            {
                TerrainComponent::Data settings;
                if (!component.get_settings(settings) ||
                    !collider.set_enabled(
                        component.enabled() && settings.collision &&
                        engine::ecs_detail::component_runtime(component)->triangles_ != 0))
                {
                    return false;
                }
            }
        }
    }
    if (!world_.paused())
    {
        process_requests();
    }
    return true;
}

void TerrainSystem::on_shutdown() noexcept
{
    // Submitted closures own only candidates; dropping futures/callbacks never waits on workers.
    async_.reset();
    if (active_system == this)
    {
        active_system = nullptr;
    }
    for (const auto& [id, fragments] : fragments_)
    {
        for (const auto& fragment : fragments.items)
        {
            release_fragment(world_, physics_, fragment.node);
        }
    }
    for (auto id : generated_)
    {
        auto node = world_.find_node(id);
        if (!node)
        {
            continue;
        }
        if (auto component = node.get_component<TerrainComponent>())
        {
            engine::ecs_detail::component_runtime(component)->ready_ = false;
            engine::ecs_detail::component_runtime(component)->pending_ =
                static_cast<bool>(engine::ecs_detail::component_runtime(component)->resource_);
        }
        if (auto renderer = node.get_component<MeshRenderer>(); renderer && renderer.transient())
        {
            (void)node.remove_component(MeshRenderer::component_type);
        }
        if (auto collider = node.get_component<Collider>(); collider && collider.transient())
        {
            (void)node.remove_component(Collider::component_type);
        }
    }
    generated_.clear();
    fragments_.clear();
    simulation_time_ = 0;
}

} // namespace engine::plugins::terrain
