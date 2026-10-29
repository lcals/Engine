#pragma once

#include "runtime/scene/component.hpp"

namespace engine
{

class Camera final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::CameraData;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;

    [[nodiscard]] const CameraSettings& settings() const noexcept;
    [[nodiscard]] bool set_settings(const CameraSettings& settings) noexcept;

private:
    friend class Node;
    explicit Camera(Node& node);
};

} // namespace engine
