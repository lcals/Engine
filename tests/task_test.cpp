#include "runtime/engine_config.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace
{

using namespace std::chrono_literals;

bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "task test: %s\n", message);
    }
    return value;
}

// Gate 及回调借用数据声明在任务域之前，清理时任务先结束。
struct Gate
{
    std::mutex mutex;
    std::condition_variable condition;
    bool opened = false;

    void wait()
    {
        std::unique_lock lock(mutex);
        condition.wait(lock, [this] { return opened; });
    }

    void release()
    {
        const std::lock_guard lock(mutex);
        opened = true;
        condition.notify_all();
    }
};

// 放在任务域之后；早退先放行，随后域析构等待 worker，再销毁 Gate。
struct ReleaseGate
{
    Gate& gate;
    ~ReleaseGate()
    {
        gate.release();
    }
};

bool result(std::future<bool>& future)
{
    return future.valid() && future.wait_for(5s) == std::future_status::ready && future.get();
}

bool lifecycle_and_validation()
{
    const engine::EngineConfig config;
    engine::ThreadSystem invalid({65});
    engine::ThreadSystem invalid_priority({1, static_cast<engine::ThreadPriority>(-1)});
    engine::ThreadSystem threads({2});
    engine::ThreadSystem duplicate({1});
    std::atomic<int> ran{0};
    engine::TaskSystem tasks(threads);
    if (!expect(!invalid.initialize(config) && !invalid_priority.initialize(config),
                "invalid thread settings rejected") ||
        !expect(!tasks.initialize(config) && !tasks.run({}) &&
                    !tasks.submit("not initialized", [] { return true; }).valid(),
                "task domain requires an initialized process budget") ||
        !expect(threads.initialize(config) && threads.worker_count() == 2,
                "initialize process budget") ||
        !expect(!duplicate.initialize(config) && threads.is_initialized(),
                "second budget cannot replace the first") ||
        !expect(tasks.initialize(config) && tasks.max_concurrency() == 4,
                "initialize independent task arena") ||
        !expect(!tasks.configure_concurrency(0) && !tasks.configure_concurrency(65) &&
                    tasks.configure_concurrency(2),
                "idle concurrency configuration"))
    {
        return false;
    }
    auto execute = [&ran]
    {
        ++ran;
        return true;
    };
    if (!expect(!tasks.submit_batch({{"cycle a", execute, {1}}, {"cycle b", execute, {0}}}).valid(),
                "cycle rejected before executing anything") ||
        !expect(!tasks.submit_batch({{"self", execute, {0}}}).valid(), "self edge rejected") ||
        !expect(!tasks.submit_batch({{"missing", execute, {9}}}).valid(),
                "missing dependency rejected") ||
        !expect(
            !tasks.submit_batch({{"root", execute, {}}, {"duplicate", execute, {0, 0}}}).valid(),
            "duplicate dependency rejected") ||
        !expect(!tasks.submit("empty", {}).valid(), "empty callable rejected") ||
        !expect(!tasks.submit("invalid", execute, static_cast<engine::TaskPriority>(9)).valid(),
                "invalid priority rejected") ||
        !expect(ran == 0 && tasks.run({}), "validation has no side effects; empty batch succeeds"))
    {
        return false;
    }
    tasks.shutdown();
    tasks.shutdown();
    if (!expect(!tasks.submit("closed", execute).valid() && tasks.initialize(config),
                "close rejects new work; domain can restart") ||
        !expect(tasks.run({{"restart", execute, {}}}) && tasks.configure_concurrency(1),
                "synchronous run returns after bookkeeping permits reconfiguration"))
    {
        return false;
    }
    tasks.shutdown();
    threads.shutdown();
    if (!expect(threads.worker_count() == 0 && duplicate.initialize(config),
                "budget released only by its owner"))
    {
        return false;
    }
    return true;
}

bool priority_and_async()
{
    const engine::EngineConfig config;
    const auto owner = std::this_thread::get_id();
    std::promise<void> started;
    auto entered = started.get_future();
    std::vector<int> order;
    Gate gate;
    engine::ThreadSystem threads({1});
    engine::TaskSystem tasks(threads, {"Priority", 1, engine::TaskPriority::normal});
    ReleaseGate release{gate};
    if (!threads.initialize(config) || !tasks.initialize(config))
    {
        return false;
    }
    auto blocker = tasks.submit("blocker",
                                [&]
                                {
                                    started.set_value();
                                    gate.wait();
                                    return std::this_thread::get_id() != owner && !tasks.wait() &&
                                           !tasks.configure_concurrency(2) && !tasks.run({});
                                });
    if (!expect(blocker.valid() && entered.wait_for(5s) == std::future_status::ready,
                "task entered worker") ||
        !expect(blocker.wait_for(0ms) == std::future_status::timeout &&
                    !tasks.configure_concurrency(2),
                "submit is asynchronous and busy domain rejects reconfiguration"))
    {
        return false;
    }
    std::vector<std::future<bool>> futures;
    for (int index = 0; index < 10; ++index)
    {
        for (const auto priority :
             {engine::TaskPriority::low, engine::TaskPriority::normal, engine::TaskPriority::high})
        {
            futures.push_back(tasks.submit(
                "ordered",
                [&, priority]
                {
                    order.push_back(static_cast<int>(priority));
                    return std::this_thread::get_id() != owner;
                },
                priority));
        }
    }
    gate.release();
    if (!expect(result(blocker), "worker cannot synchronously reenter task control"))
    {
        return false;
    }
    for (auto& future : futures)
    {
        if (!expect(result(future), "accepted task completes on a worker"))
        {
            return false;
        }
    }
    return expect(order.size() == 30 &&
                      std::is_sorted(order.begin(), order.end(), std::greater<>()),
                  "ready single tasks use real high/normal/low priority") &&
           expect(tasks.wait(), "domain drains scheduling tails");
}

