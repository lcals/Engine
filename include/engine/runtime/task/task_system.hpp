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
