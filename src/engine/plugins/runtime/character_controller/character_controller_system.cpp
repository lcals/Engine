#include "plugins/runtime/character_controller/character_controller_system.hpp"

#include "plugins/runtime/character_controller/character_controller.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/character_motor.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>

namespace engine::plugins::character_controller
{
namespace
{

bool valid_input(const Vec2& value) noexcept
{
    return std::isfinite(value.x()) && std::isfinite(value.y()) &&
           std::abs(value.x()) <= 1000000.0 && std::abs(value.y()) <= 1000000.0;
}

bool active(const Node& node)
{
    for (const auto* current = &node; current != nullptr; current = current->parent())
    {
        const auto* transform = current->get_component<Transform>();
        if (transform == nullptr || !transform->enabled())
        {
            return false;
        }
    }
    return true;
}

} // namespace

CharacterControllerSystem::CharacterControllerSystem(WorldSystem& world, PhysicsSystem& physics,
                                                     WindowSystem* window) noexcept
    : world_(world), physics_(physics), window_(window)
{
}

CharacterControllerSystem::~CharacterControllerSystem() noexcept
{
    shutdown();
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

bool CharacterControllerSystem::resolve_camera(CharacterController& controller, Node*& camera)
{
    camera = nullptr;
    const auto& node = controller.node();
    const auto* transform = node.get_component<Transform>();
    const auto* motor = node.get_component<CharacterMotor>();
    if (node.parent() != nullptr || transform == nullptr || motor == nullptr || !motor->enabled() ||
        transform->settings().scale != Vec3(1, 1, 1) || transform->settings().rotation.x() != 0.0 ||
        transform->settings().rotation.z() != 0.0)
    {
        return false;
    }
    for (const auto& candidate : world_.scene()->nodes())
    {
        if (candidate->info().id == controller.settings().camera_node_id)
        {
            camera = candidate.get();
            break;
        }
    }
    if (camera == nullptr || camera == &node || camera->parent() != nullptr || !active(*camera))
    {
        return false;
    }
    const auto* component = camera->get_component<Camera>();
    if (component == nullptr || !component->enabled())
    {
        return false;
    }
    if (!controller.view_initialized_)
    {
        controller.yaw_ = std::remainder(transform->settings().rotation.y(), 360.0);
        controller.pitch_ = camera->get_component<Transform>()->settings().rotation.x();
        controller.view_initialized_ = true;
    }
    controller.pitch_ =
        std::clamp(controller.pitch_, static_cast<double>(controller.settings().pitch_min),
                   static_cast<double>(controller.settings().pitch_max));
    return true;
}

bool CharacterControllerSystem::on_tick(double)
{
    auto input = input_;
    if (!has_input_ && window_ != nullptr)
    {
        input.move = {static_cast<double>(window_->key_down(WindowKey::d)) -
                          static_cast<double>(window_->key_down(WindowKey::a)),
                      static_cast<double>(window_->key_down(WindowKey::w)) -
                          static_cast<double>(window_->key_down(WindowKey::s))};
        input.look =
            window_->pointer_down(WindowPointerButton::right) ? window_->pointer_delta() : Vec2{};
        input.jump = window_->key_pressed(WindowKey::space);
        input.toggle_view = window_->key_pressed(WindowKey::tab);
    }
    input_ = {};
    has_input_ = false;
    auto* scene = world_.scene();
    if (scene == nullptr)
    {
        restore_visibility();
        return true;
    }
    const double length = std::max(1.0, std::hypot(input.move.x(), input.move.y()));
    std::set<std::uint64_t> cameras;
    for (const auto& node : scene->nodes())
    {
        auto* controller = node->get_component<CharacterController>();
        if (controller == nullptr)
        {
            continue;
        }
        auto* motor = node->get_component<CharacterMotor>();
        if (!controller->enabled() || !active(*node))
        {
            if (controller->driving_ && motor != nullptr && motor->enabled() &&
                !motor->set_velocity({}))
            {
                return false;
            }
            controller->driving_ = false;
            continue;
        }
        Node* camera = nullptr;
        if (!resolve_camera(*controller, camera) || !cameras.insert(camera->id()).second)
        {
            return false;
        }
        if (input.toggle_view)
        {
            auto settings = controller->settings();
            settings.mode = settings.mode == schema::CharacterView::first_person
                                ? schema::CharacterView::third_person
                                : schema::CharacterView::first_person;
            if (!controller->set_settings(settings))
            {
                return false;
            }
        }
        const auto& settings = controller->settings();
        controller->yaw_ =
            std::remainder(controller->yaw_ - input.look.x() * settings.look_sensitivity, 360.0);
        controller->pitch_ = std::clamp(
            controller->pitch_ - input.look.y() * settings.look_sensitivity,
            static_cast<double>(settings.pitch_min), static_cast<double>(settings.pitch_max));
        const double yaw = controller->yaw_ * std::numbers::pi / 180.0;
        const double right = input.move.x() / length * settings.move_speed;
        const double forward = input.move.y() / length * settings.move_speed;
        const Vec3 velocity(std::cos(yaw) * right - std::sin(yaw) * forward, 0,
                            -std::sin(yaw) * right - std::cos(yaw) * forward);
        auto* transform = node->get_component<Transform>();
        auto pose = transform->settings();
        pose.rotation = {0, controller->yaw_, 0};
        if (!transform->set_settings(pose) || !motor->set_velocity(velocity) ||
            (input.jump && motor->grounded() && !motor->jump(settings.jump_speed)))
        {
            return false;
        }
        controller->driving_ = true;
    }
    return update_cameras();
}

bool CharacterControllerSystem::update_cameras()
{
    auto* scene = world_.scene();
    if (scene == nullptr)
    {
        restore_visibility();
        return true;
    }
    std::set<std::uint64_t> cameras;
    for (const auto& node : scene->nodes())
    {
        auto* controller = node->get_component<CharacterController>();
        if (controller == nullptr || !controller->enabled() || !active(*node))
        {
            continue;
        }
        Node* camera = nullptr;
        if (!resolve_camera(*controller, camera) || !cameras.insert(camera->id()).second)
        {
            return false;
        }
        const auto& settings = controller->settings();
        const auto& foot = node->get_component<Transform>()->settings().position;
        const Vec3 pivot(foot.x(), foot.y() + settings.eye_height, foot.z());
        Vec3 position = pivot;
        if (settings.mode == schema::CharacterView::third_person)
        {
            const double yaw = controller->yaw_ * std::numbers::pi / 180.0;
            const double pitch = controller->pitch_ * std::numbers::pi / 180.0;
            const Vec3 displacement(std::sin(yaw) * std::cos(pitch) * settings.camera_distance,
                                    -std::sin(pitch) * settings.camera_distance,
                                    std::cos(yaw) * std::cos(pitch) * settings.camera_distance);
            float fraction = 0.0F;
            if (!physics_.cast_sphere(pivot, displacement, 0.15F, fraction))
            {
                return false;
            }
            const double safe_fraction =
                std::max(0.0, static_cast<double>(fraction) -
                                  (fraction < 1.0F ? 0.02 / settings.camera_distance : 0.0));
            position = {pivot.x() + displacement.x() * safe_fraction,
                        pivot.y() + displacement.y() * safe_fraction,
                        pivot.z() + displacement.z() * safe_fraction};
        }
        auto* transform = camera->get_component<Transform>();
        auto pose = transform->settings();
        pose.position = position;
        pose.rotation = {controller->pitch_, controller->yaw_, 0};
        if (!transform->set_settings(pose))
        {
            return false;
        }
    }
    update_visibility();
    return true;
}

void CharacterControllerSystem::update_visibility()
{
    auto* scene = world_.scene();
    if (scene == nullptr || scene->id() != visibility_scene_id_)
    {
        restore_visibility();
        visibility_scene_id_ = scene != nullptr ? scene->id() : 0;
    }
    if (scene == nullptr)
    {
        return;
    }
    std::set<std::uint64_t> hidden_instances;
    for (const auto& node : scene->nodes())
    {
        auto* renderer = node->get_component<MeshRenderer>();
        if (renderer == nullptr)
        {
            continue;
        }
        bool hide = false;
        for (const Node* ancestor = node.get(); ancestor != nullptr; ancestor = ancestor->parent())
        {
            const auto* controller = ancestor->get_component<CharacterController>();
            if (controller != nullptr && controller->enabled() && active(*ancestor) &&
                controller->settings().mode == schema::CharacterView::first_person)
            {
                hide = true;
                break;
            }
        }
        if (!hide)
        {
            continue;
        }
        hidden_instances.insert(renderer->mesh_instance_id());
        const auto saved = std::find_if(
            hidden_meshes_.begin(), hidden_meshes_.end(), [renderer](const HiddenMesh& entry)
            { return entry.instance_id == renderer->mesh_instance_id(); });
        if (saved == hidden_meshes_.end())
        {
            hidden_meshes_.push_back(
                {node->id(), renderer->mesh_instance_id(), renderer->visible()});
        }
        renderer->set_visible(false);
    }
    for (auto entry = hidden_meshes_.begin(); entry != hidden_meshes_.end();)
    {
        if (hidden_instances.contains(entry->instance_id))
        {
            ++entry;
            continue;
        }
        auto* node = scene->find_node(entry->node_id);
        auto* renderer = node != nullptr ? node->get_component<MeshRenderer>() : nullptr;
        if (renderer != nullptr && renderer->mesh_instance_id() == entry->instance_id)
        {
            renderer->set_visible(entry->visible);
        }
        entry = hidden_meshes_.erase(entry);
    }
}

void CharacterControllerSystem::restore_visibility() noexcept
{
    auto* scene = world_.scene();
    if (scene != nullptr && scene->id() == visibility_scene_id_)
    {
        for (const auto& entry : hidden_meshes_)
        {
            auto* node = scene->find_node(entry.node_id);
            auto* renderer = node != nullptr ? node->get_component<MeshRenderer>() : nullptr;
            if (renderer != nullptr && renderer->mesh_instance_id() == entry.instance_id)
            {
                renderer->set_visible(entry.visible);
            }
        }
    }
    hidden_meshes_.clear();
    visibility_scene_id_ = 0;
}

void CharacterControllerSystem::on_shutdown() noexcept
{
    restore_visibility();
    if (auto* scene = world_.scene())
    {
        for (const auto& node : scene->nodes())
        {
            auto* controller = node->get_component<CharacterController>();
            auto* motor = node->get_component<CharacterMotor>();
            if (controller != nullptr && controller->driving_ && motor != nullptr &&
                motor->enabled())
            {
                (void)motor->set_velocity({});
                controller->driving_ = false;
            }
        }
    }
    input_ = {};
    has_input_ = false;
}

} // namespace engine::plugins::character_controller
