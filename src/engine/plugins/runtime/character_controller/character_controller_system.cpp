#include "plugins/runtime/character_controller/character_controller_system.hpp"

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
    for (const auto& scene : world_.scenes())
    {
        for (const auto& node : scene->nodes())
        {
            if (auto* controller = node->get_component<CharacterController>())
            {
                controller->input_ = {};
            }
        }
    }
}

CharacterControllerSystem::CharacterControllerSystem(WorldSystem& world) noexcept : world_(world)
{
}

CharacterControllerSystem::~CharacterControllerSystem() noexcept
{
    shutdown();
}

bool CharacterControllerSystem::set_player(Node* node) noexcept
{
    auto* controller = node != nullptr ? node->get_component<CharacterController>() : nullptr;
    if (node != nullptr && (world_.find_node(node->id()) != node || controller == nullptr))
    {
        return false;
    }
    stop_driving();
    explicit_player_ = true;
    selected_scene_ = node != nullptr ? node->scene().id() : 0;
    selected_node_ = node != nullptr ? node->id() : 0;
    selected_controller_ = controller != nullptr ? controller->instance_id() : 0;
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
    auto* node = world_.find_node(driving_node_);
    if (node && node->scene().id() != driving_scene_)
    {
        node = nullptr;
    }
    auto* motor = node != nullptr ? node->get_component<CharacterMotor>() : nullptr;
    if (motor != nullptr && motor->instance_id() == driving_motor_)
    {
        motor->clear_input();
    }
    auto* controller = node != nullptr ? node->get_component<CharacterController>() : nullptr;
    if (controller != nullptr && controller->instance_id() == driving_controller_)
    {
        controller->clear_input();
    }
    driving_scene_ = 0;
    driving_node_ = 0;
    driving_controller_ = 0;
    driving_motor_ = 0;
}

bool CharacterControllerSystem::on_tick(double)
{
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
    Node* player = nullptr;
    if (explicit_player_)
    {
        player = world_.find_node(selected_node_);
        if (player && player->scene().id() != selected_scene_)
        {
            player = nullptr;
        }
        const auto* component =
            player != nullptr ? player->get_component<CharacterController>() : nullptr;
        if (component == nullptr || component->instance_id() != selected_controller_)
        {
            player = nullptr;
        }
    }
    else
    {
        for (const auto& scene : world_.scenes())
        {
            for (const auto& node : scene->nodes())
            {
                const auto* component = node->get_component<CharacterController>();
                if (component == nullptr || !component->enabled() || !detail::active(*node))
                {
                    continue;
                }
                if (player != nullptr)
                {
                    discard_component_inputs();
                    stop_driving();
                    return false;
                }
                player = node.get();
            }
        }
    }
    auto* controller = player != nullptr ? player->get_component<CharacterController>() : nullptr;
    auto* motor = player != nullptr ? player->get_component<CharacterMotor>() : nullptr;
    if (!explicit_input && controller != nullptr)
    {
        input = controller->input_;
    }
    discard_component_inputs();
    if (controller == nullptr || !controller->enabled() || !detail::active(*player) ||
        motor == nullptr || !motor->enabled())
    {
        stop_driving();
        return true;
    }
    if (driving_scene_ != player->scene().id() || driving_node_ != player->id() ||
        driving_controller_ != controller->instance_id() || driving_motor_ != motor->instance_id())
    {
        stop_driving();
    }
    auto* transform = player->get_component<Transform>();
    const auto fail = [this, motor]()
    {
        stop_driving();
        motor->clear_input();
        return false;
    };
    if (player->parent() != nullptr || transform->settings().scale != Vec3(1, 1, 1) ||
        transform->settings().rotation.x() != 0.0 || transform->settings().rotation.z() != 0.0)
    {
        return fail();
    }
    const auto& settings = controller->settings();
    double yaw = transform->settings().rotation.y();
    if (settings.camera_node_id != 0)
    {
        auto* camera_node = detail::find_local_node(player->scene(), settings.camera_node_id);
        if (camera_node == nullptr)
        {
            stop_driving();
            motor->clear_input();
            return true;
        }
        auto* camera = camera_node->get_component<Camera>();
        auto* rig =
            camera_node != nullptr ? camera_node->get_component<CharacterCamera>() : nullptr;
        if (camera == nullptr || !camera->enabled() || rig == nullptr || !rig->enabled() ||
            camera_node == player || camera_node->parent() != nullptr ||
            !detail::active(*camera_node) || rig->settings().target_node_id != player->info().id ||
            !rig->synchronize_angles())
        {
            return fail();
        }
        if (input.toggle_view)
        {
            auto view = rig->settings();
            view.mode = view.mode == schema::CharacterView::first_person
                            ? schema::CharacterView::third_person
                            : schema::CharacterView::first_person;
            if (!rig->set_settings(view))
            {
                return fail();
            }
        }
        if (!rig->set_angles(rig->yaw() - input.look.x() * settings.look_sensitivity,
                             rig->pitch() - input.look.y() * settings.look_sensitivity))
        {
            return fail();
        }
        yaw = rig->yaw();
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
    auto pose = transform->settings();
    pose.rotation = {0, yaw, 0};
    if (!transform->set_settings(pose) || !motor->set_velocity(velocity) ||
        (input.jump && motor->grounded() && !motor->jump(settings.jump_speed)))
    {
        return fail();
    }
    driving_scene_ = player->scene().id();
    driving_node_ = player->id();
    driving_controller_ = controller->instance_id();
    driving_motor_ = motor->instance_id();
    controller->driven_motor_ = driving_motor_;
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
