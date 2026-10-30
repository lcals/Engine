#pragma once

#include "runtime/scene/component.hpp"

#include <memory>
#include <string_view>

namespace engine
{

class PhysicsSystem;
class Resource;

class Collider final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::ColliderData;
    [[nodiscard]] static bool valid_settings(const schema::ColliderDataT& settings) noexcept;
    [[nodiscard]] const schema::ColliderDataT& settings() const noexcept;
    [[nodiscard]] bool set_settings(const schema::ColliderDataT& settings);
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;
    // 选择静态三角网格；失败保留原有设置和资源引用。
    [[nodiscard]] bool set_mesh(std::string_view path);
    [[nodiscard]] std::string_view mesh_path() const noexcept;

private:
    friend class Node;
    friend class PhysicsSystem;
    explicit Collider(Node& node);
    std::shared_ptr<const Resource> mesh_;
};

} // namespace engine
