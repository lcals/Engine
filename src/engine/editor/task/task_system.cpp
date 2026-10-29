#include "editor/task/task_system.hpp"

#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <queue>

namespace engine
{

struct TaskSystem::Impl
{
    std::unique_ptr<tf::Executor> executor;
    std::atomic<std::size_t> thread_count{4};
    std::mutex mutex;
    bool running = false;
    std::string error;
};

TaskSystem::TaskSystem() : impl_(std::make_unique<Impl>())
{
}

TaskSystem::~TaskSystem() noexcept
{
    shutdown();
}

bool TaskSystem::on_initialize(const EngineConfig&)
{
    impl_->executor = std::make_unique<tf::Executor>(impl_->thread_count.load());
    return true;
}

void TaskSystem::on_shutdown() noexcept
{
    if (impl_->executor != nullptr)
    {
        impl_->executor->wait_for_all();
        impl_->executor.reset();
    }
}

bool TaskSystem::configure_threads(std::size_t count)
{
    const std::lock_guard lock(impl_->mutex);
    if (!is_initialized() || impl_->running || count == 0 || count > 64)
    {
        return false;
    }
    if (count != impl_->thread_count.load())
    {
        impl_->executor = std::make_unique<tf::Executor>(count);
        impl_->thread_count.store(count);
    }
    return true;
}

bool TaskSystem::run(const std::vector<TaskDescription>& tasks)
{
    {
        const std::lock_guard lock(impl_->mutex);
        if (!is_initialized() || impl_->running)
        {
            return false;
        }
        impl_->running = true;
        impl_->error.clear();
    }
    const auto finish = [this](bool success)
    {
        const std::lock_guard lock(impl_->mutex);
        impl_->running = false;
        return success;
    };
    std::vector<std::size_t> incoming(tasks.size(), 0);
    std::vector<std::vector<std::size_t>> dependents(tasks.size());
    for (std::size_t index = 0; index < tasks.size(); ++index)
    {
        if (!tasks[index].execute)
        {
            impl_->error = "task has no function: " + tasks[index].name;
            return finish(false);
        }
        std::vector<std::size_t> seen;
        for (const auto dependency : tasks[index].dependencies)
        {
            if (dependency >= tasks.size() || dependency == index ||
                std::find(seen.begin(), seen.end(), dependency) != seen.end())
            {
                impl_->error = "invalid task dependency: " + tasks[index].name;
                return finish(false);
            }
            seen.push_back(dependency);
            ++incoming[index];
            dependents[dependency].push_back(index);
        }
    }
    std::queue<std::size_t> pending;
    for (std::size_t index = 0; index < tasks.size(); ++index)
    {
        if (incoming[index] == 0)
        {
            pending.push(index);
        }
    }
    std::size_t visited = 0;
    while (!pending.empty())
    {
        const auto index = pending.front();
        pending.pop();
        ++visited;
        for (const auto dependent : dependents[index])
        {
            if (--incoming[dependent] == 0)
            {
                pending.push(dependent);
            }
        }
    }
    if (visited != tasks.size())
    {
        impl_->error = "task dependency cycle";
        return finish(false);
    }

    tf::Taskflow graph;
    std::vector<tf::Task> handles;
    handles.reserve(tasks.size());
    // 每个槽只有自己的任务写入；Taskflow 依赖边提供前后任务的同步。
    std::vector<unsigned char> success(tasks.size(), 0);
    for (std::size_t index = 0; index < tasks.size(); ++index)
    {
        handles.push_back(graph
                              .emplace(
                                  [&, index]
                                  {
                                      for (const auto dependency : tasks[index].dependencies)
                                      {
                                          if (success[dependency] == 0)
                                          {
                                              return;
                                          }
                                      }
                                      success[index] = tasks[index].execute() ? 1 : 0;
                                  })
                              .name(tasks[index].name));
    }
    for (std::size_t index = 0; index < tasks.size(); ++index)
    {
        for (const auto dependency : tasks[index].dependencies)
        {
            handles[dependency].precede(handles[index]);
        }
    }
    impl_->executor->run(graph).wait();
    for (std::size_t index = 0; index < tasks.size(); ++index)
    {
        if (success[index] == 0)
        {
            impl_->error = "task failed or dependency failed: " + tasks[index].name;
            return finish(false);
        }
    }
    return finish(true);
}

std::size_t TaskSystem::thread_count() const noexcept
{
    return impl_->thread_count.load();
}

const std::string& TaskSystem::last_error() const noexcept
{
    return impl_->error;
}

} // namespace engine
