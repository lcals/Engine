#include "plugins/runtime/terrain/terrain_system.hpp"

#include "plugins/runtime/terrain/terrain_component.hpp"
#include "runtime/performance/profiling.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/rigid_body.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/world/world_system.hpp"

#include <algorithm>
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

void release_fragments(Scene& scene, PhysicsSystem& physics,
                       const std::vector<std::uint64_t>& fragments) noexcept
{
    for (auto id : fragments)
    {
        auto* node = scene.find_node(id);
        if (!node)
        {
            continue;
        }
        if (auto* collider = node->get_component<Collider>())
        {
            collider->set_enabled(false);
            // These are our transient nodes, about to be destroyed. Restore a valid pose for
            // removal even if a caller has since edited the fragment's transform or body.
            if (auto* transform = node->get_component<Transform>())
            {
                (void)transform->set_settings(TransformSettings{});
            }
            if (auto* body = node->get_component<RigidBody>())
            {
                (void)body->set_settings(schema::RigidBodyDataT{});
            }
            // Default shape is enough to remove an inactive body without allocating a shape.
            if (!physics.set_collider_data(*collider, schema::ColliderDataT{}))
            {
                // Deleting the node still lets the next physics synchronization discard it.
                collider->set_enabled(false);
            }
        }
        (void)scene.destroy_node(id);
    }
}

