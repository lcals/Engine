#include "editor/editor_system.hpp"

#include "editor/project/project_system.hpp"

#include <imgui.h>

namespace engine
{

EditorSystem::EditorSystem(ProjectSystem& project) noexcept : project_(project)
{
}

EditorSystem::~EditorSystem() noexcept
{
    shutdown();
}

bool EditorSystem::on_initialize(const EngineConfig&)
{
    if (!project_.is_initialized() || !IMGUI_CHECKVERSION())
    {
        return false;
    }
    ImGuiContext* previous = ImGui::GetCurrentContext();
    context_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(previous);
    return context_ != nullptr;
}

void EditorSystem::on_shutdown() noexcept
{
    if (context_ != nullptr)
    {
        ImGui::DestroyContext(context_);
        context_ = nullptr;
    }
}

} // namespace engine
