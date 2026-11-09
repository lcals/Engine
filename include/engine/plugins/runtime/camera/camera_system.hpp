#pragma once
#include "plugins/runtime/camera/camera_controller.hpp"
#include "runtime/system/system.hpp"
namespace engine::plugins::camera
{
class CameraSystem final : public System
{
public:
    CameraSystem(WorldSystem& world, PhysicsSystem* physics) noexcept;
    ~CameraSystem() noexcept override;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig&) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    [[nodiscard]] bool update(CameraController, double);
    WorldSystem& world_;
    PhysicsSystem* physics_;
};
} // namespace engine::plugins::camera
