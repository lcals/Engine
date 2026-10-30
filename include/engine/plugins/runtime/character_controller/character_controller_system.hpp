#pragma once

#include "plugins/runtime/character_controller/character_input.hpp"
#include "runtime/system/system.hpp"

#include <cstdint>
#include <vector>

namespace engine
{
class WorldSystem;
class PhysicsSystem;
class WindowSystem;
class Node;
} // namespace engine

namespace engine::plugins::character_controller
{

class CharacterController;

class CharacterControllerSystem final : public System
{
public:
    // 借用对象须覆盖本系统生命周期；可空窗口允许业务直接提交输入。
    CharacterControllerSystem(WorldSystem& world, PhysicsSystem& physics,
                              WindowSystem* window = nullptr) noexcept;
    ~CharacterControllerSystem() noexcept override;
    // 覆盖下一帧输入，消费一次；不持续保留上一次按键。
    [[nodiscard]] bool set_input(const CharacterInput& input) noexcept;
    [[nodiscard]] bool update_cameras();

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    [[nodiscard]] bool resolve_camera(CharacterController& controller, Node*& camera);
    void update_visibility();
    void restore_visibility() noexcept;
    struct HiddenMesh
    {
        std::uint64_t node_id = 0;
        std::uint64_t instance_id = 0;
        bool visible = true;
    };
    std::vector<HiddenMesh> hidden_meshes_;
    std::uint64_t visibility_scene_id_ = 0;
    WorldSystem& world_;
    PhysicsSystem& physics_;
    WindowSystem* window_ = nullptr;
    CharacterInput input_{};
    bool has_input_ = false;
};

} // namespace engine::plugins::character_controller
