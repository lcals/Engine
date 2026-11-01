#pragma once

#include "plugins/runtime/character_controller/character_input.hpp"
#include "runtime/scene/component.hpp"

#include <cstdint>

namespace engine::plugins::character_controller
{

class CharacterController final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::CharacterControllerData;
    [[nodiscard]] static bool
    valid_settings(const schema::CharacterControllerDataT& settings) noexcept;
    [[nodiscard]] const schema::CharacterControllerDataT& settings() const noexcept;
    [[nodiscard]] bool set_settings(const schema::CharacterControllerDataT& settings) noexcept;
    // 瞬态意图仅供下次控制器更新消费；不进入组件持久数据。
    [[nodiscard]] bool set_input(const CharacterInput& input) noexcept;
    // 完整取消自身意图及最近实际驱动的同实例 Motor 主动输入；保留外力。
    void clear_input() noexcept;
    [[nodiscard]] std::uint64_t instance_id() const noexcept;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;

private:
    friend class engine::Node;
    friend class CharacterControllerSystem;
    explicit CharacterController(Node& node);
    [[nodiscard]] bool initialize() override;
    const std::uint64_t instance_id_;
    std::uint64_t driven_motor_ = 0;
    CharacterInput input_{};
};

} // namespace engine::plugins::character_controller
