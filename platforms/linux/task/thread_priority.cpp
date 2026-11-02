#include "runtime/task/thread_priority.hpp"

#include <cerrno>
#include <sched.h>
#include <sys/resource.h>

namespace engine::task_detail
{
namespace
{
thread_local ThreadPriorityState failed_restore;
} // namespace

bool apply_current_thread_priority(ThreadPriority priority, ThreadPriorityState& state,
                                   std::string& error)
{
    error.clear();
    if (state.owner != nullptr)
    {
        error = "Thread priority state is already active";
        return false;
    }
    if (priority != ThreadPriority::low && priority != ThreadPriority::normal &&
        priority != ThreadPriority::high)
    {
        error = "Invalid thread priority";
        return false;
    }
    if (failed_restore.owner != nullptr && !restore_current_thread_priority(failed_restore, error))
    {
        return false;
    }
    state = {};
    if (priority == ThreadPriority::normal)
    {
        state.owner = &failed_restore;
        return true;
    }

    const int original_policy = sched_getscheduler(0);
    if (original_policy < 0)
    {
        const int result = errno;
        error = "sched_getscheduler failed: " + std::to_string(result);
        return false;
    }
    if (original_policy != SCHED_OTHER && original_policy != SCHED_BATCH)
    {
        error = "Temporary priority changes require SCHED_OTHER or SCHED_BATCH";
        return false;
    }
    errno = 0;
    const int original_priority = getpriority(PRIO_PROCESS, 0);
    if (original_priority == -1 && errno != 0)
    {
        const int result = errno;
        error = "getpriority failed: " + std::to_string(result);
        return false;
    }

    if (priority == ThreadPriority::low && original_policy != SCHED_BATCH)
    {
        // 公平批处理策略可无特权恢复；直接调高 nice 可能无法无特权调回。
        const sched_param parameters{};
        if (sched_setscheduler(0, SCHED_BATCH, &parameters) != 0)
        {
            const int result = errno;
            error = "sched_setscheduler(SCHED_BATCH) failed: " + std::to_string(result);
            return false;
        }
        state.policy_changed = true;
    }
    else if (priority == ThreadPriority::high)
    {
        const int desired_priority = original_priority > -15 ? original_priority - 5 : -20;
        if (desired_priority != original_priority)
        {
            if (setpriority(PRIO_PROCESS, 0, desired_priority) != 0)
            {
                const int result = errno;
                error = "setpriority failed: " + std::to_string(result);
                return false;
            }
            state.priority_changed = true;
        }
    }
    state.original_policy = original_policy;
    state.original_priority = original_priority;
    state.owner = &failed_restore;
    return true;
}

bool restore_current_thread_priority(ThreadPriorityState& state, std::string& error)
{
    error.clear();
    if (state.owner == nullptr)
    {
        return true;
    }
    if (state.owner != &failed_restore)
    {
        error = "Thread priority must be restored on its original worker";
        return false;
    }
    if (state.policy_changed)
    {
        const sched_param parameters{};
        if (sched_setscheduler(0, state.original_policy, &parameters) != 0)
        {
            const int result = errno;
            failed_restore = state;
            error = "Thread scheduling restoration failed; worker retains modified policy: " +
                    std::to_string(result);
            return false;
        }
    }
    if (state.priority_changed && setpriority(PRIO_PROCESS, 0, state.original_priority) != 0)
    {
        const int result = errno;
        failed_restore = state;
        error = "Thread nice restoration failed; worker retains modified nice: " +
                std::to_string(result);
        return false;
    }
    if (state.policy_changed || state.priority_changed)
    {
        failed_restore = {};
    }
    state = {};
    if (failed_restore.owner != nullptr)
    {
        error = "Worker still has a pending thread priority restoration";
        return false;
    }
    return true;
}
} // namespace engine::task_detail
