#pragma once

#include "runtime/scene/component.hpp"

namespace engine
{

class Collider final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::ColliderData;
    [[nodiscard]] static bool valid_settings(const schema::ColliderDataT& settings) noexcept;
    [[nodiscard]] const schema::ColliderDataT& settings() const noexcept;
    [[nodiscard]] bool set_settings(const schema::ColliderDataT& settings) noexcept;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;

private:
    friend class Node;
    explicit Collider(Node& node);
};

} // namespace engine