bool collapse_pose(const Node& node, TransformSettings& pose)
{
    const auto* transform = node.get_component<Transform>();
    if (node.parent() || !transform || !transform->enabled())
    {
        return false;
    }
    pose = transform->settings();
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
        std::function<void(bool)> callback;
        bool success = false;
    };
    std::map<std::uint64_t, Owner> owners;
    std::vector<Completion> ready;
    std::size_t pending = 0;
    std::uint64_t published_after = 0;
    std::uint64_t submitted_after = 0;
    std::shared_ptr<const std::uint8_t> lifetime = std::make_shared<const std::uint8_t>(0);

    void cancel(Owner& owner)
    {
        for (auto& request : owner.requests)
        {
            ready.push_back({std::move(request.completed), false});
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

bool TerrainSystem::commit(TerrainComponent& component, TerrainVolume candidate)
{
    ENGINE_PROFILE_ZONE("Terrain.commit");
    schema::MeshDataT mesh;
    if (!candidate.build_mesh(mesh, component.last_error_))
    {
        return false;
    }
    decorate(mesh, component.settings());
    return commit_prepared(component, std::move(candidate), std::move(mesh));
}

bool TerrainSystem::commit_prepared(TerrainComponent& component, TerrainVolume candidate,
                                    schema::MeshDataT mesh)
{
    ENGINE_PROFILE_ZONE("Terrain.publish");
    auto& node = component.node();
    if (&node.scene() != world_.scene() ||
        component.revision_ == std::numeric_limits<std::uint64_t>::max())
    {
        component.last_error_ = "Terrain is not in the active world";
        return false;
    }
    if (scene_id_ != node.scene().id())
    {
        generated_.clear();
        fragments_.clear();
        scene_id_ = node.scene().id();
    }
    auto* renderer = node.get_component<MeshRenderer>();
    auto* collider = node.get_component<Collider>();
    const bool owns = generated_.contains(node.id());
    if ((renderer || collider) && !owns)
    {
        component.last_error_ = "Terrain requires its own generated renderer and collider";
        return false;
    }
    if ((renderer && !renderer->transient()) || (collider && !collider->transient()))
    {
        component.last_error_ = "Terrain generated components were replaced";
        return false;
    }
    const bool new_renderer = renderer == nullptr;
    const bool new_collider = collider == nullptr;
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
        component.last_error_ = "Cannot create terrain output components";
        return false;
    }
    renderer->set_transient(true);
    collider->set_transient(true);
    const auto previous_mesh = *renderer->mesh_data();
    const auto previous_material = std::string(renderer->material().type_path());
    std::string material = "builtin://materials/default-pbr";
    if ((!component.settings().material_guid.empty() &&
         !ResourceSystem::guid_to_path(component.settings().material_guid, material)) ||
        !renderer->material().set_type(material))
    {
        remove_new();
        component.last_error_ = "Cannot apply terrain material";
        return false;
    }
    const bool empty = mesh.indices.empty();
    if (!empty && !renderer->set_mesh_data(mesh))
    {
        (void)renderer->material().set_type(previous_material);
        remove_new();
        component.last_error_ = "Cannot prepare terrain rendering mesh";
        return false;
    }
    const MeshCollisionData no_collision;
    const bool previous_collision_enabled = collider->enabled();
    collider->set_enabled(component.enabled() && component.settings().collision && !empty);
    if (!physics_.set_mesh_parts(
            *collider, component.settings().collision ? candidate.collision_data() : no_collision))
    {
        collider->set_enabled(previous_collision_enabled);
        const bool restored = renderer->set_mesh_data(previous_mesh) &&
                              renderer->material().set_type(previous_material);
        remove_new();
        component.last_error_ = restored ? "Cannot prepare terrain collision mesh"
                                         : "Cannot restore terrain rendering mesh";
        return false;
    }
    renderer->set_visible(!empty);
    renderer->set_enabled(component.enabled());
    component.triangles_ = mesh.indices.size() / 3;
    {
        // 包含旧碰撞几何最后引用的释放，不将其析构成本藏到提交计时之外。
        ENGINE_PROFILE_ZONE("Terrain.release_previous_volume");
        component.volume_ = std::move(candidate);
    }
    component.pending_ = false;
    component.ready_ = true;
    component.last_error_.clear();
    ++component.revision_;
    scene_id_ = node.scene().id();
    generated_.insert(node.id());
    return true;
}

bool TerrainSystem::request_apply_sphere(TerrainComponent& component, const Vec3& center,
                                         float radius, bool fill,
                                         std::function<void(bool)> completed)
{
    if (!async_ || !is_initialized() || tasks_ == nullptr || !tasks_->is_initialized() ||
        !completed || !component.ready_ || !component.enabled() ||
        &component.node().scene() != world_.scene() || !std::isfinite(center.x()) ||
        !std::isfinite(center.y()) || !std::isfinite(center.z()) || !std::isfinite(radius) ||
        radius <= 0 || radius > 10000)
    {
        component.last_error_ = "Invalid asynchronous terrain request or task domain";
        return false;
    }
    const auto id = component.node().id();
    auto found = async_->owners.find(id);
    if (found != async_->owners.end() && (found->second.scene != component.node().scene().id() ||
                                          found->second.lifetime.lock() != component.lifetime_ ||
                                          found->second.revision != component.revision_))
    {
        async_->cancel(found->second);
        async_->owners.erase(found);
    }
    if (async_->pending >= max_pending_requests_)
    {
        component.last_error_ = "Terrain asynchronous request budget is full";
        return false;
    }
    auto& owner = async_->owners[id];
    owner.scene = component.node().scene().id();
    owner.revision = component.revision_;
    owner.lifetime = component.lifetime_;
    owner.requests.push_back({center, radius, fill, std::move(completed)});
    ++async_->pending;
    component.last_error_.clear();
    return true;
}

void TerrainSystem::process_requests()
{
    ENGINE_PROFILE_ZONE("Terrain.async_publish");
    if (world_.paused())
    {
        return;
    }
    auto* scene = world_.scene();
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
        auto* node =
            scene && scene->id() == owner.scene ? scene->find_node(current->first) : nullptr;
        auto* component = node ? node->get_component<TerrainComponent>() : nullptr;
        if (!component || !component->ready_ || !component->enabled() ||
            owner.lifetime.lock() != component->lifetime_ || owner.revision != component->revision_)
        {
            async_->cancel(owner);
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
                success = commit_prepared(*component, std::move(owner.prepared->volume),
                                          std::move(owner.prepared->mesh));
                owner.revision = component->revision_;
            }
            else
            {
                component->last_error_ = owner.prepared->error.empty()
                                             ? "Terrain asynchronous preparation failed"
                                             : owner.prepared->error;
            }
            owner.prepared.reset();
            async_->ready.push_back({std::move(owner.requests.front().completed), success});
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
        auto* component = scene->find_node(id)->get_component<TerrainComponent>();
        if (!owner.prepared)
        {
            owner.prepared = std::make_shared<AsyncState::Prepared>();
            owner.prepared->volume = component->volume_;
        }
        const auto& request = owner.requests.front();
        owner.completion = tasks_->submit(
            "Terrain.prepare",
            [prepared = owner.prepared, center = request.center, radius = request.radius,
             fill = request.fill, settings = component->settings()]
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
        completion.callback(completion.success);
    }
}

bool TerrainSystem::on_tick(double)
{
    return world_.paused() || synchronize();
}

std::size_t TerrainSystem::fragment_count(const TerrainComponent& component) const noexcept
{
    const auto* scene = world_.scene();
    if (!scene || scene->id() != scene_id_ || &component.node().scene() != scene)
    {
        return 0;
    }
    const auto found = fragments_.find(component.node().id());
    if (found == fragments_.end())
    {
        return 0;
    }
    return static_cast<std::size_t>(std::count_if(found->second.begin(), found->second.end(),
                                                  [&](std::uint64_t id)
                                                  { return scene->find_node(id) != nullptr; }));
}

void TerrainSystem::clear_fragments(TerrainComponent& component) noexcept
{
    auto* scene = world_.scene();
    if (!scene || scene->id() != scene_id_ || &component.node().scene() != scene)
    {
        return;
    }
    const auto found = fragments_.find(component.node().id());
    if (found != fragments_.end())
    {
        release_fragments(*scene, physics_, found->second);
        fragments_.erase(found);
    }
}

bool TerrainSystem::collapse_detached(TerrainComponent& component, std::uint32_t min_samples,
                                      std::uint32_t max_fragments, std::uint32_t fragment_cells,
                                      float density)
{
    ENGINE_PROFILE_ZONE("Terrain.collapse");
    auto* scene = world_.scene();
    TransformSettings pose;
    if (!is_initialized() || !scene || scene != &component.node().scene() || !component.ready_ ||
        !component.enabled() || !component.settings().collision ||
        !collapse_pose(component.node(), pose) || !std::isfinite(density) || density <= 0 ||
        density > 100000 || max_fragments == 0 || max_fragments > 128)
    {
        component.last_error_ = "Terrain collapse requires an active root, positive uniform scale, "
                                "collision, valid density and a 1..128 fragment budget";
        return false;
    }
    const auto current = fragment_count(component);
    if (current >= max_fragments)
    {
        component.last_error_ = "Terrain has reached its total active fragment budget";
        return false;
    }
    auto candidate = component.volume_;
    std::vector<TerrainFragment> fragments;
    if (!candidate.extract_detached(min_samples,
                                    max_fragments - static_cast<std::uint32_t>(current),
                                    fragment_cells, fragments, component.last_error_))
    {
        return false;
    }
    if (fragments.empty())
    {
        component.last_error_.clear();
        return true;
    }
    std::string material = "builtin://materials/default-pbr";
    if (!component.settings().material_guid.empty() &&
        !ResourceSystem::guid_to_path(component.settings().material_guid, material))
    {
        component.last_error_ = "Cannot resolve terrain fragment material";
        return false;
    }
    std::vector<std::uint64_t> created;
    const auto fail = [&](const char* message)
    {
        release_fragments(*scene, physics_, created);
        component.last_error_ = message;
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
            auto* node = scene->create_node("terrain-fragment");
            if (!node)
            {
                return fail("Cannot create terrain fragment node");
            }
            node->set_transient(true);
            created.push_back(node->id());
            auto* renderer = node->add_component<MeshRenderer>();
            auto* collider = node->add_component<Collider>();
            auto* body = node->add_component<RigidBody>();
            schema::RigidBodyDataT body_settings;
            body_settings.mass = static_cast<float>(mass);
            decorate(fragment.mesh, component.settings());
            if (!renderer || !collider || !body ||
                !node->get_component<Transform>()->set_settings(pose) ||
                !renderer->material().set_type(material) ||
                !renderer->set_mesh_data(fragment.mesh) || !body->set_settings(body_settings) ||
                !physics_.set_collider_data(*collider, fragment.collision))
            {
                return fail(
                    "Cannot prepare terrain fragment rendering or convex compound collision");
            }
        }
    }
    // No simulation/query runs between preparing candidate bodies and committing the source.
    if (!commit(component, std::move(candidate)))
    {
        release_fragments(*scene, physics_, created);
        return false;
    }
    auto& owned = fragments_[component.node().id()];
    std::erase_if(owned, [&](std::uint64_t id) { return scene->find_node(id) == nullptr; });
    owned.insert(owned.end(), created.begin(), created.end());
    component.detached_ = true;
    return true;
}

