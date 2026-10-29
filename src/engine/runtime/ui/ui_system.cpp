#include "runtime/ui/ui_system.hpp"

#include <RmlUi/Core/Core.h>

namespace engine
{

UiSystem::UiSystem() noexcept = default;

UiSystem::~UiSystem() noexcept
{
    shutdown();
}

bool UiSystem::on_initialize(const EngineConfig&)
{
    // 校验 RmlUi 库的链接；本系统不创建渲染接口与 UI 上下文。
    return !Rml::GetVersion().empty();
}

void UiSystem::on_shutdown() noexcept
{
}

} // namespace engine
