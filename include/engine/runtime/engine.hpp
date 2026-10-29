#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace engine
{

struct EngineConfig;
class System;

class Engine final
{
public:
    [[nodiscard]] static Engine& instance() noexcept;

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    // 成功转移未初始化根系统的所有权；失败保持输入不变。
    [[nodiscard]] bool add_system(std::unique_ptr<System>& system);
    [[nodiscard]] std::size_t system_count() const noexcept;
    [[nodiscard]] bool is_initialized() const noexcept;
    [[nodiscard]] bool initialize(const EngineConfig& config);
    [[nodiscard]] bool tick(double delta_time);
    void shutdown() noexcept;

private:
    Engine() noexcept;
    ~Engine() noexcept;

    std::vector<std::unique_ptr<System>> systems_;
    bool initialized_ = false;
    bool executing_ = false;
    bool failed_ = false;
};

} // namespace engine
