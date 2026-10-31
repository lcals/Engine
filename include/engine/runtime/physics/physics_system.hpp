#pragma once

#include "runtime/scene/scene_types.hpp"
#include "runtime/system/system.hpp"

#include <cstddef>
#include <memory>

namespace engine
{

class WorldSystem;
class Collider;
namespace schema
{
struct MeshDataT;
struct ColliderDataT;
} // namespace schema

class PhysicsSystem final : public System
{
public:
    // 可空借用；世界须存活到系统关闭。空世界用于独立初始化与链接验证。
    explicit PhysicsSystem(WorldSystem* world = nullptr) noexcept;
    ~PhysicsSystem() noexcept override;

    void clear() noexcept;
    [[nodiscard]] std::size_t body_count() const noexcept;
    [[nodiscard]] std::size_t character_count() const noexcept;
    // 同一安全点准备并提交通用碰撞描述；失败保留旧组件及碰撞，不推进模拟。
    // 复合凸包仅支持正等比世界缩放；调用约束与 set_mesh_data 相同。
    // 禁用时只验证描述与变换、移除已有碰撞；不构建形状，重新启用时再准备。
    [[nodiscard]] bool set_collider_data(Collider& collider, const schema::ColliderDataT& settings);
    // 主线程安全点调用；复制并准备静态网格，成功后提交，失败保留旧几何与碰撞。
    // 顶点和索引均空时移除旧碰撞；保留组件启用状态，后续非空网格可恢复。
    // collider 必须属于当前世界；不与物理步进、查询或场景修改并发。
    [[nodiscard]] bool set_mesh_data(Collider& collider, const schema::MeshDataT& mesh);
    // 查询当前碰撞世界；失败输出为零，成功未命中输出为一；忽略触发器。
    [[nodiscard]] bool cast_sphere(const Vec3& origin, const Vec3& displacement, float radius,
                                   float& fraction) const;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    struct State;
    WorldSystem* world_ = nullptr;
    std::unique_ptr<State> state_;
};

} // namespace engine
