#include "editor/commands/editor_commands.hpp"

#include "editor/commands/editor_edit.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <map>
#include <utility>
#include <vector>

namespace engine::editor
{
namespace
{
using Json = nlohmann::json;
constexpr std::size_t history_limit = 128;
constexpr std::size_t batch_limit = 64;

bool matching_scene(const Json& arguments, std::uint64_t current)
{
    if (!arguments.is_object() || arguments.size() != 1 || !arguments.contains("sceneId") ||
        !arguments["sceneId"].is_string() || current == 0)
    {
        return false;
    }
    const auto& text = arguments["sceneId"].get_ref<const std::string&>();
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
           value == current && text == std::to_string(value);
}

class BatchEdit final : public EditorEdit
{
public:
    std::vector<std::unique_ptr<EditorEdit>> edits;

    bool affects_scene() const noexcept override
    {
        return std::any_of(edits.begin(), edits.end(),
                           [](const auto& edit) { return edit->affects_scene(); });
    }

    bool apply(std::string& error) override
    {
        for (std::size_t index = 0; index < edits.size(); ++index)
        {
            if (!edits[index]->apply(error))
            {
                for (std::size_t applied = index; applied != 0; --applied)
                {
                    std::string rollback_error;
                    if (!edits[applied - 1]->revert(rollback_error))
                    {
                        error += "; rollback failed: " + rollback_error;
                    }
                }
                return false;
            }
        }
        return true;
    }

    bool revert(std::string& error) override
    {
        for (std::size_t index = edits.size(); index != 0; --index)
        {
            if (!edits[index - 1]->revert(error))
            {
                for (std::size_t restored = index; restored < edits.size(); ++restored)
                {
                    std::string rollback_error;
                    if (!edits[restored]->apply(rollback_error))
                    {
                        error += "; rollback failed: " + rollback_error;
                    }
                }
                return false;
            }
        }
        return true;
    }
};
} // namespace

struct EditorCommands::State
{
    struct Command
    {
        bool undoable = false;
        Handler handler;
    };

    SceneIdProvider scene_id;
    std::uint64_t generation = 0;
    std::uint64_t mutation_revision = 0;
    std::map<std::string, Command, std::less<>> commands;
    std::vector<std::unique_ptr<EditorEdit>> undo;
    std::vector<std::unique_ptr<EditorEdit>> redo;

    explicit State(SceneIdProvider provider) : scene_id(std::move(provider))
    {
    }

    void commit(std::unique_ptr<EditorEdit> edit)
    {
        redo.clear();
        if (undo.size() == history_limit)
        {
            undo.erase(undo.begin());
        }
        if (edit->affects_scene())
        {
            ++mutation_revision;
        }
        undo.push_back(std::move(edit));
    }

    bool prepare(std::string_view name, const Json& arguments, Json& result,
                 std::unique_ptr<EditorEdit>& edit, std::string& error, bool require_edit)
    {
        const auto found = commands.find(name);
        if (found == commands.end())
        {
            error = "Unknown editor command: " + std::string(name);
            return false;
        }
        if (!arguments.is_object() || (require_edit && !found->second.undoable))
        {
            error = "Command requires an arguments object; batches accept only edits";
            return false;
        }
        if (!found->second.handler(arguments, result, edit, error))
        {
            return false;
        }
        if (found->second.undoable != static_cast<bool>(edit))
        {
            error = "Editor command returned an invalid undo record";
            return false;
        }
        return true;
    }

