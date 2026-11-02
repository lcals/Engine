#pragma once

#include "runtime/system/system.hpp"

#include <cstddef>
#include <memory>
#include <string>

namespace engine
{

enum class ThreadPriority
{
    low,
    normal,
    high
};

struct ThreadConfig
{
    // 0 按可用 CPU 自动选择；显式值 1–64。不创建自有线程池。
    std::size_t worker_limit = 0;
    ThreadPriority worker_priority = ThreadPriority::normal;
};

class ThreadSystem final : public System
{
public:
    explicit ThreadSystem(const ThreadConfig& config = {});
    ~ThreadSystem() noexcept override;

    // 预算上限，不是当前已创建线程数；初始化前/关闭后为 0。
    [[nodiscard]] std::size_t worker_count() const noexcept;
    [[nodiscard]] ThreadPriority worker_priority() const noexcept;
    [[nodiscard]] const std::string& last_error() const noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine
