#include "runtime/task/task_system.hpp"

#include "runtime/performance/profiling.hpp"
#include "runtime/task/thread_priority.hpp"
#include "runtime/task/thread_system.hpp"

#include <oneapi/tbb/flow_graph.h>
#include <oneapi/tbb/task_arena.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace engine
{
namespace
{

namespace tbb = oneapi::tbb;
using Clock = std::chrono::steady_clock;

// 标记引擎回调；生命周期与同步等待不允许从共享 worker 反向进入。
thread_local bool in_task = false;

bool valid_priority(TaskPriority priority)
{
    return priority == TaskPriority::low || priority == TaskPriority::normal ||
           priority == TaskPriority::high;
}

tbb::task_arena::priority arena_priority(TaskPriority priority)
{
    switch (priority)
    {
    case TaskPriority::low:
        return tbb::task_arena::priority::low;
    case TaskPriority::high:
        return tbb::task_arena::priority::high;
    default:
        return tbb::task_arena::priority::normal;
    }
}

tbb::flow::node_priority_t node_priority(TaskPriority priority)
{
    // 所有三档均为非零原生优先度；0 表示不参与图的优先队列。
    return static_cast<tbb::flow::node_priority_t>(priority) + 1;
}

bool validate_tasks(const std::vector<TaskDescription>& tasks, std::string& error)
{
    std::vector<std::size_t> incoming(tasks.size());
    std::vector<std::vector<std::size_t>> dependents(tasks.size());
    std::vector<std::size_t> pending;
    for (std::size_t index = 0; index < tasks.size(); ++index)
    {
        const auto& task = tasks[index];
        if (!task.execute || !valid_priority(task.priority))
        {
            error = "task has no function or invalid priority: " + task.name;
            return false;
        }
        std::vector<std::size_t> seen;
        for (const auto dependency : task.dependencies)
        {
            if (dependency >= tasks.size() || dependency == index ||
                std::find(seen.begin(), seen.end(), dependency) != seen.end())
            {
                error = "invalid task dependency: " + task.name;
                return false;
            }
            seen.push_back(dependency);
            ++incoming[index];
            dependents[dependency].push_back(index);
        }
        if (incoming[index] == 0)
        {
            pending.push_back(index);
        }
    }
    // 这里只校验 DAG；执行及依赖唤醒交给 oneTBB。
    for (std::size_t position = 0; position < pending.size(); ++position)
    {
        for (const auto dependent : dependents[pending[position]])
        {
            if (--incoming[dependent] == 0)
            {
                pending.push_back(dependent);
            }
        }
    }
    if (pending.size() != tasks.size())
    {
        error = "task dependency cycle";
        return false;
    }
    return true;
}

} // namespace

struct TaskSystem::Impl
{
    struct Submission
    {
        std::function<bool()> execute;
        std::promise<bool> completion;
        std::function<void(bool)> callback;
    };
    struct Completion
    {
        std::function<void(bool)> callback;
        bool success = false;
    };
    using Entry = tbb::flow::function_node<std::shared_ptr<Submission>, tbb::flow::continue_msg>;

    ThreadSystem& threads;
    TaskConfig config;
    std::unique_ptr<tbb::task_arena> arena;
    std::unique_ptr<tbb::flow::graph> graph;
    std::array<std::unique_ptr<Entry>, 3> entries;
    std::mutex mutex;
    std::condition_variable completed;
    std::size_t pending = 0;
    std::size_t outstanding = 0;
    std::deque<Completion> ready;
    bool dispatching = false;
    bool accepting = false;
    std::thread::id owner = std::this_thread::get_id();
    ThreadPriority thread_priority = ThreadPriority::normal;
    std::string error;

    Impl(ThreadSystem& budget, const TaskConfig& settings) : threads(budget), config(settings)
    {
    }

    bool on_owner() const
    {
        return !in_task && owner == std::this_thread::get_id();
    }

    void create_arena()
    {
        arena = std::make_unique<tbb::task_arena>(static_cast<int>(config.max_concurrency), 0,
                                                  arena_priority(config.priority));
        arena->initialize();
        // 在目标 arena 内构造，graph 才会将后续 try_put 路由到该域。
        arena->execute(
            [this]
            {
                graph = std::make_unique<tbb::flow::graph>();
                for (std::size_t index = 0; index < entries.size(); ++index)
                {
                    // 原生队列限制同时进入的批次体，避免协作等待无限嵌套批次栈。
                    entries[index] = std::make_unique<Entry>(
                        *graph, config.max_concurrency,
                        [this](const std::shared_ptr<Submission>& submission)
                        {
                            const bool success = submission->execute();
                            // future 就绪前释放捕获数据；调度节点本身仍由 drain 收尾。
                            submission->execute = {};
                            {
                                const std::lock_guard lock(mutex);
                                --pending;
                                if (submission->callback)
                                {
                                    ready.push_back({std::move(submission->callback), success});
                                }
                                else
                                {
                                    --outstanding;
                                }
                            }
                            submission->completion.set_value(success);
                            completed.notify_one();
                            return tbb::flow::continue_msg{};
                        },
                        static_cast<tbb::flow::node_priority_t>(index + 1));
                }
            });
    }

    void drain()
    {
        ENGINE_PROFILE_ZONE("Task.wait");
        {
            std::unique_lock lock(mutex);
            completed.wait(lock, [this] { return pending == 0; });
        }
        // pending=0 表示用户函数已结束，不代表 TBB 节点已返回。
        // 只在这里做图的尾部回收，主线程不会代跑业务。
        if (graph != nullptr)
        {
            graph->wait_for_all();
        }
    }

    void release_arena()
    {
        for (auto& entry : entries)
        {
            entry.reset();
        }
        graph.reset();
        arena.reset();
    }

    bool enqueue(const std::shared_ptr<Submission>& submission, TaskPriority priority)
    {
        // Hold through try_put: shutdown/configuration must not destroy the graph between
        // accepting a request and handing it to oneTBB. No caller waits for worker execution.
        const std::lock_guard lock(mutex);
        if (!accepting || outstanding >= config.max_pending)
        {
            return false;
        }
        ++pending;
        ++outstanding;
        if (!entries[static_cast<std::size_t>(priority)]->try_put(submission))
        {
            --pending;
            --outstanding;
            return false;
        }
        return true;
    }

    std::future<bool> submit(std::function<bool()> execute, TaskPriority priority)
    {
        auto submission = std::make_shared<Submission>();
        submission->execute = std::move(execute);
        auto future = submission->completion.get_future();
        if (!enqueue(submission, priority))
        {
            error = "task domain is closed or its pending budget is exhausted";
            return {};
        }
        return future;
    }

    bool execute_task(const std::string& name, const std::function<bool()>& execute,
                      TaskPriority priority, Clock::time_point submitted)
    {
        ENGINE_PROFILE_ZONE("Task.execute");
#if defined(ENGINE_ENABLE_TRACY)
        thread_local const bool named = []
        {
            tracy::SetThreadName("Engine TBB worker");
            return true;
        }();
        static_cast<void>(named);
        if (ZoneIsActive)
        {
            ZoneName(name.data(), name.size());
            const char* label = priority == TaskPriority::high  ? "high"
                                : priority == TaskPriority::low ? "low"
                                                                : "normal";
            const auto queued_ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - submitted)
                    .count();
            std::string details = "domain=" + config.name + "; task=" + name +
                                  "; priority=" + label +
                                  "; queued_ns=" + std::to_string(queued_ns);
            // 多次 ZoneText/ZoneValue 会插入换行，官方 csvexport 不转义它；
            // 保持一条安全注释，使已有离线分析流程也能读取这些埋点。
            std::replace_if(
                details.begin(), details.end(),
                [](char value) { return value == '\t' || value == '\r' || value == '\n'; }, ' ');
            ZoneText(details.data(), details.size());
        }
#else
        static_cast<void>(name);
        static_cast<void>(priority);
        static_cast<void>(submitted);
#endif
        task_detail::ThreadPriorityState previous;
        std::string priority_error;
        const bool priority_applied =
            task_detail::apply_current_thread_priority(thread_priority, previous, priority_error);
        if (!priority_applied)
        {
            // Accepted work must finish even when the OS denies a scheduling hint.
            // In particular, dropping a physics job would leave its barrier waiting forever.
            ENGINE_PROFILE_MESSAGE(priority_error.data(), priority_error.size());
        }
        const bool was_in_task = in_task;
        in_task = true;
        const bool success = execute();
        in_task = was_in_task;
        if (priority_applied &&
            !task_detail::restore_current_thread_priority(previous, priority_error))
        {
            ENGINE_PROFILE_MESSAGE(priority_error.data(), priority_error.size());
            return false;
        }
        return success && priority_applied;
    }

    bool execute_batch(const std::vector<TaskDescription>& tasks, Clock::time_point submitted)
    {
        ENGINE_PROFILE_ZONE("Task.batch");
        tbb::flow::graph batch;
        using Node = tbb::flow::continue_node<tbb::flow::continue_msg>;
        std::vector<std::unique_ptr<Node>> nodes;
        // 每槽只有一个写入者；依赖边提供相邻任务间的同步。
        std::vector<unsigned char> success(tasks.size());
        nodes.reserve(tasks.size());
        for (std::size_t index = 0; index < tasks.size(); ++index)
        {
            nodes.push_back(std::make_unique<Node>(
                batch,
                [&, index](const tbb::flow::continue_msg&)
                {
                    const auto& task = tasks[index];
                    for (const auto dependency : task.dependencies)
                    {
                        if (success[dependency] == 0)
                        {
                            return tbb::flow::continue_msg{};
                        }
                    }
                    success[index] =
                        execute_task(task.name, task.execute, task.priority, submitted) ? 1 : 0;
                    return tbb::flow::continue_msg{};
                },
                node_priority(tasks[index].priority)));
        }
        for (std::size_t index = 0; index < tasks.size(); ++index)
        {
            for (const auto dependency : tasks[index].dependencies)
            {
                tbb::flow::make_edge(*nodes[dependency], *nodes[index]);
            }
        }
        for (std::size_t index = 0; index < tasks.size(); ++index)
        {
            if (tasks[index].dependencies.empty())
            {
                static_cast<void>(nodes[index]->try_put(tbb::flow::continue_msg{}));
            }
        }
        // 官方协作等待：此 worker 可推进图，而不是阻塞等待同池 future。
        batch.wait_for_all();
        return std::all_of(success.begin(), success.end(), [](auto value) { return value != 0; });
    }
};

