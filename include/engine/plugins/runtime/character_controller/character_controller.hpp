#pragma once
#include "plugins/runtime/character_controller/character_types.hpp"
#include "runtime/scene/component.hpp"
namespace engine::plugins::character_controller
{
class CharacterAbilityRegistry;
struct CharacterControllerRuntime;
class CharacterController final : public Component
{
public:
    using Data = schema::CharacterControllerDataT;
    using Runtime = CharacterControllerRuntime;
    static constexpr ComponentType component_type = ComponentType::CharacterControllerData;
    CharacterController() noexcept = default;
    explicit CharacterController(Component c) noexcept : Component(c, component_type)
    {
    }
    [[nodiscard]] static bool register_type(ComponentRegistry& registry);
    [[nodiscard]] bool initialize();
    [[nodiscard]] static bool valid_settings(const Data&, const CharacterAbilityRegistry&,
                                             std::string& error);
    [[nodiscard]] bool get_settings(Data& output) const;
    [[nodiscard]] bool set_settings(const Data&, std::string& error);
    [[nodiscard]] bool set_input(const CharacterInput& input);
    [[nodiscard]] bool clear_input() noexcept;
    [[nodiscard]] bool get_status(CharacterStatus& output) const;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);
    [[nodiscard]] bool remap_node_references(schema::ComponentDataT& data,
                                             std::span<const schema::NodeIdMappingT> mapping) const;

private:
    template <class T> friend auto engine::ecs_detail::component_runtime(T component) noexcept;
    [[nodiscard]] Runtime* runtime() const noexcept;
};
} // namespace engine::plugins::character_controller
