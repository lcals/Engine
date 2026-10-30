#pragma once

#include "runtime/frame/frame_limiter.hpp"

#include <cstddef>
#include <cstdint>
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
    [[nodiscard]] bool set_target_frame_rate(std::int32_t rate) noexcept;
    [[nodiscard]] std::int32_t target_frame_rate() const noexcept;
    // 宿主包围完整帧调用，包含窗口事件、编辑器处理和渲染提交。
    void begin_frame() noexcept;
    void end_frame();
    [[nodiscard]] bool tick(double delta_time);
    void shutdown() noexcept;

private:
    Engine() noexcept;
    ~Engine() noexcept;

    std::vector<std::unique_ptr<System>> systems_;
    FrameLimiter frame_limiter_;
    bool initialized_ = false;
    bool executing_ = false;
    bool failed_ = false;
};

} // namespace engine
