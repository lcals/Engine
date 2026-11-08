#include "plugins/runtime/character_controller/character_camera_system.hpp"
#include "runtime/scene/ecs_scene.hpp"
#include "runtime/scene/ecs_storage.hpp"

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

bool descendant_of(const Node& node, Node ancestor) noexcept
{
    for (Node current = node; static_cast<bool>(current); current = current.parent())
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
    for (Node current = node; static_cast<bool>(current); current = current.parent())
    {
        const auto visual = current.get_component<CharacterVisual>();
        CharacterVisual::Data settings;
        if (visual.get_settings(settings) && visual.active())
        {
            return settings.kind;
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
    if (!world_.refresh_scene_state())
    {
        return false;
    }
    if (world_.paused())
    {
        return true;
    }
    {
        const auto process =
            [&](Node node,
                const ecs_detail::ComponentStorage<CharacterCamera::Data>& stored) -> bool
        {
            const auto* scene = node.scene();
            (void)scene;
            (void)stored;

            auto rig = node.get_component<CharacterCamera>();
            auto camera = node.get_component<Camera>();
            if (!rig || !rig.active() || !detail::active(node) || stored.data.target_node_id == 0 ||
                (static_cast<bool>(camera) && !camera.active()))
            {
                return true;
            }
            if (!camera || node.parent().valid() || !rig.synchronize_angles())
            {
                clear_visibility();
                return false;
            }
            auto target = scene->find_local_node(stored.data.target_node_id);
            if (!target)
            {
                // 跨场景保留相机时，普通目标可以已经卸载；原场景 local ID 不转向新场景。
                return true;
            }
            if (descendant_of(target, node))
            {
                clear_visibility();
                return false;
            }
            if (!detail::active(target))
            {
                return true;
            }
            float world_matrix[16];
            if (!target.get_component<Transform>().world_matrix(world_matrix))
            {
                clear_visibility();
                return false;
            }
            const auto& settings = stored.data;
            const Vec3 pivot(world_matrix[12], world_matrix[13] + settings.eye_height,
                             world_matrix[14]);
            Vec3 position = pivot;
            if (settings.mode == schema::CharacterView::third_person)
            {
                const double yaw = rig.yaw() * std::numbers::pi / 180.0;
                const double pitch = rig.pitch() * std::numbers::pi / 180.0;
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
                    0.0, static_cast<double>(fraction) -
                             (fraction < 1.0F ? settings.collision_margin / settings.camera_distance
                                              : 0.0));
                position = {pivot.x() + displacement.x() * safe_fraction,
                            pivot.y() + displacement.y() * safe_fraction,
                            pivot.z() + displacement.z() * safe_fraction};
            }
            auto transform = node.get_component<Transform>();
            TransformSettings pose;
            if (!transform.get_settings(pose))
            {
                clear_visibility();
                return false;
            }
            pose.position = position;
            pose.rotation = {rig.pitch(), rig.yaw(), 0};
            if (!transform.set_settings(pose))
            {
                clear_visibility();
                return false;
            }

            return true;
        };
        auto query = flecs::world(world_.native_world())
                         .query_builder<const ecs_detail::ComponentStorage<CharacterCamera::Data>>()
                         .with<ecs_detail::Published>()
                         .build();
        bool query_success = true;
        query.each(
            [&](flecs::entity entity,
                const ecs_detail::ComponentStorage<CharacterCamera::Data>& stored)
            {
                if (query_success)
                {
                    query_success = process(world_.find_node(entity.id()), stored);
                }
            });
        if (!query_success)
        {
            return false;
        }
    }
    return update_visibility();
}

bool CharacterCameraSystem::update_visibility()
{
    if (!world_.refresh_scene_state())
    {
        return false;
    }
    std::vector<std::uint64_t> hidden;
    std::vector<std::uint64_t> cameras;
    {
        const auto process = [&](Node camera_node,
                                 const ecs_detail::ComponentStorage<Camera::Data>& stored) -> bool
        {
            const auto* scene = camera_node.scene();
            (void)scene;
            (void)stored;

            auto camera = camera_node.get_component<Camera>();
            if (!camera)
            {
                return true;
            }
            const auto rig = camera_node.get_component<CharacterCamera>();
            Node first_person_target = {};
            CharacterCamera::Data settings;
            if (rig.get_settings(settings) && rig.active() && camera.active() &&
                detail::active(camera_node) && settings.mode == schema::CharacterView::first_person)
            {
                first_person_target = scene->find_local_node(settings.target_node_id);
                if (static_cast<bool>(first_person_target) && !detail::active(first_person_target))
                {
                    first_person_target = {};
                }
            }
            hidden.clear();
            {
                const auto process =
                    [&](Node node,
                        const ecs_detail::ComponentStorage<MeshRenderer::Data>& stored) -> bool
                {
                    const auto* visible_scene = node.scene();
                    (void)visible_scene;
                    (void)stored;

                    const auto mesh = node.get_component<MeshRenderer>();
                    if (!mesh)
                    {
                        return true;
                    }
                    const bool own = descendant_of(node, first_person_target);
                    const bool first_person =
                        visual_kind(node) == schema::CharacterVisualKind::first_person;
                    if ((first_person && !own) || (!first_person && own))
                    {
                        hidden.push_back(mesh.instance_id());
                    }

                    return true;
                };
                auto query =
                    flecs::world(world_.native_world())
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
                            query_success = process(world_.find_node(entity.id()), stored);
                        }
                    });
                (void)query_success;
            }
            if (!camera.set_hidden_meshes(hidden))
            {
                clear_visibility();
                return false;
            }
            cameras.push_back(camera_node.id());

            return true;
        };
        auto query = flecs::world(world_.native_world())
                         .query_builder<const ecs_detail::ComponentStorage<Camera::Data>>()
                         .with<ecs_detail::Published>()
                         .build();
        bool query_success = true;
        query.each(
            [&](flecs::entity entity, const ecs_detail::ComponentStorage<Camera::Data>& stored)
            {
                if (query_success)
                {
                    query_success = process(world_.find_node(entity.id()), stored);
                }
            });
        if (!query_success)
        {
            return false;
        }
    }
    filtered_cameras_.swap(cameras);
    return true;
}

void CharacterCameraSystem::clear_visibility() noexcept
{
    for (const auto id : filtered_cameras_)
    {
        auto node = world_.find_node(id);
        auto camera = static_cast<bool>(node) ? node.get_component<Camera>() : Camera{};
        if (static_cast<bool>(camera))
        {
            camera.clear_hidden_meshes();
        }
    }
    filtered_cameras_.clear();
}

void CharacterCameraSystem::on_shutdown() noexcept
{
    clear_visibility();
}

} // namespace engine::plugins::character_controller