bool dependency_batches(std::size_t workers)
{
    const engine::EngineConfig config;
    const auto owner = std::this_thread::get_id();
    std::atomic<int> singles{0};
    std::atomic<int> independent{0};
    std::atomic<int> joins{0};
    engine::ThreadSystem threads({workers});
    engine::TaskSystem tasks(threads, {"Dependencies", workers, engine::TaskPriority::normal});
    if (!threads.initialize(config) || !tasks.initialize(config))
    {
        return false;
    }
    std::vector<std::future<bool>> batches;
    std::vector<std::future<bool>> individual;
    for (int index = 0; index < 96; ++index)
    {
        const bool fail = index % 3 == 0;
        auto count = std::make_shared<std::atomic<int>>(0);
        std::vector<engine::TaskDescription> graph{{"first",
                                                    [=]
                                                    {
                                                        ++*count;
                                                        return !fail;
                                                    },
                                                    {},
                                                    engine::TaskPriority::low},
                                                   {"parallel",
                                                    [&, count]
                                                    {
                                                        ++*count;
                                                        ++independent;
                                                        return true;
                                                    },
                                                    {}},
                                                   {"join",
                                                    [&, count]
                                                    {
                                                        ++joins;
                                                        return *count == 2 &&
                                                               std::this_thread::get_id() != owner;
                                                    },
                                                    {0, 1},
                                                    engine::TaskPriority::high}};
        batches.push_back(
            tasks.submit_batch(std::move(graph), static_cast<engine::TaskPriority>(index % 3)));
        individual.push_back(tasks.submit("independent single",
                                          [&]
                                          {
                                              ++singles;
                                              return true;
                                          }));
    }
    for (std::size_t index = 0; index < batches.size(); ++index)
    {
        auto& future = batches[index];
        if (!expect(future.valid() && future.wait_for(5s) == std::future_status::ready &&
                        future.get() == (index % 3 != 0),
                    "batch completes without blocking its worker on a future") ||
            !expect(result(individual[index]), "single tasks interoperate with dependency batches"))
        {
            return false;
        }
    }
    return expect(tasks.wait() && singles == 96 && independent == 96 && joins == 64,
                  "failed dependencies skip descendants, independent branches still finish");
}

bool independent_domains()
{
    const engine::EngineConfig config;
    std::promise<void> started;
    auto entered = started.get_future();
    Gate gate;
    engine::ThreadSystem threads({2});
    engine::TaskSystem editor(threads, {"Editor", 1, engine::TaskPriority::low});
    engine::TaskSystem runtime(threads, {"Runtime", 1, engine::TaskPriority::high});
    ReleaseGate release{gate};
    if (!threads.initialize(config) || !editor.initialize(config) || !runtime.initialize(config))
    {
        return false;
    }
    auto blocked = editor.submit("background",
                                 [&]
                                 {
                                     started.set_value();
                                     gate.wait();
                                     return true;
                                 });
    if (!expect(entered.wait_for(5s) == std::future_status::ready, "background work started"))
    {
        return false;
    }
    auto foreground =
        runtime.submit("frame preparation", [] { return true; }, engine::TaskPriority::high);
    if (!expect(result(foreground), "foreground domain can use remaining shared capacity"))
    {
        return false;
    }
    runtime.shutdown();
    if (!expect(blocked.wait_for(0ms) == std::future_status::timeout && editor.is_initialized(),
                "closing runtime does not drain or close editor domain"))
    {
        return false;
    }
    gate.release();
    editor.shutdown();
    return expect(result(blocked), "shutdown finishes all previously accepted work");
}

bool os_priority()
{
    // OS 无权提升时任务必须明确失败；普通任务在恢复后仍应正常。
    // 各平台具体调度值在平台探针验证，不把权限差异写死成跨平台测试失败。
    const engine::EngineConfig config;
    for (const auto priority : {engine::ThreadPriority::low, engine::ThreadPriority::high,
                                engine::ThreadPriority::normal})
    {
        bool executed = false;
        engine::ThreadSystem threads({1, priority});
        engine::TaskSystem tasks(threads, {"OS priority", 1});
        if (!threads.initialize(config) || !tasks.initialize(config))
        {
            return false;
        }
        auto future = tasks.submit("priority scope",
                                   [&]
                                   {
                                       executed = true;
                                       return true;
                                   });
        if (!expect(future.valid() && future.wait_for(5s) == std::future_status::ready,
                    "priority request always completes, even when OS denies it"))
        {
            return false;
        }
        const bool success = future.get();
        if (!expect(!success || executed, "reported success requires callback execution"))
        {
            return false;
        }
        if (priority == engine::ThreadPriority::normal &&
            !expect(success, "normal task still runs after scoped priority requests"))
        {
            return false;
        }
    }
    return true;
}

} // namespace

int main()
{
    return lifecycle_and_validation() && priority_and_async() && dependency_batches(1) &&
                   dependency_batches(3) && independent_domains() && os_priority()
               ? 0
               : 1;
}
