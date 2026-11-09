#include "plugins/runtime/character_controller/character_camera_system.hpp"
#include "runtime/scene/ecs_scene.hpp"
#include "runtime/scene/ecs_storage.hpp"

#include "plugins/runtime/camera/camera_controller.hpp"
#include "plugins/runtime/character_controller/character_camera.hpp"
#include "plugins/runtime/character_controller/character_visual.hpp"
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

CharacterCameraSystem::CharacterCameraSystem(WorldSystem& world) noexcept : world_(world)
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
    return update_visibility();
}

bool CharacterCameraSystem::update_visibility()
{
    if (!world_.refresh_scene_state())
    {
        return false;
    }
    std::vector<std::uint64_t> hidden;
    std::vector<Camera> cameras;
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
            auto solver = camera_node.get_component<camera::CameraController>();
            camera::CameraStatus status;
            if (rig.get_settings(settings) && rig.active() && camera.active() && solver.active() &&
                solver.get_status(status) && status.has_output &&
                std::find(settings.first_person_states.begin(), settings.first_person_states.end(),
                          status.state) != settings.first_person_states.end())
            {
                if (!solver.get_target(settings.subject_slot, first_person_target))
                {
                    first_person_target = {};
                }
                if (first_person_target && !detail::active(first_person_target))
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
            cameras.push_back(camera);

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
    for (auto camera : filtered_cameras_)
    {
        if (camera)
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
