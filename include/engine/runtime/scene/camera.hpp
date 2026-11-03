#pragma once

#include "runtime/scene/component.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace engine
{

struct CameraRuntime
{
    std::vector<std::uint64_t> hidden_meshes_;
};

class Camera final : public Component
{
public:
    using Data = schema::CameraDataT;
    Camera() noexcept = default;
    explicit Camera(Component component) noexcept : Component(component, component_type)
    {
    }
    [[nodiscard]] static bool register_type(ComponentRegistry& registry);
    using Runtime = CameraRuntime;
    [[nodiscard]] Runtime* runtime() const noexcept;
    static constexpr ComponentType component_type = ComponentType::CameraData;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);

    [[nodiscard]] CameraSettings settings() const noexcept;
    [[nodiscard]] bool get_settings(Data& output) const noexcept;
    [[nodiscard]] bool set_settings(const CameraSettings& settings) noexcept;
    // 左上原点；点与视口尺寸使用同一单位。失败清零两个输出。
    [[nodiscard]] bool screen_point_to_ray(const Vec2& point, const Vec2& viewport_size,
                                           Vec3& origin, Vec3& direction) const noexcept;
    // 左上 (0,0)、右下 (1,1)，允许有限外推；近裁面起射，世界方向归一化。
    [[nodiscard]] bool viewport_point_to_ray(const Vec2& point, double aspect, Vec3& origin,
                                             Vec3& direction) const noexcept;
    // 本镜头的瞬态隐藏列表，使用组件实例身份；非法 ID 保留原列表，不改变网格状态。
    [[nodiscard]] bool set_hidden_meshes(std::span<const std::uint64_t> mesh_instance_ids);
    void clear_hidden_meshes() noexcept;
    [[nodiscard]] bool mesh_visible(std::uint64_t mesh_instance_id) const noexcept;
};

} // namespace engine
