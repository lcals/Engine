#pragma once

#include <string>
#include <string_view>

namespace engine
{

class WorldSystem;
class RenderSystem;
class ScriptSystem;
struct ScriptStatus;

class GameplaySession final
{
public:
    // 系统由运行宿主装配并保持就绪，生命周期须覆盖此会话；script 可以为空。
    GameplaySession(WorldSystem& world, RenderSystem& renderer, ScriptSystem* script) noexcept;
    ~GameplaySession() noexcept;

    GameplaySession(const GameplaySession&) = delete;
    GameplaySession& operator=(const GameplaySession&) = delete;
    GameplaySession(GameplaySession&&) = delete;
    GameplaySession& operator=(GameplaySession&&) = delete;

    [[nodiscard]] bool start(std::string_view entry = {});
    void stop(std::string_view reason = {}) noexcept;
    [[nodiscard]] const ScriptStatus* script_status() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;

private:
    WorldSystem& world_;
    RenderSystem& renderer_;
    [[maybe_unused]] ScriptSystem* script_ = nullptr;
    std::string last_error_;
};

} // namespace engine
