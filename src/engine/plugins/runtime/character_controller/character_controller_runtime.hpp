#pragma once
#include "plugins/runtime/character_controller/character_ability_registry.hpp"
#include "plugins/runtime/character_controller/character_controller.hpp"
#include "runtime/scene/component_runtime.hpp"
namespace engine::plugins::character_controller
{
struct CharacterWorldRuntime
{
    const CharacterAbilityRegistry* abilities = nullptr;
};
inline const CharacterAbilityRegistry* character_abilities(WorldSystem& world)
{
    const auto* r = flecs::world(world.native_world()).try_get<CharacterWorldRuntime>();
    return r ? r->abilities : nullptr;
}
struct CharacterControllerRuntime
{
    CharacterInput pending_input{}, sampled_input{};
    bool has_input = false;
    std::uint64_t sequence = 0;
    std::uint64_t last_motor_instance = 0;
    std::vector<CharacterActionState> actions;
    Vec3 desired_velocity{}, candidate_velocity{};
    double view_yaw = 0;
    bool camera_resolved = false;
    Node camera_node{};
    CharacterStatus status{};
    CharacterMotorStepContext context{};
    schema::CharacterMotorDataT step_settings{};
    Vec3 move_direction{};
    float move_amount = 0;
    std::vector<CharacterAbilityDecision> decisions;
    std::vector<CharacterAbilityRequest> requests;
    std::vector<std::size_t> ordered_abilities;
    bool prepared = false;
};
} // namespace engine::plugins::character_controller