bool TerrainSystem::synchronize()
{
    ENGINE_PROFILE_ZONE("Terrain.synchronize");
    if (!is_initialized())
    {
        return false;
    }
    auto* scene = world_.scene();
    if (!scene || scene->id() != scene_id_)
    {
        generated_.clear();
        fragments_.clear();
        scene_id_ = scene ? scene->id() : 0;
    }
    if (!scene)
    {
        process_requests();
        return true;
    }
    for (auto item = fragments_.begin(); item != fragments_.end();)
    {
        auto* node = scene->find_node(item->first);
        auto* component = node ? node->get_component<TerrainComponent>() : nullptr;
        if (!component || !component->resource_)
        {
            release_fragments(*scene, physics_, item->second);
            item = fragments_.erase(item);
            continue;
        }
        std::erase_if(item->second,
                      [&](std::uint64_t id) { return scene->find_node(id) == nullptr; });
        const auto* transform = node->get_component<Transform>();
        const bool enabled = component->enabled() && transform && transform->enabled();
        for (auto id : item->second)
        {
            auto* fragment = scene->find_node(id);
            if (auto* renderer = fragment->get_component<MeshRenderer>())
            {
                renderer->set_enabled(enabled);
            }
            if (auto* collider = fragment->get_component<Collider>())
            {
                if (collider->enabled() != enabled)
                {
                    const bool previous = collider->enabled();
                    collider->set_enabled(enabled);
                    if (!physics_.set_collider_data(*collider, collider->settings()))
                    {
                        collider->set_enabled(previous);
                        component->last_error_ = "Cannot synchronize terrain fragment collision";
                        return false;
                    }
                }
            }
        }
        ++item;
    }
    for (auto item = generated_.begin(); item != generated_.end();)
    {
        auto* node = scene->find_node(*item);
        auto* component = node ? node->get_component<TerrainComponent>() : nullptr;
        if (!component || !component->resource_)
        {
            if (node)
            {
                auto* renderer = node->get_component<MeshRenderer>();
                auto* collider = node->get_component<Collider>();
                if (renderer && renderer->transient())
                {
                    (void)node->remove_component(MeshRenderer::component_type);
                }
                if (collider && collider->transient())
                {
                    (void)node->remove_component(Collider::component_type);
                }
            }
            item = generated_.erase(item);
        }
        else
        {
            ++item;
        }
    }
    for (const auto& node : scene->nodes())
    {
        auto* component = node->get_component<TerrainComponent>();
        if (!component)
        {
            continue;
        }
        if (component->pending_ && component->enabled())
        {
            if (!commit(*component, component->volume_))
            {
                return false;
            }
        }
        if (generated_.contains(node->id()))
        {
            if (auto* renderer = node->get_component<MeshRenderer>())
            {
                renderer->set_enabled(component->enabled());
            }
            if (auto* collider = node->get_component<Collider>())
            {
                collider->set_enabled(component->enabled() && component->settings().collision &&
                                      component->triangles_ != 0);
            }
        }
    }
    process_requests();
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
    auto* scene = world_.scene();
    if (scene && scene->id() == scene_id_)
    {
        for (const auto& [id, fragments] : fragments_)
        {
            release_fragments(*scene, physics_, fragments);
        }
        for (auto id : generated_)
        {
            auto* node = scene->find_node(id);
            if (!node)
            {
                continue;
            }
            if (auto* component = node->get_component<TerrainComponent>())
            {
                component->ready_ = false;
                component->pending_ = component->resource_ != nullptr;
            }
            if (auto* renderer = node->get_component<MeshRenderer>();
                renderer && renderer->transient())
            {
                (void)node->remove_component(MeshRenderer::component_type);
            }
            if (auto* collider = node->get_component<Collider>(); collider && collider->transient())
            {
                (void)node->remove_component(Collider::component_type);
            }
        }
    }
    generated_.clear();
    fragments_.clear();
    scene_id_ = 0;
}

} // namespace engine::plugins::terrain
