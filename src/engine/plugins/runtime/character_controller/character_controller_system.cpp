#include "plugins/runtime/character_controller/character_controller_system.hpp"

#include "plugins/runtime/character_controller/character_controller_runtime.hpp"
#include "runtime/scene/ecs_scene.hpp"
#include "runtime/scene/ecs_storage.hpp"

#include "plugins/runtime/character_controller/character_camera.hpp"
#include "plugins/runtime/character_controller/character_controller.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/character_motor.hpp"
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

bool valid_input(const Vec2& value) noexcept
{
    return std::isfinite(value.x()) && std::isfinite(value.y()) &&
           std::abs(value.x()) <= 1000000.0 && std::abs(value.y()) <= 1000000.0;
}

} // namespace

void CharacterControllerSystem::discard_component_inputs() noexcept
{
    if (!world_.native_world())
    {
        return;
    }
    {
        const auto process =
            [&](Node node,
                const ecs_detail::ComponentStorage<CharacterController::Data>& stored) -> bool
        {
            const auto* scene = node.scene();
            (void)scene;
            (void)stored;

            if (auto controller = node.get_component<CharacterController>())
            {
                engine::ecs_detail::component_runtime(controller)->input_ = {};
            }

            return true;
        };
        auto query =
            flecs::world(world_.native_world())
                .query_builder<const ecs_detail::ComponentStorage<CharacterController::Data>>()
                .with<ecs_detail::Published>()
                .build();
        bool query_success = true;
        query.each(
            [&](flecs::entity entity,
                const ecs_detail::ComponentStorage<CharacterController::Data>& stored)
            {
                if (query_success)
                {
                    query_success = process(world_.find_node(entity.id()), stored);
                }
            });
        (void)query_success;
    }
}

CharacterControllerSystem::CharacterControllerSystem(WorldSystem& world) noexcept : world_(world)
{
}

CharacterControllerSystem::~CharacterControllerSystem() noexcept
{
    shutdown();
}

bool CharacterControllerSystem::set_player(Node node) noexcept
{
    if (node.id() != 0 && (!node || node.world() != &world_))
    {
        return false;
    }
    auto controller =
        static_cast<bool>(node) ? node.get_component<CharacterController>() : CharacterController{};
    if (static_cast<bool>(node) && (world_.find_node(node.id()) != node || !controller))
    {
        return false;
    }
    stop_driving();
    explicit_player_ = true;
    selected_scene_ = static_cast<bool>(node) ? node.scene()->id() : 0;
    selected_node_ = static_cast<bool>(node) ? node.id() : 0;
    selected_controller_ = static_cast<bool>(controller) ? controller.instance_id() : 0;
    input_ = {};
    has_input_ = false;
    return true;
}

bool CharacterControllerSystem::set_input(const CharacterInput& input) noexcept
{
    if (!valid_input(input.move) || !valid_input(input.look))
    {
        return false;
    }
    input_ = input;
    has_input_ = true;
    return true;
}

bool CharacterControllerSystem::on_initialize(const EngineConfig&)
{
    auto& registry = world_.component_registry();
    return registry.contains(CharacterController::component_type) ||
           registry.add<CharacterController>();
}

void CharacterControllerSystem::stop_driving() noexcept
{
    auto node = world_.find_node(driving_node_);
    if (node && node.scene()->id() != driving_scene_)
    {
        node = {};
    }
    auto motor = static_cast<bool>(node) ? node.get_component<CharacterMotor>() : CharacterMotor{};
    if (static_cast<bool>(motor) && motor.instance_id() == driving_motor_)
    {
        motor.clear_input();
    }
    auto controller =
        static_cast<bool>(node) ? node.get_component<CharacterController>() : CharacterController{};
    if (static_cast<bool>(controller) && controller.instance_id() == driving_controller_)
    {
        controller.clear_input();
    }
    driving_scene_ = 0;
    driving_node_ = 0;
    driving_controller_ = 0;
    driving_motor_ = 0;
}

