#pragma once
#include "plugins/runtime/camera/camera_controller.hpp"
#include "runtime/system/system.hpp"
namespace engine::plugins::character_controller
{
class CharacterCameraParameterSystem final : public System
{
public:
    explicit CharacterCameraParameterSystem(WorldSystem& world) noexcept;
    ~CharacterCameraParameterSystem() noexcept override;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig&) override;
    [[nodiscard]] bool on_tick(double) override;
    void on_shutdown() noexcept override;

private:
    WorldSystem& world_;
    // Identity-qualified destinations for cleanup after an adapter is removed.
    std::vector<std::pair<camera::CameraController, std::vector<std::string>>> written_;
};
} // namespace engine::plugins::character_controller
