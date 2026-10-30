#pragma once

#include "runtime/scene/component.hpp"

namespace engine
{

class PostProcessComponent final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::PostProcessData;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;

    [[nodiscard]] const PostProcessSettings& settings() const noexcept;
    [[nodiscard]] bool set_settings(const PostProcessSettings& settings) noexcept;

private:
    friend class Node;
    explicit PostProcessComponent(Node& node);
};

} // namespace engine