bool CharacterControllerSystem::on_tick(double)
{
    if (!world_.refresh_scene_state())
    {
        return false;
    }
    auto input = input_;
    const bool explicit_input = has_input_;
    input_ = {};
    has_input_ = false;
    if (world_.paused())
    {
        discard_component_inputs();
        stop_driving();
        return true;
    }
    Node player = {};
    if (explicit_player_)
    {
        player = world_.find_node(selected_node_);
        if (player && player.scene()->id() != selected_scene_)
        {
            player = {};
        }
        const auto component = static_cast<bool>(player)
                                   ? player.get_component<CharacterController>()
                                   : CharacterController{};
        if (!component || component.instance_id() != selected_controller_)
        {
            player = {};
        }
    }
    else
    {
        {
            const auto process =
                [&](Node node,
                    const ecs_detail::ComponentStorage<CharacterController::Data>& stored) -> bool
            {
                const auto* scene = node.scene();
                (void)scene;
                (void)stored;

                const auto component = node.get_component<CharacterController>();
                if (!component || !component.active() || !detail::active(node))
                {
                    return true;
                }
                if (static_cast<bool>(player))
                {
                    discard_component_inputs();
                    stop_driving();
                    return false;
                }
                player = node;

                return true;
            };
            auto query =
                flecs::world(world_.native_world())
                    .query_builder<const ecs_detail::ComponentStorage<CharacterController::Data>>()
                    .with<ecs_detail::Published>()
                    .build();
            bool query_success = true;
            query.each(
                [&](flecs::entity entity,
                    const ecs_detail::ComponentStorage<CharacterController::Data>& stored)
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
    }
    auto controller = static_cast<bool>(player) ? player.get_component<CharacterController>()
                                                : CharacterController{};
    auto motor =
        static_cast<bool>(player) ? player.get_component<CharacterMotor>() : CharacterMotor{};
    if (!explicit_input && static_cast<bool>(controller))
    {
        input = engine::ecs_detail::component_runtime(controller)->input_;
    }
    discard_component_inputs();
    if (!controller || !controller.active() || !detail::active(player) || !motor || !motor.active())
    {
        stop_driving();
        return true;
    }
    if (driving_scene_ != player.scene()->id() || driving_node_ != player.id() ||
        driving_controller_ != controller.instance_id() || driving_motor_ != motor.instance_id())
    {
        stop_driving();
    }
    auto transform = player.get_component<Transform>();
    auto fail = [this, motor]() mutable
    {
        stop_driving();
        motor.clear_input();
        return false;
    };
    if (player.parent().valid() || transform.settings().scale != Vec3(1, 1, 1) ||
        transform.settings().rotation.x() != 0.0 || transform.settings().rotation.z() != 0.0)
    {
        return fail();
    }
    const auto& settings = controller.settings();
    double yaw = transform.settings().rotation.y();
    if (settings.camera_node_id != 0)
    {
        auto camera_node = player.scene()->find_local_node(settings.camera_node_id);
        if (!camera_node)
        {
            stop_driving();
            motor.clear_input();
            return true;
        }
        auto camera = camera_node.get_component<Camera>();
        auto rig = static_cast<bool>(camera_node) ? camera_node.get_component<CharacterCamera>()
                                                  : CharacterCamera{};
        if (!camera || !camera.active() || !rig || !rig.active() || camera_node == player ||
            camera_node.parent().valid() || !detail::active(camera_node) ||
            rig.settings().target_node_id != player.info().properties->id ||
            !rig.synchronize_angles())
        {
            return fail();
        }
        if (input.toggle_view)
        {
            auto view = rig.settings();
            view.mode = view.mode == schema::CharacterView::first_person
                            ? schema::CharacterView::third_person
                            : schema::CharacterView::first_person;
            if (!rig.set_settings(view))
            {
                return fail();
            }
        }
        if (!rig.set_angles(rig.yaw() - input.look.x() * settings.look_sensitivity,
                            rig.pitch() - input.look.y() * settings.look_sensitivity))
        {
            return fail();
        }
        yaw = rig.yaw();
    }
    else
    {
        yaw = std::remainder(yaw - input.look.x() * settings.look_sensitivity, 360.0);
    }
    const double radians = yaw * std::numbers::pi / 180.0;
    const double length = std::max(1.0, std::hypot(input.move.x(), input.move.y()));
    const double right = input.move.x() / length * settings.move_speed;
    const double forward = input.move.y() / length * settings.move_speed;
    const Vec3 velocity(std::cos(radians) * right - std::sin(radians) * forward, 0,
                        -std::sin(radians) * right - std::cos(radians) * forward);
    auto pose = transform.settings();
    pose.rotation = {0, yaw, 0};
    if (!transform.set_settings(pose) || !motor.set_velocity(velocity) ||
        (input.jump && motor.grounded() && !motor.jump(settings.jump_speed)))
    {
        return fail();
    }
    driving_scene_ = player.scene()->id();
    driving_node_ = player.id();
    driving_controller_ = controller.instance_id();
    driving_motor_ = motor.instance_id();
    engine::ecs_detail::component_runtime(controller)->driven_motor_ = driving_motor_;
    return true;
}

void CharacterControllerSystem::on_shutdown() noexcept
{
    stop_driving();
    discard_component_inputs();
    explicit_player_ = false;
    selected_scene_ = 0;
    selected_node_ = 0;
    selected_controller_ = 0;
    input_ = {};
    has_input_ = false;
}

} // namespace engine::plugins::character_controller