    bool batch(const Json& arguments, Json& result, std::string& error)
    {
        if (!arguments.is_object() || arguments.size() != 1 || !arguments.contains("commands") ||
            !arguments["commands"].is_array() || arguments["commands"].empty() ||
            arguments["commands"].size() > batch_limit)
        {
            error = "Batch requires between 1 and 64 commands";
            return false;
        }
        auto batch = std::make_unique<BatchEdit>();
        for (const auto& command : arguments["commands"])
        {
            Json ignored;
            std::unique_ptr<EditorEdit> edit;
            bool ready = command.is_object() && command.size() == 2 &&
                         command.contains("command") && command["command"].is_string() &&
                         command.contains("arguments");
            if (ready)
            {
                ready = prepare(command["command"].get_ref<const std::string&>(),
                                command["arguments"], ignored, edit, error, true) &&
                        edit->apply(error);
            }
            else
            {
                error = "Each batch item requires command and arguments";
            }
            if (!ready)
            {
                std::string rollback_error;
                if (!batch->revert(rollback_error))
                {
                    error += "; rollback failed: " + rollback_error;
                }
                return false;
            }
            batch->edits.push_back(std::move(edit));
        }
        result = {{"sceneId", std::to_string(generation)}, {"count", batch->edits.size()}};
        commit(std::move(batch));
        return true;
    }
};

EditorCommands::EditorCommands(SceneIdProvider scene_id)
    : state_(std::make_unique<State>(std::move(scene_id)))
{
}

EditorCommands::~EditorCommands() noexcept = default;

bool EditorCommands::register_command(std::string_view name, bool undoable, Handler handler)
{
    if (name.empty() || name.size() > 128 || !handler || name == "batch" || name == "undo" ||
        name == "redo")
    {
        return false;
    }
    return state_->commands.emplace(std::string(name), State::Command{undoable, std::move(handler)})
        .second;
}

bool EditorCommands::execute(std::string_view name, const Json& arguments, Json& result,
                             std::string& error)
{
    result = nullptr;
    error.clear();
    const auto generation = state_->scene_id ? state_->scene_id() : 0;
    if (state_->generation != generation)
    {
        clear_history();
        state_->generation = generation;
    }
    bool success = false;
    if (name == "undo" || name == "redo")
    {
        auto& from = name == "undo" ? state_->undo : state_->redo;
        auto& to = name == "undo" ? state_->redo : state_->undo;
        if (!matching_scene(arguments, generation))
        {
            error = "Undo and redo require the current sceneId string";
        }
        else if (from.empty())
        {
            error = "No edit is available to " + std::string(name);
        }
        else if (name == "undo" ? from.back()->revert(error) : from.back()->apply(error))
        {
            if (from.back()->affects_scene())
            {
                ++state_->mutation_revision;
            }
            to.push_back(std::move(from.back()));
            from.pop_back();
            result = {{"sceneId", std::to_string(generation)}};
            success = true;
        }
    }
    else if (name == "batch")
    {
        success = state_->batch(arguments, result, error);
    }
    else
    {
        std::unique_ptr<EditorEdit> edit;
        success = state_->prepare(name, arguments, result, edit, error, false);
        if (success &&
            result.dump(-1, ' ', false, Json::error_handler_t::replace).size() > 1024 * 1024 - 4096)
        {
            error = "Editor command result exceeds the 1 MiB receipt limit";
            success = false;
        }
        if (success && edit)
        {
            success = edit->apply(error);
            if (success)
            {
                state_->commit(std::move(edit));
            }
        }
    }
    if (success)
    {
        if (!result.is_object())
        {
            result = Json::object();
        }
        result["undoCount"] = undo_count();
        result["redoCount"] = redo_count();
    }
    else
    {
        result = nullptr;
        if (error.empty())
        {
            error = "Editor command failed";
        }
    }
    return success;
}

void EditorCommands::clear_history() noexcept
{
    state_->undo.clear();
    state_->redo.clear();
}

std::size_t EditorCommands::undo_count() const noexcept
{
    return state_->undo.size();
}

std::size_t EditorCommands::redo_count() const noexcept
{
    return state_->redo.size();
}

std::uint64_t EditorCommands::mutation_revision() const noexcept
{
    return state_->mutation_revision;
}

} // namespace engine::editor