TaskSystem::TaskSystem(ThreadSystem& threads, const TaskConfig& config)
    : impl_(std::make_unique<Impl>(threads, config))
{
}

TaskSystem::~TaskSystem() noexcept
{
    shutdown();
}

bool TaskSystem::on_initialize(const EngineConfig&)
{
    impl_->error.clear();
    if (!impl_->threads.is_initialized() || impl_->config.name.empty() ||
        impl_->config.max_concurrency == 0 || impl_->config.max_concurrency > 64 ||
        impl_->config.max_pending == 0 ||
        !valid_priority(impl_->config.priority))
    {
        impl_->error = "invalid task domain configuration or uninitialized thread budget";
        return false;
    }
    impl_->owner = std::this_thread::get_id();
    impl_->thread_priority = impl_->threads.worker_priority();
    impl_->create_arena();
    impl_->accepting = true;
    return true;
}

void TaskSystem::on_shutdown() noexcept
{
    ENGINE_PROFILE_ZONE("Task.shutdown");
    {
        const std::lock_guard lock(impl_->mutex);
        impl_->accepting = false;
    }
    impl_->drain();
    impl_->release_arena();
    // Destroy captures outside the mutex; no application callbacks run during shutdown.
    std::deque<Impl::Completion> discarded;
    {
        const std::lock_guard lock(impl_->mutex);
        discarded.swap(impl_->ready);
        impl_->outstanding = 0;
    }
}

