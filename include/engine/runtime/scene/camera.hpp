#pragma once

#include "runtime/scene/component.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace engine
{

class Camera final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::CameraData;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;

    [[nodiscard]] const CameraSettings& settings() const noexcept;
    [[nodiscard]] bool set_settings(const CameraSettings& settings) noexcept;
    // 本镜头的瞬态隐藏列表，使用组件实例身份；非法 ID 保留原列表，不改变网格状态。
    [[nodiscard]] bool set_hidden_meshes(std::span<const std::uint64_t> mesh_instance_ids);
    void clear_hidden_meshes() noexcept;
    [[nodiscard]] bool mesh_visible(std::uint64_t mesh_instance_id) const noexcept;

private:
    friend class Node;
    explicit Camera(Node& node);

    std::vector<std::uint64_t> hidden_meshes_;
};

} // namespace engine
