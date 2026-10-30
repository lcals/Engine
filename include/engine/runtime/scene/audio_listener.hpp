#pragma once

#include "runtime/scene/component.hpp"

namespace engine
{

class AudioListener final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::AudioListenerData;
    [[nodiscard]] static bool valid_settings(const schema::AudioListenerDataT& settings) noexcept;
    [[nodiscard]] const schema::AudioListenerDataT& settings() const noexcept;
    [[nodiscard]] bool set_settings(const schema::AudioListenerDataT& settings) noexcept;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;

private:
    friend class Node;
    explicit AudioListener(Node& node);
};

} // namespace engine
