#pragma once

#include "runtime/scene/camera.hpp"
#include "runtime/system/system.hpp"

#include <cstdint>
#include <vector>

namespace engine
{
class WorldSystem;
} // namespace engine

namespace engine::plugins::character_controller
{

class CharacterCameraSystem final : public System
{
public:
    // 借用对象须覆盖本系统生命周期；在物理和动画之后更新。
    CharacterCameraSystem(WorldSystem& world) noexcept;
    ~CharacterCameraSystem() noexcept override;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    [[nodiscard]] bool update_visibility();
    void clear_visibility() noexcept;
    WorldSystem& world_;
    std::vector<Camera> filtered_cameras_;
};

} // namespace engine::plugins::character_controller
