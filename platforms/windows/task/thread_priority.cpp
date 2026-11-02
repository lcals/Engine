#include "runtime/task/thread_priority.hpp"

#include <windows.h>

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

    const HANDLE thread = GetCurrentThread();
    const int original_priority = GetThreadPriority(thread);
    if (original_priority == THREAD_PRIORITY_ERROR_RETURN)
    {
        const DWORD result = GetLastError();
        error = "GetThreadPriority failed: " + std::to_string(result);
        return false;
    }
    const int desired_priority = priority == ThreadPriority::low ? THREAD_PRIORITY_BELOW_NORMAL
                                                                 : THREAD_PRIORITY_ABOVE_NORMAL;
    if (original_priority != desired_priority)
    {
        if (SetThreadPriority(thread, desired_priority) == 0)
        {
            const DWORD result = GetLastError();
            error = "SetThreadPriority failed: " + std::to_string(result);
            return false;
        }
        state.priority_changed = true;
    }
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
    if (state.priority_changed &&
        SetThreadPriority(GetCurrentThread(), state.original_priority) == 0)
    {
        const DWORD result = GetLastError();
        failed_restore = state;
        error = "Thread priority restoration failed; worker retains modified priority: " +
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
