#pragma once

#include "runtime/system/system.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace engine
{

struct TaskDescription
{
    std::string name;
    std::function<bool()> execute;
    std::vector<std::size_t> dependencies;
};

class TaskSystem final : public System
{
public:
    TaskSystem();
    ~TaskSystem() noexcept override;

    // 仅允许空闲且已初始化时配置；1–64，默认 4。
    [[nodiscard]] bool configure_threads(std::size_t count);
    // 同步等待整批完成；失败依赖的下游任务不会执行。
    [[nodiscard]] bool run(const std::vector<TaskDescription>& tasks);
    [[nodiscard]] std::size_t thread_count() const noexcept;
    [[nodiscard]] const std::string& last_error() const noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine
