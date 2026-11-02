#include "runtime/task/thread_system.hpp"

#include <oneapi/tbb/global_control.h>
#include <oneapi/tbb/info.h>

#include <algorithm>
#include <atomic>

namespace engine
{
namespace
{

std::atomic<ThreadSystem*> budget_owner{nullptr};

bool valid_priority(ThreadPriority priority)
{
    return priority == ThreadPriority::low || priority == ThreadPriority::normal ||
           priority == ThreadPriority::high;
}

} // namespace

struct ThreadSystem::Impl
{
    ThreadConfig config;
    std::unique_ptr<oneapi::tbb::global_control> control;
    std::size_t workers = 0;
    std::string error;
};

ThreadSystem::ThreadSystem(const ThreadConfig& config) : impl_(std::make_unique<Impl>())
{
    impl_->config = config;
}

ThreadSystem::~ThreadSystem() noexcept
{
    shutdown();
}

bool ThreadSystem::on_initialize(const EngineConfig&)
{
    impl_->error.clear();
    if (impl_->config.worker_limit > 64 || !valid_priority(impl_->config.worker_priority))
    {
        impl_->error = "invalid thread budget or priority";
        return false;
    }
    ThreadSystem* expected = nullptr;
    if (!budget_owner.compare_exchange_strong(expected, this))
    {
        impl_->error = "a process thread budget is already initialized";
        return false;
    }
    impl_->workers = impl_->config.worker_limit;
    if (impl_->workers == 0)
    {
        impl_->workers = static_cast<std::size_t>(
            std::clamp(oneapi::tbb::info::default_concurrency() - 1, 1, 64));
    }
    // oneTBB 全局并行度包含一个调用线程槽；域保留槽设为 0，工作只异步入队。
    impl_->control = std::make_unique<oneapi::tbb::global_control>(
        oneapi::tbb::global_control::max_allowed_parallelism, impl_->workers + 1);
    return true;
}

void ThreadSystem::on_shutdown() noexcept
{
    impl_->control.reset();
    impl_->workers = 0;
    ThreadSystem* expected = this;
    static_cast<void>(budget_owner.compare_exchange_strong(expected, nullptr));
}

std::size_t ThreadSystem::worker_count() const noexcept
{
    return impl_->workers;
}

ThreadPriority ThreadSystem::worker_priority() const noexcept
{
    return impl_->config.worker_priority;
}

const std::string& ThreadSystem::last_error() const noexcept
{
    return impl_->error;
}

} // namespace engine
