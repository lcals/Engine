#pragma once

#include "runtime/scene/scene_types.hpp"
#include "runtime/system/system.hpp"

#include <cstddef>
#include <memory>

namespace engine
{

class WorldSystem;

class PhysicsSystem final : public System
{
public:
    // 可空借用；世界须存活到系统关闭。空世界用于独立初始化与链接验证。
    explicit PhysicsSystem(WorldSystem* world = nullptr) noexcept;
    ~PhysicsSystem() noexcept override;

    void clear() noexcept;
    [[nodiscard]] std::size_t body_count() const noexcept;
    [[nodiscard]] std::size_t character_count() const noexcept;
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
