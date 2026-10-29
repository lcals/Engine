#pragma once

#include "runtime/scene/scene_types.hpp"

namespace engine
{

class Node;

class Component
{
public:
    virtual ~Component() noexcept = 0;

    Component(const Component&) = delete;
    Component& operator=(const Component&) = delete;
    Component(Component&&) = delete;
    Component& operator=(Component&&) = delete;

    [[nodiscard]] Node& node() noexcept;
    [[nodiscard]] const Node& node() const noexcept;
    [[nodiscard]] ComponentType type() const noexcept;
    [[nodiscard]] bool enabled() const noexcept;
    void set_enabled(bool enabled) noexcept;

protected:
    Component(Node& node, ComponentType type) noexcept;

private:
    Node& node_;
    ComponentType type_;
    bool enabled_ = true;
};

class Camera final : public Component
{
public:
    [[nodiscard]] const CameraSettings& settings() const noexcept;
    [[nodiscard]] bool set_settings(const CameraSettings& settings) noexcept;

private:
    friend class Node;
    explicit Camera(Node& node) noexcept;
    CameraSettings settings_{};
};

class Light final : public Component
{
public:
    [[nodiscard]] const LightSettings& settings() const noexcept;
    [[nodiscard]] bool set_settings(const LightSettings& settings) noexcept;

private:
    friend class Node;
    explicit Light(Node& node) noexcept;
    LightSettings settings_{};
};

class MeshRenderer final : public Component
{
public:
    [[nodiscard]] MeshKind mesh() const noexcept;
    [[nodiscard]] const Material& material() const noexcept;
    [[nodiscard]] bool set_material(const Material& material) noexcept;

private:
    friend class Node;
    explicit MeshRenderer(Node& node) noexcept;
    Material material_{};
};

} // namespace engine
