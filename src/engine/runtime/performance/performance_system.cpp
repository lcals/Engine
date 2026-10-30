#include "runtime/performance/performance_system.hpp"

#if defined(ENGINE_ENABLE_TRACY)
#include <tracy/Tracy.hpp>
#endif

namespace engine
{

PerformanceSystem::PerformanceSystem() noexcept = default;

PerformanceSystem::~PerformanceSystem() noexcept
{
    shutdown();
}

bool PerformanceSystem::on_initialize(const EngineConfig&)
{
#if defined(ENGINE_ENABLE_TRACY)
    // 触达客户端链接实现；当前不提交采样，客户端生命周期由 Tracy 管理。
    static_cast<void>(TracyIsConnected);
#endif
    return true;
}

void PerformanceSystem::on_shutdown() noexcept
{
}

} // namespace engine
