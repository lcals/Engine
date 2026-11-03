#pragma once

#include "runtime/scene/component.hpp"

namespace engine
{

class PostProcessComponent final : public Component
{
public:
    using Data = schema::PostProcessDataT;
    PostProcessComponent() noexcept = default;
    explicit PostProcessComponent(Component component) noexcept
        : Component(component, component_type)
    {
    }
    [[nodiscard]] static bool register_type(ComponentRegistry& registry);
    static constexpr ComponentType component_type = ComponentType::PostProcessData;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);

    [[nodiscard]] PostProcessSettings settings() const noexcept;
    [[nodiscard]] bool get_settings(Data& output) const noexcept;
    [[nodiscard]] bool set_settings(const PostProcessSettings& settings) noexcept;
};

} // namespace engine
