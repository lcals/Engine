#include "script_system.hpp"

#include <v8-initialization.h>

namespace engine
{

ScriptSystem::ScriptSystem() noexcept = default;

ScriptSystem::~ScriptSystem() noexcept
{
    shutdown();
}

bool ScriptSystem::on_initialize(const EngineConfig&)
{
    // 此阶段只确认 V8 已链接，不创建 isolate，也不初始化进程级平台。
    const char* version = v8::V8::GetVersion();
    return version != nullptr && version[0] != '\0';
}

void ScriptSystem::on_shutdown() noexcept
{
}

} // namespace engine
