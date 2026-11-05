#pragma once

#include "plugins/runtime/character_controller/character_input.hpp"
#include "runtime/scene/component.hpp"

#include <cstdint>

namespace engine::plugins::character_controller
{

struct CharacterControllerRuntime;

class CharacterController final : public Component
{
public:
    using Data = schema::CharacterControllerDataT;
    CharacterController() noexcept = default;
    explicit CharacterController(Component component) noexcept
        : Component(component, component_type)
    {
    }
    [[nodiscard]] static bool register_type(ComponentRegistry& registry);
    using Runtime = CharacterControllerRuntime;
    static constexpr ComponentType component_type = ComponentType::CharacterControllerData;
    [[nodiscard]] static bool
    valid_settings(const schema::CharacterControllerDataT& settings) noexcept;
    [[nodiscard]] schema::CharacterControllerDataT settings() const noexcept;
    [[nodiscard]] bool get_settings(Data& output) const noexcept;
    [[nodiscard]] bool set_settings(const schema::CharacterControllerDataT& settings) noexcept;
    // 瞬态意图仅供下次控制器更新消费；不进入组件持久数据。
    [[nodiscard]] bool set_input(const CharacterInput& input) noexcept;
    // 完整取消自身意图及最近实际驱动的同实例 Motor 主动输入；保留外力。
    void clear_input() noexcept;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);
    [[nodiscard]] bool remap_node_references(schema::ComponentDataT& data,
                                             std::span<const schema::NodeIdMappingT> mapping) const;

private:
    template <class T> friend auto engine::ecs_detail::component_runtime(T component) noexcept;
    [[nodiscard]] Runtime* runtime() const noexcept;
};

} // namespace engine::plugins::character_controller
