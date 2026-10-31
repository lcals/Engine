#pragma once

#include "runtime/system/system.hpp"

#include <cstdint>
#include <vector>

namespace engine
{
class WorldSystem;
class PhysicsSystem;
} // namespace engine

namespace engine::plugins::character_controller
{

class CharacterCameraSystem final : public System
{
public:
    // 借用对象须覆盖本系统生命周期；在物理和动画之后更新。
    CharacterCameraSystem(WorldSystem& world, PhysicsSystem& physics) noexcept;
    ~CharacterCameraSystem() noexcept override;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    [[nodiscard]] bool update_visibility();
    void clear_visibility() noexcept;
    WorldSystem& world_;
    PhysicsSystem& physics_;
    std::uint64_t visibility_scene_ = 0;
    std::vector<std::uint64_t> filtered_cameras_;
};

} // namespace engine::plugins::character_controller
