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
    // 客户端按进程存活，具体领域在各自实现中提交按需采样。
    tracy::SetThreadName("Engine main");
    static_cast<void>(TracyIsConnected);
#endif
    return true;
}

void PerformanceSystem::on_shutdown() noexcept
{
}

} // namespace engine
