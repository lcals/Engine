#pragma once

#include "runtime/scene/component.hpp"
#include "runtime/scene/material.hpp"

#include <memory>
#include <string_view>

namespace engine
{

class Resource;

class MeshRenderer final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::MeshRendererData;

    [[nodiscard]] std::string_view mesh_path() const noexcept;
    [[nodiscard]] bool set_mesh(std::string_view path);
    [[nodiscard]] Material& material() noexcept;
    [[nodiscard]] const Material& material() const noexcept;

private:
    friend class Node;
    explicit MeshRenderer(Node& node);
    [[nodiscard]] bool initialize() override;

    std::shared_ptr<const Resource> mesh_resource_;
    Material material_;
};

} // namespace engine
