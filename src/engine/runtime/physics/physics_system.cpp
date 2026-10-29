#include "runtime/physics/physics_system.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>

namespace engine
{

PhysicsSystem::PhysicsSystem() noexcept = default;

PhysicsSystem::~PhysicsSystem() noexcept
{
    shutdown();
}

bool PhysicsSystem::on_initialize(const EngineConfig&)
{
    // 本阶段确认物理库 ABI；碰撞世界和类型注册随物理功能一起启用。
    return JPH::VerifyJoltVersionID();
}

void PhysicsSystem::on_shutdown() noexcept
{
}

} // namespace engine
