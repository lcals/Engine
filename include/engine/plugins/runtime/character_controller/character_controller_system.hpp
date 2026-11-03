#pragma once
#include "runtime/scene/node.hpp"

#include "plugins/runtime/character_controller/character_input.hpp"
#include "runtime/system/system.hpp"

#include <cstdint>

namespace engine
{
class WorldSystem;
class Node;
} // namespace engine

namespace engine::plugins::character_controller
{

class CharacterControllerSystem final : public System
{
public:
    // 世界借用须覆盖本系统生命周期，输入仅由调用方明确提交。
    explicit CharacterControllerSystem(WorldSystem& world) noexcept;
    ~CharacterControllerSystem() noexcept override;
    // 选中已发布世界中的玩家；空指针解除选择，不自动接管其他玩家。
    [[nodiscard]] bool set_player(Node node) noexcept;
    // 覆盖下一帧输入，消费一次；不持续保留上一次按键。
    [[nodiscard]] bool set_input(const CharacterInput& input) noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    void discard_component_inputs() noexcept;
    void stop_driving() noexcept;
    WorldSystem& world_;
    CharacterInput input_{};
    bool has_input_ = false;
    bool explicit_player_ = false;
    std::uint64_t selected_scene_ = 0;
    std::uint64_t selected_node_ = 0;
    std::uint64_t selected_controller_ = 0;
    std::uint64_t driving_scene_ = 0;
    std::uint64_t driving_node_ = 0;
    std::uint64_t driving_controller_ = 0;
    std::uint64_t driving_motor_ = 0;
};

} // namespace engine::plugins::character_controller
