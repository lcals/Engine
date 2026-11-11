#pragma once

#include "runtime/physics/physics_object.hpp"
#include "runtime/scene/character_motor.hpp"

namespace engine
{
enum class CharacterGroundState
{
    supported,
    steep,
    unsupported,
    airborne
};
enum class CharacterShapeResult
{
    unchanged,
    applied,
    blocked
};

struct CharacterMotorStatus
{
    std::uint64_t step = 0;
    double simulation_time = 0;
    bool has_simulated = false;
    Vec3 feet_position{};
    float yaw_degrees = 0;
    Vec3 velocity{};
    CharacterGroundState ground_state = CharacterGroundState::airborne;
    bool grounded = false;
    PhysicsObject support{};
    Vec3 support_normal{};
    Vec3 support_velocity{};
    Vec3 carried_planar_velocity{};
    Vec3 external_planar_velocity{};
    float active_half_height = 0;
    CharacterShapeResult shape_result = CharacterShapeResult::unchanged;
    bool control_error = false;
};

struct CharacterMotorStepContext
{
    std::uint64_t step = 0;
    double delta_time = 0;
    double simulation_time = 0;
    const schema::CharacterMotorDataT* settings = nullptr;
    CharacterMotorStatus before{};
};

struct CharacterMotorCommand
{
    Vec3 planar_velocity{};
    float yaw_degrees = 0;
    float vertical_impulse = 0;
    float gravity_scale = 1;
    float half_height = 0;
    bool detach_support = false;
    bool inherit_support_planar_velocity = false;
};

struct CharacterMotorStepResult
{
    CharacterMotorStatus status{};
    bool impulse_applied = false;
};

class CharacterMotorDriver
{
public:
    virtual ~CharacterMotorDriver() noexcept = default;
    [[nodiscard]] virtual Component control_owner(CharacterMotor motor) const = 0;
    [[nodiscard]] virtual bool prepare_step(Component owner, CharacterMotor motor,
                                            const CharacterMotorStepContext& context,
                                            CharacterMotorCommand& output) = 0;
    virtual void finish_step(Component owner, CharacterMotor motor,
                             const CharacterMotorStepResult& result) noexcept = 0;
    virtual void reset(Component owner, CharacterMotor motor) noexcept = 0;
};
} // namespace engine
