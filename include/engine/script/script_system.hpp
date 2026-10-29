#pragma once

#include "system.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace engine
{

struct ScriptStatus
{
    bool running = false;
    bool hot_reload = false;
    std::string active_revision;
    std::string available_revision;
    std::string last_error;
};

class ScriptSystem final : public System
{
public:
    ScriptSystem() noexcept;
    ~ScriptSystem() noexcept override;

    // 空入口采用发布清单 entry；无清单时使用 scripts/game.js。
    [[nodiscard]] bool start_gameplay(std::string_view entry = {});
    void stop_gameplay() noexcept;
    void set_hot_reload(bool enabled);
    [[nodiscard]] const ScriptStatus& status() const noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    struct State;
    ScriptStatus status_;
    std::unique_ptr<State> state_;
};

} // namespace engine
