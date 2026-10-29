#pragma once

#include "runtime/frame_status.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace engine
{

struct EngineConfig;
class System;
class WindowSystem;
class RenderSystem;
class ScriptSystem;
class WorldSystem;
struct ScriptStatus;

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
    [[nodiscard]] bool start_gameplay(std::string_view entry = {});
    void stop_gameplay() noexcept;
    void set_hot_reload(bool enabled);
    [[nodiscard]] const ScriptStatus* script_status() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;
    void request_exit() noexcept;
    void shutdown() noexcept;

private:
    Engine() noexcept;
    ~Engine() noexcept;

    std::vector<std::unique_ptr<System>> systems_;
    WindowSystem* window_ = nullptr;
    RenderSystem* renderer_ = nullptr;
    ScriptSystem* script_ = nullptr;
    WorldSystem* world_ = nullptr;
    std::string last_error_;
    bool initialized_ = false;
    bool exit_requested_ = false;
    bool failed_ = false;
};

} // namespace engine
