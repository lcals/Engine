#pragma once

#include "runtime/scene/component.hpp"
#include "runtime/scene/material.hpp"

namespace engine
{

class MeshRenderer final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::MeshRendererData;

    [[nodiscard]] MeshKind mesh() const noexcept;
    [[nodiscard]] Material& material() noexcept;
    [[nodiscard]] const Material& material() const noexcept;

private:
    friend class Node;
    explicit MeshRenderer(Node& node);
    [[nodiscard]] bool initialize() override;

    Material material_;
};

} // namespace engine
