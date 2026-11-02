#include "runtime/task/thread_priority.hpp"

#include <pthread.h>
#include <pthread/qos.h>

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

    qos_class_t original_qos = QOS_CLASS_UNSPECIFIED;
    int original_relative = 0;
    const int query_result =
        pthread_get_qos_class_np(pthread_self(), &original_qos, &original_relative);
    if (query_result != 0)
    {
        error = "pthread_get_qos_class_np failed: " + std::to_string(query_result);
        return false;
    }
    // setter 不接受 UNSPECIFIED，无法恢复时不能修改原 QoS。
    if (original_qos == QOS_CLASS_UNSPECIFIED)
    {
        error = "Cannot temporarily change an unspecified thread QoS";
        return false;
    }
    const qos_class_t desired_qos =
        priority == ThreadPriority::low ? QOS_CLASS_BACKGROUND : QOS_CLASS_USER_INITIATED;
    if (original_qos != desired_qos || original_relative != 0)
    {
        const int apply_result = pthread_set_qos_class_self_np(desired_qos, 0);
        if (apply_result != 0)
        {
            error = "pthread_set_qos_class_self_np failed: " + std::to_string(apply_result);
            return false;
        }
        state.policy_changed = true;
    }
    state.original_policy = static_cast<int>(original_qos);
    state.original_priority = original_relative;
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
        const int result = pthread_set_qos_class_self_np(
            static_cast<qos_class_t>(state.original_policy), state.original_priority);
        if (result != 0)
        {
            failed_restore = state;
            error = "Thread QoS restoration failed; worker retains modified QoS: " +
                    std::to_string(result);
            return false;
        }
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
