#include "plugins/runtime/terrain/terrain_system.hpp"

#include "plugins/runtime/terrain/terrain_component.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace engine::plugins::terrain
{
namespace
{
TerrainSystem* active_system = nullptr;

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

TerrainSystem::TerrainSystem(WorldSystem& world, PhysicsSystem& physics) noexcept
    : world_(world), physics_(physics)
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
    if (active_system != nullptr)
    {
        return false;
    }
    auto& registry = world_.component_registry();
    if (!registry.contains(TerrainComponent::component_type) && !registry.add<TerrainComponent>())
    {
        return false;
    }
    active_system = this;
    return true;
}

bool TerrainSystem::commit(TerrainComponent& component, TerrainVolume candidate)
{
    auto& node = component.node();
    if (&node.scene() != world_.scene() ||
        component.revision_ == std::numeric_limits<std::uint64_t>::max())
    {
        component.last_error_ = "Terrain is not in the active world";
        return false;
    }
    schema::MeshDataT mesh;
    if (!candidate.build_mesh(mesh, component.last_error_))
    {
        return false;
    }
    decorate(mesh, component.settings());
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
    std::string material = "builtin/materials/default-pbr";
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
    schema::MeshDataT no_collision;
    const bool previous_collision_enabled = collider->enabled();
    collider->set_enabled(component.enabled() && component.settings().collision && !empty);
    if (!physics_.set_mesh_data(*collider, component.settings().collision ? mesh : no_collision))
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
    component.volume_ = std::move(candidate);
    component.pending_ = false;
    component.ready_ = true;
    component.last_error_.clear();
    ++component.revision_;
    scene_id_ = node.scene().id();
    generated_.insert(node.id());
    return true;
}

bool TerrainSystem::on_tick(double)
{
    return synchronize();
}

bool TerrainSystem::synchronize()
{
    if (!is_initialized())
    {
        return false;
    }
    auto* scene = world_.scene();
    if (!scene || scene->id() != scene_id_)
    {
        generated_.clear();
        scene_id_ = scene ? scene->id() : 0;
    }
    if (!scene)
    {
        return true;
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
    return true;
}

void TerrainSystem::on_shutdown() noexcept
{
    if (active_system == this)
    {
        active_system = nullptr;
    }
    auto* scene = world_.scene();
    if (scene && scene->id() == scene_id_)
    {
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
    scene_id_ = 0;
}

} // namespace engine::plugins::terrain
