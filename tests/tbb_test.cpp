#include <oneapi/tbb/task_group.h>

#include <array>
#include <atomic>
#include <cstdio>

namespace
{

struct LargeTask
{
    std::atomic<int>& calls;
    std::array<unsigned char, 1024> state{};

    void operator()() const
    {
        calls.fetch_add(1 + state.front(), std::memory_order_relaxed);
    }
};

} // namespace

int main()
{
    std::atomic<int> calls{0};
    const LargeTask body{calls, {}};
    oneapi::tbb::task_group tasks;

    tasks.run(body);
    tasks.wait();
    if (calls.load(std::memory_order_relaxed) != 1)
    {
        std::fputs("TBB task did not execute exactly once\n", stderr);
        return 1;
    }

    tasks.cancel();
    tasks.run(body);
    tasks.wait();
    if (calls.load(std::memory_order_relaxed) != 1)
    {
        std::fputs("TBB executed a cancelled task\n", stderr);
        return 1;
    }

    // 任务数据超过小对象池阈值；未提交的任务句柄必须按实际类型大小释放。
    {
        const auto pending = tasks.defer(body);
        if (!pending)
        {
            std::fputs("TBB did not create a deferred task\n", stderr);
            return 1;
        }
    }
    if (calls.load(std::memory_order_relaxed) != 1)
    {
        std::fputs("TBB executed an unsubmitted task\n", stderr);
        return 1;
    }

    return 0;
}
