#include "runtime/task/thread_priority.hpp"

namespace engine::task_detail
{
namespace
{
thread_local int worker_identity;
}

bool apply_current_thread_priority(ThreadPriority priority, ThreadPriorityState& state,
                                   std::string& error)
{
    error.clear();
    if (state.owner != nullptr || priority != ThreadPriority::normal)
    {
        error = "Browser workers require normal OS thread priority";
        return false;
    }
    state = {};
    state.owner = &worker_identity;
    return true;
}

bool restore_current_thread_priority(ThreadPriorityState& state, std::string& error)
{
    error.clear();
    if (state.owner != nullptr && state.owner != &worker_identity)
    {
        error = "Thread priority must be restored on its original worker";
        return false;
    }
    state = {};
    return true;
}
} // namespace engine::task_detail
