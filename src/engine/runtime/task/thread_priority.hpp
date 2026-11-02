#pragma once

#include "runtime/task/thread_system.hpp"

#include <string>

namespace engine::task_detail
{
// 应用成功后必须在同一 OS worker 恢复；恢复失败保留状态供下一项任务先重试。
struct ThreadPriorityState
{
    const void* owner = nullptr;
    int original_policy = 0;
    int original_priority = 0;
    bool policy_changed = false;
    bool priority_changed = false;
};

[[nodiscard]] bool apply_current_thread_priority(ThreadPriority priority,
                                                 ThreadPriorityState& state, std::string& error);
[[nodiscard]] bool restore_current_thread_priority(ThreadPriorityState& state, std::string& error);
} // namespace engine::task_detail
