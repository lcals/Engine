#pragma once
#include "runtime/physics/character_motor_driver.hpp"
#include "runtime/system/system.hpp"
namespace engine::plugins::character_controller
{
class CharacterControllerSystem final : public System, public CharacterMotorDriver
{
public:
    explicit CharacterControllerSystem(WorldSystem& world) noexcept;
    ~CharacterControllerSystem() noexcept override;
    [[nodiscard]] Component control_owner(CharacterMotor motor) const override;
    [[nodiscard]] bool prepare_step(Component owner, CharacterMotor motor,
                                    const CharacterMotorStepContext& context,
                                    CharacterMotorCommand& output) override;
    void finish_step(Component owner, CharacterMotor motor,
                     const CharacterMotorStepResult& result) noexcept override;
    void reset(Component owner, CharacterMotor motor) noexcept override;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    WorldSystem& world_;
};
} // namespace engine::plugins::character_controller
