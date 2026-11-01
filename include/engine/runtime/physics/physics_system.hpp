#pragma once

#include "runtime/physics/physics_impact.hpp"
#include "runtime/physics/physics_query.hpp"
#include "runtime/system/system.hpp"

#include <cstddef>
#include <memory>
#include <span>

namespace engine
{

class WorldSystem;
class Collider;
struct MeshCollisionData;
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
    // 复制完整槽位列表并共享不可变网格；复用未变分段，全部成功后一次发布。
    // 空列表/全部空槽移除碰撞；调用安全点、静态限制与 set_mesh_data 相同。
    [[nodiscard]] bool set_mesh_parts(Collider& collider, const MeshCollisionData& mesh);
    // 查询当前碰撞世界；失败输出为零，成功未命中输出为一；忽略触发器。
    [[nodiscard]] bool cast_sphere(const Vec3& origin, const Vec3& displacement, float radius,
                                   float& fraction) const;
    // 仅查询当前世界指定 Collider；无命中（含禁用/sensor）true/1，错误 false/0。
    // 世界起点和完整位移；读取已提交碰撞，不推进模拟，与物理步进不并发。
    [[nodiscard]] bool cast_ray(const Collider& collider, const Vec3& origin,
                                const Vec3& displacement, float& fraction) const;
    // 世界起点与非零完整位移；只读当前场景已提交碰撞，须在主线程步进外调用。
    // false 为无效查询；true 且 !hit.hit 为无命中。两者输出均为空。
    [[nodiscard]] bool query_ray(const Vec3& origin, const Vec3& displacement,
                                 const PhysicsQueryFilter& filter, PhysicsQueryHit& hit) const;
    [[nodiscard]] bool query_sphere(const Vec3& origin, const Vec3& displacement, float radius,
                                    const PhysicsQueryFilter& filter, PhysicsQueryHit& hit) const;
    // 只检查场景、节点、Collider 实例及启用；位置/形状变化不重算历史命中。
    [[nodiscard]] bool is_hit_valid(const PhysicsQueryHit& hit) const noexcept;
    // 主线程步进外非消费借用；下一 tick（含零步）、clear 或无效节点清理后失效。
    // 删除/禁用的节点不返回；frame 在系统生命周期内单调递增。
    [[nodiscard]] std::span<const PhysicsImpact> impacts() const noexcept;

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
