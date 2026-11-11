#pragma once
#include "plugins/runtime/character_controller/character_input.hpp"
#include "runtime/physics/character_motor_driver.hpp"
#include <span>
#include <string_view>
namespace engine::plugins::character_controller
{
enum class CharacterControlChannels : std::uint32_t
{
    none = 0,
    speed_policy = 1,
    stance = 2,
    vertical_motion = 4,
    planar_override = 8,
    facing_override = 16
};
constexpr CharacterControlChannels operator|(CharacterControlChannels a, CharacterControlChannels b)
{
    return static_cast<CharacterControlChannels>(static_cast<std::uint32_t>(a) |
                                                 static_cast<std::uint32_t>(b));
}
constexpr bool overlaps(CharacterControlChannels a, CharacterControlChannels b)
{
    return (static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b)) != 0;
}
enum class CharacterAbilitySelection
{
    inactive,
    selected,
    denied,
    failed
};
enum class CharacterMotionState
{
    unknown,
    grounded,
    rising,
    falling
};
enum class CharacterStance
{
    unknown,
    standing,
    crouched
};
struct CharacterActionState
{
    std::string name;
    bool held = false;
    bool has_press = false;
    std::uint64_t press_sequence = 0;
    double press_age = 0;
};
struct CharacterAbilityStatus
{
    std::string id;
    bool active = false;
    std::string phase;
    double remaining_seconds = 0;
    CharacterAbilitySelection selection = CharacterAbilitySelection::inactive;
};
struct CharacterMotionPlan
{
    CharacterControlChannels claimed = CharacterControlChannels::none;
    float max_speed = 0;
    float half_height = 0;
    float vertical_impulse = 0;
    float gravity_scale = 1;
    bool detach_support = false;
    Vec3 planar_override{};
    float yaw_override = 0;
};
struct CharacterAbilityRequest
{
    bool requested = false;
    CharacterControlChannels writes = CharacterControlChannels::none;
    CharacterMotionPlan values{};
    std::string consume_action;
    std::uint64_t press_sequence = 0;
};
struct CharacterAbilityDecision
{
    CharacterAbilitySelection result = CharacterAbilitySelection::inactive;
    CharacterControlChannels granted_channels = CharacterControlChannels::none;
};
struct CharacterAbilityContext
{
    const CharacterMotorStepContext* motor = nullptr;
    Vec3 move_direction{};
    float move_amount = 0;
    std::span<const CharacterActionState> actions;
    [[nodiscard]] bool action(std::string_view name, CharacterActionState& output) const
    {
        output = {};
        for (const auto& value : actions)
        {
            if (value.name == name)
            {
                output = value;
                return true;
            }
        }
        return false;
    }
};
struct CharacterStatus
{
    bool ready = false;
    std::string diagnostic;
    CharacterMotorStatus motor{};
    CharacterMotionState motion = CharacterMotionState::unknown;
    CharacterStance stance = CharacterStance::unknown;
    bool stance_blocked = false;
    Vec3 desired_planar_velocity{};
    float relative_planar_speed = 0;
    std::uint64_t input_sequence = 0;
    std::vector<CharacterActionState> actions;
    std::vector<CharacterAbilityStatus> abilities;
    std::uint64_t jump_sequence = 0;
    std::uint64_t land_sequence = 0;
};
} // namespace engine::plugins::character_controller
