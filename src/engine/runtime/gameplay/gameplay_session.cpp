#include "runtime/gameplay/gameplay_session.hpp"

#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/world/world_system.hpp"

#if ENGINE_HAS_SCRIPT_RUNTIME
#include "runtime/script/script_system.hpp"
#endif

namespace engine
{

GameplaySession::GameplaySession(WorldSystem& world, RenderSystem& renderer,
                                 ScriptSystem* script) noexcept
    : world_(world), renderer_(renderer), script_(script)
{
}

GameplaySession::~GameplaySession() noexcept
{
    stop();
}

bool GameplaySession::start(std::string_view entry)
{
    last_error_.clear();
#if ENGINE_HAS_SCRIPT_RUNTIME
    if (!world_.is_initialized() || !renderer_.is_initialized() || script_ == nullptr ||
        !script_->is_initialized())
    {
        last_error_ = "The runtime is not ready for gameplay";
        return false;
    }
    if (script_->status().running)
    {
        return true;
    }
    if (!script_->start_gameplay(entry))
    {
        last_error_ = script_->status().last_error;
        stop();
        return false;
    }
    if (!renderer_.prepare_scene())
    {
        stop("Cannot prepare scene rendering resources");
        return false;
    }
    return true;
#else
    static_cast<void>(entry);
    last_error_ = "Script runtime is not enabled";
    return false;
#endif
}

void GameplaySession::stop(std::string_view reason) noexcept
{
    if (!reason.empty())
    {
        last_error_ = reason;
    }
#if ENGINE_HAS_SCRIPT_RUNTIME
    if (script_ != nullptr)
    {
        script_->stop_gameplay();
    }
#endif
    world_.destroy_scene();
    renderer_.release_scene();
    ResourceSystem::clear_project_definitions();
}

const ScriptStatus* GameplaySession::script_status() const noexcept
{
#if ENGINE_HAS_SCRIPT_RUNTIME
    return script_ != nullptr ? &script_->status() : nullptr;
#else
    return nullptr;
#endif
}

std::string_view GameplaySession::last_error() const noexcept
{
    return last_error_;
}

} // namespace engine
