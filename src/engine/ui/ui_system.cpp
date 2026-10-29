#include "ui_system.hpp"

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
    // 先接入 RmlUi 二进制库，渲染接口和 UI 上下文在 UI 阶段实现。
    return !Rml::GetVersion().empty();
}

void UiSystem::on_shutdown() noexcept
{
}

} // namespace engine
