#include "plugins/runtime/character_controller/character_camera_system.hpp"

#include "plugins/runtime/character_controller/character_camera.hpp"
#include "plugins/runtime/character_controller/character_visual.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/world/world_system.hpp"

#include "plugins/runtime/character_controller/detail/character_scene.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace engine::plugins::character_controller
{
namespace
{

bool descendant_of(const Node& node, const Node* ancestor) noexcept
{
    for (const Node* current = &node; current != nullptr; current = current->parent())
    {
        if (current == ancestor)
        {
            return true;
        }
    }
    return false;
}

schema::CharacterVisualKind visual_kind(const Node& node) noexcept
{
    for (const Node* current = &node; current != nullptr; current = current->parent())
    {
        const auto* visual = current->get_component<CharacterVisual>();
        if (visual != nullptr && visual->enabled())
        {
            return visual->settings().kind;
        }
    }
    return schema::CharacterVisualKind::body;
}

} // namespace

CharacterCameraSystem::CharacterCameraSystem(WorldSystem& world, PhysicsSystem& physics) noexcept
    : world_(world), physics_(physics)
{
}

CharacterCameraSystem::~CharacterCameraSystem() noexcept
{
    shutdown();
}

bool CharacterCameraSystem::on_initialize(const EngineConfig&)
{
    auto& registry = world_.component_registry();
    return (registry.contains(CharacterCamera::component_type) ||
            registry.add<CharacterCamera>()) &&
           (registry.contains(CharacterVisual::component_type) || registry.add<CharacterVisual>());
}

bool CharacterCameraSystem::on_tick(double)
{
    auto* scene = world_.scene();
    if (scene == nullptr)
    {
        clear_visibility();
        return true;
    }
    for (const auto& node : scene->nodes())
    {
        auto* rig = node->get_component<CharacterCamera>();
        auto* camera = node->get_component<Camera>();
        if (rig == nullptr || !rig->enabled() || !detail::active(*node) ||
            rig->settings().target_node_id == 0 || (camera != nullptr && !camera->enabled()))
        {
            continue;
        }
        auto* target = detail::find_local_node(*scene, rig->settings().target_node_id);
        if (camera == nullptr || node->parent() != nullptr || target == nullptr ||
            descendant_of(*target, node.get()) || !rig->synchronize_angles())
        {
            clear_visibility();
            return false;
        }
        if (!detail::active(*target))
        {
            continue;
        }
        float world_matrix[16];
        if (!target->get_component<Transform>()->world_matrix(world_matrix))
        {
            clear_visibility();
            return false;
        }
        const auto& settings = rig->settings();
        const Vec3 pivot(world_matrix[12], world_matrix[13] + settings.eye_height,
                         world_matrix[14]);
        Vec3 position = pivot;
        if (settings.mode == schema::CharacterView::third_person)
        {
            const double yaw = rig->yaw() * std::numbers::pi / 180.0;
            const double pitch = rig->pitch() * std::numbers::pi / 180.0;
            const Vec3 displacement(std::sin(yaw) * std::cos(pitch) * settings.camera_distance,
                                    -std::sin(pitch) * settings.camera_distance,
                                    std::cos(yaw) * std::cos(pitch) * settings.camera_distance);
            float fraction = 0.0F;
            if (!physics_.cast_sphere(pivot, displacement, settings.collision_radius, fraction))
            {
                clear_visibility();
                return false;
            }
            const double safe_fraction = std::max(
                0.0,
                static_cast<double>(fraction) -
                    (fraction < 1.0F ? settings.collision_margin / settings.camera_distance : 0.0));
            position = {pivot.x() + displacement.x() * safe_fraction,
                        pivot.y() + displacement.y() * safe_fraction,
                        pivot.z() + displacement.z() * safe_fraction};
        }
        auto* transform = node->get_component<Transform>();
        auto pose = transform->settings();
        pose.position = position;
        pose.rotation = {rig->pitch(), rig->yaw(), 0};
        if (!transform->set_settings(pose))
        {
            clear_visibility();
            return false;
        }
    }
    return update_visibility();
}

bool CharacterCameraSystem::update_visibility()
{
    auto* scene = world_.scene();
    if (visibility_scene_ != scene->id())
    {
        clear_visibility();
        visibility_scene_ = scene->id();
    }
    std::vector<std::uint64_t> hidden;
    std::vector<std::uint64_t> cameras;
    for (const auto& camera_node : scene->nodes())
    {
        auto* camera = camera_node->get_component<Camera>();
        if (camera == nullptr)
        {
            continue;
        }
        const auto* rig = camera_node->get_component<CharacterCamera>();
        const Node* first_person_target = nullptr;
        if (rig != nullptr && rig->enabled() && camera->enabled() && detail::active(*camera_node) &&
            rig->settings().mode == schema::CharacterView::first_person)
        {
            first_person_target = detail::find_local_node(*scene, rig->settings().target_node_id);
            if (first_person_target != nullptr && !detail::active(*first_person_target))
            {
                first_person_target = nullptr;
            }
        }
        hidden.clear();
        for (const auto& node : scene->nodes())
        {
            const auto* mesh = node->get_component<MeshRenderer>();
            if (mesh == nullptr)
            {
                continue;
            }
            const bool own = descendant_of(*node, first_person_target);
            const bool first_person =
                visual_kind(*node) == schema::CharacterVisualKind::first_person;
            if ((first_person && !own) || (!first_person && own))
            {
                hidden.push_back(mesh->mesh_instance_id());
            }
        }
        if (!camera->set_hidden_meshes(hidden))
        {
            clear_visibility();
            return false;
        }
        cameras.push_back(camera_node->id());
    }
    filtered_cameras_.swap(cameras);
    return true;
}

void CharacterCameraSystem::clear_visibility() noexcept
{
    auto* scene = world_.scene();
    if (scene != nullptr && scene->id() == visibility_scene_)
    {
        for (const auto id : filtered_cameras_)
        {
            auto* node = scene->find_node(id);
            auto* camera = node != nullptr ? node->get_component<Camera>() : nullptr;
            if (camera != nullptr)
            {
                camera->clear_hidden_meshes();
            }
        }
    }
    filtered_cameras_.clear();
    visibility_scene_ = 0;
}

void CharacterCameraSystem::on_shutdown() noexcept
{
    clear_visibility();
}

} // namespace engine::plugins::character_controller