bool TaskSystem::configure_concurrency(std::size_t count)
{
    if (!impl_->on_owner())
    {
        return false;
    }
    impl_->error.clear();
    {
        const std::lock_guard lock(impl_->mutex);
        if (!impl_->accepting || impl_->pending != 0 || count == 0 || count > 64)
        {
            impl_->error = "invalid concurrency or task domain is not idle";
            return false;
        }
        impl_->accepting = false;
    }
    if (count != impl_->config.max_concurrency)
    {
        impl_->drain();
        impl_->release_arena();
        impl_->config.max_concurrency = count;
        impl_->create_arena();
    }
    {
        const std::lock_guard lock(impl_->mutex);
        impl_->accepting = true;
    }
    return true;
}

std::future<bool> TaskSystem::submit(std::string name, std::function<bool()> task,
                                     TaskPriority priority)
{
    if (!impl_->on_owner())
    {
        return {};
    }
    impl_->error.clear();
    if (!task || !valid_priority(priority))
    {
        impl_->error = "task has no function or invalid priority: " + name;
        return {};
    }
    return impl_->submit(
        [this, name = std::move(name), task = std::move(task), priority, submitted = Clock::now()]
        { return impl_->execute_task(name, task, priority, submitted); }, priority);
}

std::future<bool> TaskSystem::submit_batch(std::vector<TaskDescription> tasks,
                                           TaskPriority priority)
{
    if (!impl_->on_owner())
    {
        return {};
    }
    impl_->error.clear();
    if (!impl_->accepting || !valid_priority(priority))
    {
        impl_->error = "task domain is not accepting work or invalid batch priority";
        return {};
    }
    if (!validate_tasks(tasks, impl_->error))
    {
        return {};
    }
    if (tasks.empty())
    {
        std::promise<bool> ready;
        ready.set_value(true);
        return ready.get_future();
    }
    return impl_->submit([this, tasks = std::move(tasks), submitted = Clock::now()]
                          { return impl_->execute_batch(tasks, submitted); }, priority);
}

