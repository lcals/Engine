#pragma once

#include "runtime/resource/virtual_file_system.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace engine
{

namespace schema
{
struct AssetCatalogDataT;
}

class WorldSystem;
class RenderSystem;
class PhysicsSystem;
class AudioSystem;
class ScriptRuntime;
struct ScriptStatus;

class GameplaySession final
{
public:
    // 系统由运行宿主装配并保持就绪，生命周期须覆盖此会话；script 可以为空。
    GameplaySession(WorldSystem& world, RenderSystem& renderer, ScriptRuntime* script,
                    PhysicsSystem* physics = nullptr, AudioSystem* audio = nullptr) noexcept;
    ~GameplaySession() noexcept;

    GameplaySession(const GameplaySession&) = delete;
    GameplaySession& operator=(const GameplaySession&) = delete;
    GameplaySession(GameplaySession&&) = delete;
    GameplaySession& operator=(GameplaySession&&) = delete;

    [[nodiscard]] bool set_resources(const schema::AssetCatalogDataT& data, const MountList& mounts,
                                     std::string_view entry = {});
    [[nodiscard]] bool tick();
    [[nodiscard]] bool start(std::string_view entry = {});
    void stop(std::string_view reason = {}) noexcept;
    [[nodiscard]] const ScriptStatus* script_status() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;

private:
    void release_scene() noexcept;
    struct State;
    std::unique_ptr<State> state_;
    WorldSystem& world_;
    RenderSystem& renderer_;
    ScriptRuntime* script_ = nullptr;
    PhysicsSystem* physics_ = nullptr;
    AudioSystem* audio_ = nullptr;
    std::string last_error_;
};

} // namespace engine
