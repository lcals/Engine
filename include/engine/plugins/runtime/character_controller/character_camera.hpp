#pragma once

#include "runtime/scene/component.hpp"

namespace engine::plugins::character_controller
{

struct CharacterCameraRuntime;

class CharacterCamera final : public Component
{
public:
    using Data = schema::CharacterCameraDataT;
    CharacterCamera() noexcept = default;
    explicit CharacterCamera(Component component) noexcept : Component(component, component_type)
    {
    }
    [[nodiscard]] static bool register_type(ComponentRegistry& registry);
    using Runtime = CharacterCameraRuntime;
    static constexpr ComponentType component_type = ComponentType::CharacterCameraData;
    [[nodiscard]] static bool valid_settings(const schema::CharacterCameraDataT& settings) noexcept;
    [[nodiscard]] schema::CharacterCameraDataT settings() const noexcept;
    [[nodiscard]] bool get_settings(Data& output) const noexcept;
    [[nodiscard]] bool set_settings(const schema::CharacterCameraDataT& settings) noexcept;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);
    [[nodiscard]] bool remap_node_references(schema::ComponentDataT& data,
                                             std::span<const schema::NodeIdMappingT> mapping) const;
    // 首次从本相机 Transform 读取角度；随后由调用方显式设置。
    [[nodiscard]] bool synchronize_angles() noexcept;
    [[nodiscard]] bool set_angles(double yaw, double pitch) noexcept;
    [[nodiscard]] double yaw() const noexcept;
    [[nodiscard]] double pitch() const noexcept;

private:
    template <class T> friend auto engine::ecs_detail::component_runtime(T component) noexcept;
    [[nodiscard]] Runtime* runtime() const noexcept;
};

} // namespace engine::plugins::character_controller