bool TaskSystem::submit(std::string name, std::function<bool()> task,
                        std::function<void(bool)> completed, TaskPriority priority)
{
    if (!impl_->on_owner())
    {
        return false;
    }
    impl_->error.clear();
    if (!task || !completed || !valid_priority(priority))
    {
        impl_->error = "task requires work, completion and a valid priority";
        return false;
    }
    auto submission = std::make_shared<Impl::Submission>();
    submission->callback = std::move(completed);
    submission->execute =
        [this, name = std::move(name), task = std::move(task), priority, submitted = Clock::now()]
        { return impl_->execute_task(name, task, priority, submitted); };
    if (!impl_->enqueue(submission, priority))
    {
        impl_->error = "task domain is closed or its pending budget is exhausted";
        return false;
    }
    return true;
}

bool TaskSystem::enqueue(std::string name, std::function<void()> task, TaskPriority priority)
{
    if (!task || !valid_priority(priority))
    {
        return false;
    }
    auto submission = std::make_shared<Impl::Submission>();
    submission->execute =
        [this, name = std::move(name), task = std::move(task), priority, submitted = Clock::now()]
        {
            return impl_->execute_task(name, [&] { task(); return true; }, priority, submitted);
        };
    return impl_->enqueue(submission, priority);
}

std::size_t TaskSystem::dispatch_completions(std::size_t max_count, double max_seconds)
{
    if (!impl_->on_owner() || impl_->dispatching || max_count == 0 ||
        !std::isfinite(max_seconds) || max_seconds <= 0.0)
    {
        return 0;
    }
    ENGINE_PROFILE_ZONE("Task.complete");
    std::size_t available = 0;
    {
        const std::lock_guard lock(impl_->mutex);
        if (!impl_->accepting)
        {
            return 0;
        }
        available = std::min(max_count, impl_->ready.size());
    }
    const auto started = Clock::now();
    impl_->dispatching = true;
    std::size_t count = 0;
    while (count < available &&
           std::chrono::duration<double>(Clock::now() - started).count() < max_seconds)
    {
        Impl::Completion completion;
        {
            const std::lock_guard lock(impl_->mutex);
            if (!impl_->accepting || impl_->ready.empty())
            {
                break;
            }
            completion = std::move(impl_->ready.front());
            impl_->ready.pop_front();
            --impl_->outstanding;
        }
        completion.callback(completion.success);
        ++count;
    }
    impl_->dispatching = false;
    return count;
}

bool TaskSystem::run(const std::vector<TaskDescription>& tasks)
{
    auto future = submit_batch(tasks);
    if (!future.valid())
    {
        return false;
    }
    if (!future.get())
    {
        impl_->error = "task batch failed (task, dependency or worker priority)";
        return false;
    }
    return true;
}

bool TaskSystem::wait()
{
    if (!impl_->on_owner() || !impl_->accepting)
    {
        return false;
    }
    impl_->drain();
    return true;
}

std::size_t TaskSystem::max_concurrency() const noexcept
{
    return impl_->config.max_concurrency;
}

const std::string& TaskSystem::last_error() const noexcept
{
    return impl_->error;
}

} // namespace engine
