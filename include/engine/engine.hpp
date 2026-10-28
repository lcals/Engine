#pragma once

#include "frame_status.hpp"

#include <memory>
#include <vector>

namespace engine
{

struct EngineConfig;
class System;
class WindowSystem;
class RenderSystem;

class Engine final
{
public:
    [[nodiscard]] static Engine& instance() noexcept;

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    [[nodiscard]] bool initialize(const EngineConfig& config);
    [[nodiscard]] FrameStatus tick(double delta_time);
    void request_exit() noexcept;
    void shutdown() noexcept;

private:
    Engine() noexcept;
    ~Engine() noexcept;

    std::vector<std::unique_ptr<System>> systems_;
    WindowSystem* window_ = nullptr;
    RenderSystem* renderer_ = nullptr;
    bool initialized_ = false;
    bool exit_requested_ = false;
    bool failed_ = false;
};

} // namespace engine
