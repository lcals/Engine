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
    // 校验 Jolt 的链接与 ABI 配置；本系统不创建物理世界。
    return JPH::VerifyJoltVersionID();
}

void PhysicsSystem::on_shutdown() noexcept
{
}

} // namespace engine
