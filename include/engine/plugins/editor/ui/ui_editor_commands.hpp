#pragma once

#include <filesystem>
#include <memory>

union SDL_Event;
namespace engine
{
class WorldSystem;
class UiSystem;
class WindowSystem;
class TaskSystem;
namespace editor
{
class EditorCommands;
class ScenePreview;
} // namespace editor
} // namespace engine
namespace engine::plugins::ui
{
class UiEditorCommands final
{
public:
    // 所有借用持续到本对象销毁；命令服务先关闭，任务域最后关闭。
    UiEditorCommands(WorldSystem& world, UiSystem& ui, WindowSystem& window,
                     editor::ScenePreview& preview, TaskSystem& tasks,
                     std::filesystem::path project_root);
    ~UiEditorCommands() noexcept;
    UiEditorCommands(const UiEditorCommands&) = delete;
    UiEditorCommands& operator=(const UiEditorCommands&) = delete;
    UiEditorCommands(UiEditorCommands&&) = delete;
    UiEditorCommands& operator=(UiEditorCommands&&) = delete;
    [[nodiscard]] bool register_commands(editor::EditorCommands& commands);
    void tick(double delta_time);
    void observe_event(const SDL_Event& event) noexcept;
    void cancel_pending_input() noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace engine::plugins::ui
