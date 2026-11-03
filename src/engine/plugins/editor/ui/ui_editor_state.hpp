#pragma once

#include "editor/commands/editor_commands.hpp"
#include "plugins/editor/ui/ui_editor_commands.hpp"
#include "runtime/ui/ui_system.hpp"
#include "runtime/window/window_system.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace engine::plugins::ui
{
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
// Shared parsing helpers are private to this plugin.
[[nodiscard]] bool fields(const Json&, std::initializer_list<std::string_view>, std::string&);
[[nodiscard]] bool identity(const Json&, std::uint64_t&);
[[nodiscard]] bool handle(const Json&, UiElementHandle&);
[[nodiscard]] bool number(const Json&, float&);
[[nodiscard]] bool bounded_text(const Json&, std::string&, std::size_t limit = 65536);
[[nodiscard]] Json handle_json(UiElementHandle);
[[nodiscard]] Json element_json(const UiElementInfo&);
[[nodiscard]] SDL_Keycode input_key(std::string_view);
[[nodiscard]] bool validate_input(const Json&, std::uint64_t document, std::string&);

struct InputJob
{
    std::string id;
    std::uint64_t document = 0;
    std::uint64_t node = 0;
    Json events;
    std::size_t completed = 0;
    std::string status = "running";
    std::string error;
    Clock::time_point start{}, resume{};
    bool waiting = false;
    WindowSize size;
    std::set<SDL_Keycode> keys;
    SDL_MouseButtonFlags buttons = 0;
    std::uint16_t modifiers = 0;
    float x = 0, y = 0;
    UiElementHandle focus{}, hit{};
};
struct SourceJob
{
    std::string id;
    bool done = false;
    bool success = false;
    std::atomic<bool> canceled{false};
    Json result;
    std::string error;
};
struct UiEditorCommands::State
{
    State(WorldSystem& world, UiSystem& ui, WindowSystem& window, editor::ScenePreview& preview,
          TaskSystem& tasks, std::filesystem::path root);
    WorldSystem& world;
    UiSystem& ui;
    WindowSystem& window;
    editor::ScenePreview& preview;
    TaskSystem& tasks;
    std::filesystem::path project;
    std::uint64_t sequence = 0;
    std::map<std::string, InputJob> inputs;
    std::string active;
    std::map<std::string, std::shared_ptr<SourceJob>> sources;
    bool query(std::string_view name, const Json&, Json&, std::string&);
    bool edit_document(editor::SceneCommands&, const Json&, Json&,
                       std::unique_ptr<editor::EditorEdit>&, std::string&);
    bool source(std::string_view name, const Json&, Json&, std::string&);
    bool input(std::string_view name, const Json&, Json&, std::string&);
    void tick_input();
    void cancel_input(std::string_view reason) noexcept;
    bool execute_event(InputJob&, const Json&, std::string&);
    bool document(std::uint64_t id, std::uint64_t& node, UiElementHandle& root);
    bool focused(std::uint64_t document, UiElementHandle& output);
};
} // namespace engine::plugins::ui
