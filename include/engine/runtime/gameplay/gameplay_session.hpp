#pragma once

#include "runtime/resource/virtual_file_system.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace engine
{

namespace schema
{
struct AssetCatalogDataT;
}

class ResourceSystem;
class WorldSystem;
class RenderSystem;
class PhysicsSystem;
class AudioSystem;
class AnimationSystem;
class FontSystem;
class UiSystem;
class ScriptRuntime;
class TaskSystem;
class ResourceCatalog;
struct ScriptStatus;
struct ProjectScriptRequest;

enum class GameplayPrepareStatus
{
    preparing,
    ready,
    failed
};

class GameplaySession final
{
public:
    // 系统由运行宿主装配并保持就绪，生命周期须覆盖此会话；script 可以为空。
    GameplaySession(ResourceSystem& resources, WorldSystem& world, RenderSystem& renderer,
                    ScriptRuntime* script, PhysicsSystem* physics = nullptr,
                    AudioSystem* audio = nullptr, AnimationSystem* animation = nullptr,
                    FontSystem* fonts = nullptr, UiSystem* ui = nullptr,
                    TaskSystem* background = nullptr) noexcept;
    ~GameplaySession() noexcept;

    GameplaySession(const GameplaySession&) = delete;
    GameplaySession& operator=(const GameplaySession&) = delete;
    GameplaySession(GameplaySession&&) = delete;
    GameplaySession& operator=(GameplaySession&&) = delete;

    [[nodiscard]] bool set_resources(const schema::AssetCatalogDataT& data, const MountList& mounts,
                                     std::string_view entry = {});
    [[nodiscard]] bool begin_prepare(const ResourceCatalog& catalog, const MountList& mounts,
                                     std::string_view entry = {}, std::string_view scene = {});
    [[nodiscard]] GameplayPrepareStatus step_prepare(std::uint32_t resource_budget,
                                                     double max_seconds);
    [[nodiscard]] bool activate_prepared();
    [[nodiscard]] bool tick();
    [[nodiscard]] bool start(std::string_view entry = {});
    void stop(std::string_view reason = {}) noexcept;
    [[nodiscard]] bool execute_project_script(const ProjectScriptRequest& request,
                                              std::string& result_json, std::string& error);
    [[nodiscard]] const ScriptStatus* script_status() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;

private:
    void release_scene() noexcept;
    struct State;
    std::unique_ptr<State> state_;
    ResourceSystem& resources_;
    WorldSystem& world_;
    RenderSystem& renderer_;
    ScriptRuntime* script_ = nullptr;
    PhysicsSystem* physics_ = nullptr;
    AudioSystem* audio_ = nullptr;
    AnimationSystem* animation_ = nullptr;
    FontSystem* fonts_ = nullptr;
    UiSystem* ui_ = nullptr;
    TaskSystem* background_ = nullptr;
    std::string last_error_;
};

} // namespace engine
