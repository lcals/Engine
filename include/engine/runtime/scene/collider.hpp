#pragma once

#include "runtime/scene/component.hpp"

#include <cstdint>
#include <memory>
#include <string_view>

namespace engine
{

class PhysicsSystem;
class Resource;
struct MeshCollisionData;
namespace schema
{
struct MeshDataT;
}

struct ColliderRuntime;

class Collider final : public Component
{
public:
    using Data = schema::ColliderDataT;
    Collider() noexcept = default;
    explicit Collider(Component component) noexcept : Component(component, component_type)
    {
    }
    [[nodiscard]] static bool register_type(ComponentRegistry& registry);
    using Runtime = ColliderRuntime;
    static constexpr ComponentType component_type = ComponentType::ColliderData;
    // 默认校验持久描述；瞬时网格仅在调用方持有相应几何时允许空 GUID。
    [[nodiscard]] static bool
    valid_settings(const schema::ColliderDataT& settings,
                   const schema::MeshDataT* transient_mesh = nullptr,
                   const MeshCollisionData* mesh_parts = nullptr) noexcept;
    [[nodiscard]] schema::ColliderDataT settings() const noexcept;
    [[nodiscard]] bool get_settings(Data& output) const noexcept;
    [[nodiscard]] bool set_settings(const schema::ColliderDataT& settings);
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);
    [[nodiscard]] bool build_data(schema::ComponentDataT& output) const;
    // 借用当前资源或瞬时网格；分段模式返回 nullptr。设置替换或组件销毁后失效。
    [[nodiscard]] const schema::MeshDataT* mesh_data() const noexcept;
    // 选择静态三角网格；失败保留原有设置和资源引用。
    [[nodiscard]] bool set_mesh(std::string_view path);
    [[nodiscard]] std::string_view mesh_path() const noexcept;

private:
    template <class T> friend auto engine::ecs_detail::component_runtime(T component) noexcept;
    [[nodiscard]] Runtime* runtime() const noexcept;
    friend class PhysicsSystem;
};

} // namespace engine
