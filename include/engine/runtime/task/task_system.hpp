#pragma once

#include "runtime/system/system.hpp"

#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace engine
{

class ThreadSystem;

enum class TaskPriority
{
    low,
    normal,
    high
};

struct TaskConfig
{
    std::string name = "Tasks";
    std::size_t max_concurrency = 4;
    TaskPriority priority = TaskPriority::normal;
    // 包括未派发的完成委托；满额立即拒绝，不阻塞提交线程。
    std::size_t max_pending = 1024;
};

struct TaskDescription
{
    std::string name;
    std::function<bool()> execute;
    std::vector<std::size_t> dependencies;
    TaskPriority priority = TaskPriority::normal;
};

// 控制、提交及错误查询只由初始化线程调用；回调不得调用系统生命周期。
class TaskSystem final : public System
{
public:
    // 借用 threads；它先初始化，并在本域关闭后再关闭。
    explicit TaskSystem(ThreadSystem& threads, const TaskConfig& config = {});
    ~TaskSystem() noexcept override;

    // 仅所属协调线程调用；额度 1–64，域内有任务时不能修改。
    [[nodiscard]] bool configure_concurrency(std::size_t count);
    // 拒绝提交时返回无效 future；接收后 true/false 表示执行结果。
    // 函数在 worker 执行；闭包借用须活到 future 完成。
    [[nodiscard]] std::future<bool> submit(std::string name, std::function<bool()> task,
                                           TaskPriority priority = TaskPriority::normal);
    [[nodiscard]] std::future<bool> submit_batch(std::vector<TaskDescription> tasks,
                                                 TaskPriority priority = TaskPriority::normal);
    // 完成委托只由所属线程 dispatch_completions 派发；关闭会丢弃未派发委托。
    [[nodiscard]] bool submit(std::string name, std::function<bool()> task,
                              std::function<void(bool)> completed,
                              TaskPriority priority = TaskPriority::normal);
    // 允许任意线程动态投递。接受后保证执行；拒绝不写 last_error。
    // 调用方确保系统存活；关闭前先结束外部 job/barrier，不在函数内等待本池。
    [[nodiscard]] bool enqueue(std::string name, std::function<void()> task,
                               TaskPriority priority = TaskPriority::normal);
    // 两项预算任一为 0 不派发；一次调用只处理进入时已有的完成项。
    [[nodiscard]] std::size_t dispatch_completions(std::size_t max_count, double max_seconds);
    // 同步入口不在调用线程执行用户函数；回调内禁止等待本池任务。
    [[nodiscard]] bool run(const std::vector<TaskDescription>& tasks);
    [[nodiscard]] bool wait();
    [[nodiscard]] std::size_t max_concurrency() const noexcept;
    // 仅协调线程查询；异步执行失败由各自 future 返回。
    [[nodiscard]] const std::string& last_error() const noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine
