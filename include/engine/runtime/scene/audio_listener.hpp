#pragma once

#include "runtime/scene/component.hpp"

namespace engine
{

class AudioListener final : public Component
{
public:
    using Data = schema::AudioListenerDataT;
    AudioListener() noexcept = default;
    explicit AudioListener(Component component) noexcept : Component(component, component_type)
    {
    }
    [[nodiscard]] static bool register_type(ComponentRegistry& registry);
    static constexpr ComponentType component_type = ComponentType::AudioListenerData;
    [[nodiscard]] static bool valid_settings(const schema::AudioListenerDataT& settings) noexcept;
    [[nodiscard]] bool get_settings(Data& output) const noexcept;
    [[nodiscard]] bool set_settings(const schema::AudioListenerDataT& settings) noexcept;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);
};

} // namespace engine
