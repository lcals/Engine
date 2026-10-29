#include "runtime/script/script_system.hpp"

#include <v8-initialization.h>

namespace engine
{

struct ScriptSystem::State
{
};

ScriptSystem::ScriptSystem() noexcept = default;

ScriptSystem::~ScriptSystem() noexcept
{
    shutdown();
}

bool ScriptSystem::start_gameplay(std::string_view)
{
    status_.last_error = "Script runtime is not enabled in this build";
    return false;
}

void ScriptSystem::stop_gameplay() noexcept
{
}

void ScriptSystem::set_hot_reload(bool)
{
}

const ScriptStatus& ScriptSystem::status() const noexcept
{
    return status_;
}

bool ScriptSystem::on_initialize(const EngineConfig&)
{
    const char* version = v8::V8::GetVersion();
    return version != nullptr && version[0] != '\0';
}

bool ScriptSystem::on_tick(double)
{
    return true;
}

void ScriptSystem::on_shutdown() noexcept
{
    status_ = {};
}

} // namespace engine
