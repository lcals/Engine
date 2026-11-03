#include "plugins/editor/ui/ui_editor_commands.hpp"

#include "editor/commands/components/component_edit.hpp"
#include "editor/commands/scene_commands.hpp"
#include "editor/scene/scene_preview.hpp"
#include "plugins/editor/ui/ui_editor_state.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/ui_component.hpp"
#include "runtime/world/world_system.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <tuple>
#include <utility>

namespace engine::plugins::ui
{
bool fields(const Json& args, std::initializer_list<std::string_view> names, std::string& error)
{
    if (!args.is_object())
    {
        error = "Arguments must be an object";
        return false;
    }
    for (const auto& item : args.items())
    {
        if (std::find(names.begin(), names.end(), item.key()) == names.end())
        {
            error = "Unknown field: " + item.key();
            return false;
        }
    }
    return true;
}
bool identity(const Json& value, std::uint64_t& output)
{
    output = 0;
    if (!value.is_string())
    {
        return false;
    }
    const auto& text = value.get_ref<const std::string&>();
    const auto read = std::from_chars(text.data(), text.data() + text.size(), output);
    return read.ec == std::errc{} && read.ptr == text.data() + text.size() && output != 0 &&
           text == std::to_string(output);
}
bool handle(const Json& value, UiElementHandle& output)
{
    output = {};
    return value.is_object() && value.size() == 2 && value.contains("documentId") &&
           value.contains("elementId") && identity(value["documentId"], output.document_id) &&
           identity(value["elementId"], output.element_id);
}
bool number(const Json& value, float& output)
{
    if (!value.is_number())
    {
        return false;
    }
    const auto n = value.get<double>();
    if (!std::isfinite(n) || std::abs(n) > std::numeric_limits<float>::max())
    {
        return false;
    }
    output = static_cast<float>(n);
    return true;
}
bool bounded_text(const Json& value, std::string& output, std::size_t limit)
{
    if (!value.is_string() || value.get_ref<const std::string&>().size() > limit)
    {
        return false;
    }
    output = value.get<std::string>();
    return output.find('\0') == std::string::npos;
}
Json handle_json(UiElementHandle h)
{
    return h.document_id == 0 ? Json(nullptr)
                              : Json{{"documentId", std::to_string(h.document_id)},
                                     {"elementId", std::to_string(h.element_id)}};
}
Json element_json(const UiElementInfo& info)
{
    constexpr std::size_t max_text = 8192;
    return {{"handle", handle_json(info.handle)},
            {"parent", handle_json(info.parent)},
            {"tag", info.tag},
            {"id", info.id},
            {"classes", info.classes},
            {"text", info.text.substr(0, max_text)},
            {"value", info.value.substr(0, max_text)},
            {"textTruncated", info.text.size() > max_text},
            {"valueTruncated", info.value.size() > max_text},
            {"bounds", info.bounds},
            {"childCount", info.child_count},
            {"visible", info.visible},
            {"enabled", info.enabled},
            {"focused", info.focused}};
}
UiEditorCommands::State::State(WorldSystem& w, UiSystem& u, WindowSystem& win,
                               editor::ScenePreview& p, TaskSystem& t, std::filesystem::path root)
    : world(w), ui(u), window(win), preview(p), tasks(t), project(std::move(root))
{
}
UiEditorCommands::UiEditorCommands(WorldSystem& w, UiSystem& u, WindowSystem& win,
                                   editor::ScenePreview& p, TaskSystem& t,
                                   std::filesystem::path root)
    : state_(std::make_unique<State>(w, u, win, p, t, std::move(root)))
{
}
UiEditorCommands::~UiEditorCommands() noexcept
{
    cancel_pending_input();
    for (auto& [id, job] : state_->sources)
    {
        job->canceled.store(true);
    }
}
bool UiEditorCommands::register_commands(editor::EditorCommands& commands,
                                         editor::SceneCommands& scene_commands)
{
    for (const auto* name :
         {"get_ui_documents", "get_ui_tree", "get_ui_element", "set_ui_element", "get_ui_source",
          "save_ui_source", "ui_input", "get_ui_input", "cancel_ui_input", "click_ui"})
    {
        if (!commands.register_command(
                name, false,
                [this, name](const Json& args, Json& result, std::unique_ptr<editor::EditorEdit>&,
                             std::string& error)
                {
                    const std::string_view command(name);
                    if (command == "get_ui_source" || command == "save_ui_source")
                    {
                        return state_->source(command, args, result, error);
                    }
                    if (command == "ui_input" || command == "get_ui_input" ||
                        command == "cancel_ui_input" || command == "click_ui")
                    {
                        return state_->input(command, args, result, error);
                    }
                    return state_->query(command, args, result, error);
                }))
        {
            return false;
        }
    }
    return commands.register_command(
        "set_ui_document", true,
        [this, &scene_commands](const Json& args, Json& result,
                                std::unique_ptr<editor::EditorEdit>& edit, std::string& error)
        { return state_->edit_document(scene_commands, args, result, edit, error); });
}
void UiEditorCommands::tick(double)
{
    state_->tick_input();
}
void UiEditorCommands::cancel_pending_input() noexcept
{
    state_->cancel_input("Host canceled UI input");
}
void UiEditorCommands::observe_event(const SDL_Event& event) noexcept
{
    switch (event.type)
    {
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_TEXT_EDITING:
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_HIDDEN:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        state_->cancel_input("Native input or window change superseded UI input");
        break;
    default:
        break;
    }
}
bool UiEditorCommands::State::document(std::uint64_t id, std::uint64_t& node, UiElementHandle& root)
{
    node = 0;
    root = {};
    if (!ui.synchronize())
    {
        return false;
    }
    for (const auto& scene : world.scenes())
    {
        for (const auto& item : scene->nodes())
        {
            if (item->get_component<UiComponent>() && ui.document_id(item->id()) == id)
            {
                node = item->id();
                return ui.document_root(node, root);
            }
        }
    }
    return false;
}
bool UiEditorCommands::State::focused(std::uint64_t doc, UiElementHandle& output)
{
    output = {};
    std::uint64_t node = 0;
    UiElementHandle root;
    if (!document(doc, node, root))
    {
        return false;
    }
    std::vector<UiElementHandle> pending{root};
    std::size_t visits = 0;
    while (!pending.empty() && ++visits <= 8192)
    {
        auto current = pending.back();
        pending.pop_back();
        UiElementInfo info;
        if (!ui.inspect_element(current, info))
        {
            return false;
        }
        if (info.focused)
        {
            output = current;
            return true;
        }
        if (info.child_count > 8192 - pending.size())
        {
            return false;
        }
        for (std::uint32_t i = 0; i < info.child_count; ++i)
        {
            UiElementHandle child;
            if (!ui.child_element(current, i, child))
            {
                return false;
            }
            pending.push_back(child);
        }
    }
    return false;
}
bool UiEditorCommands::State::query(std::string_view name, const Json& args, Json& result,
                                    std::string& error)
{
    if (name == "get_ui_documents")
    {
        if (!fields(args, {}, error))
        {
            return false;
        }
        const bool synchronized = ui.synchronize();
        WindowSize size;
        const bool has_size = window.size(size);
        result = {{"documents", Json::array()},
                  {"synchronized", synchronized},
                  {"window",
                   {{"logicalWidth", size.logical_width},
                    {"logicalHeight", size.logical_height},
                    {"pixelWidth", size.pixel_width},
                    {"pixelHeight", size.pixel_height},
                    {"available", has_size}}}};
        for (const auto& scene : world.scenes())
        {
            for (const auto& node : scene->nodes())
            {
                if (const auto* component = node->get_component<UiComponent>())
                {
                    std::string path;
                    (void)ResourceSystem::guid_to_path(component->settings().document_guid, path);
                    const auto id = ui.document_id(node->id());
                    const auto& resource = component->document();
                    Json entry{{"sceneId", std::to_string(scene->id())},
                               {"nodeId", std::to_string(node->id())},
                               {"documentId", id ? Json(std::to_string(id)) : Json(nullptr)},
                               {"document", path},
                               {"guid", component->settings().document_guid},
                               {"enabled", component->enabled()},
                               {"visible", ui.document_visible(node->id())},
                               {"persistent", node->persistent()},
                               {"revision", std::to_string(component->document_revision())},
                               {"resourceGeneration",
                                resource ? std::to_string(resource->generation()) : "0"}};
                    if (!id)
                    {
                        entry["error"] = "UI document is not loaded";
                    }
                    result["documents"].push_back(std::move(entry));
                }
            }
        }
        return true;
    }
    if (name == "get_ui_tree")
    {
        std::uint64_t node = 0, doc = 0;
        if (!fields(args, {"nodeId", "documentId", "root", "offset", "limit", "depth"}, error) ||
            !args.contains("nodeId") || !identity(args["nodeId"], node) ||
            !args.contains("documentId") || !identity(args["documentId"], doc))
        {
            if (error.empty())
            {
                error = "nodeId and documentId must be full decimal strings";
            }
            return false;
        }
        UiElementHandle root;
        if (ui.document_id(node) != doc || !ui.document_root(node, root))
        {
            error = "Stale or unavailable UI document";
            return false;
        }
        if (args.contains("root") && (!handle(args["root"], root) || root.document_id != doc))
        {
            error = "Invalid subtree root";
            return false;
        }
        std::size_t offset = 0, limit = 256, depth = 8;
        for (auto [key, dest, maximum] : {std::tuple{"offset", &offset, std::size_t(8192)},
                                          std::tuple{"limit", &limit, std::size_t(1024)},
                                          std::tuple{"depth", &depth, std::size_t(64)}})
        {
            if (args.contains(key))
            {
                if (!args[key].is_number_unsigned() && !args[key].is_number_integer())
                {
                    error = "Invalid tree pagination";
                    return false;
                }
                const auto value = args[key].get<std::int64_t>();
                if (value < 0 || std::uint64_t(value) > maximum ||
                    (std::string_view(key) == "limit" && !value))
                {
                    error = "Tree pagination exceeds bounds";
                    return false;
                }
                *dest = std::size_t(value);
            }
        }
        result = {{"elements", Json::array()}, {"truncated", false}, {"nextOffset", nullptr}};
        std::vector<std::pair<UiElementHandle, std::size_t>> pending{{root, 0}};
        std::size_t visited = 0;
        while (!pending.empty() && visited < offset + limit)
        {
            const auto [current, level] = pending.back();
            pending.pop_back();
            UiElementInfo info;
            if (!ui.inspect_element(current, info))
            {
                error = "Stale tree element";
                return false;
            }
            if (visited++ >= offset)
            {
                auto item = element_json(info);
                item["depth"] = level;
                item["childrenTruncated"] = level >= depth && info.child_count != 0;
                if (level >= depth && info.child_count)
                {
                    result["truncated"] = true;
                }
                result["elements"].push_back(std::move(item));
            }
            if (level < depth)
            {
                for (auto i = info.child_count; i > 0; --i)
                {
                    UiElementHandle child;
                    if (!ui.child_element(current, i - 1, child))
                    {
                        error = "Tree changed";
                        return false;
                    }
                    pending.emplace_back(child, level + 1);
                    if (pending.size() > 16384)
                    {
                        error = "Subtree too large; query a narrower root";
                        return false;
                    }
                }
            }
        }
        if (!pending.empty())
        {
            result["truncated"] = true;
            result["nextOffset"] = visited;
        }
        return true;
    }
    UiElementHandle target;
    if (!args.is_object() || !args.contains("target") || !handle(args["target"], target))
    {
        error = "target requires documentId and elementId";
        return false;
    }
    UiElementInfo info;
    if (!ui.inspect_element(target, info))
    {
        error = "Stale UI element";
        return false;
    }
    if (name == "get_ui_element")
    {
        if (!fields(args, {"target", "styles"}, error))
        {
            return false;
        }
        result = element_json(info);
        result["styles"] = Json::object();
        if (args.contains("styles"))
        {
            if (!args["styles"].is_array() || args["styles"].size() > 64)
            {
                error = "styles must contain at most 64 property names";
                return false;
            }
            for (const auto& item : args["styles"])
            {
                std::string key, value;
                if (!bounded_text(item, key, 128) || key.empty() ||
                    !ui.element_style(target, key, value))
                {
                    error = "Unknown or unavailable computed style";
                    return false;
                }
                result["styles"][key] = value;
            }
        }
        return true;
    }
    if (!fields(args, {"target", "operation", "value", "name", "active"}, error) ||
        !args.contains("operation") || !args["operation"].is_string())
    {
        if (error.empty())
        {
            error = "operation is required";
        }
        return false;
    }
    const auto operation = args["operation"].get<std::string>();
    const bool valid_fields =
        operation == "class"   ? fields(args, {"target", "operation", "name", "active"}, error)
        : operation == "style" ? fields(args, {"target", "operation", "name", "value"}, error)
                               : fields(args, {"target", "operation", "value"}, error);
    if (!valid_fields)
    {
        return false;
    }
    bool ok = false;
    std::string value, key;
    if ((operation == "visible" || operation == "enabled") && args.contains("value") &&
        args["value"].is_boolean())
    {
        ok = operation == "visible" ? ui.set_visible(target, args["value"].get<bool>())
                                    : ui.set_enabled(target, args["value"].get<bool>());
    }
    else if (operation == "class" && args.contains("name") &&
             bounded_text(args["name"], key, 128) && !key.empty() && args.contains("active") &&
             args["active"].is_boolean())
    {
        ok = ui.set_class(target, key, args["active"].get<bool>());
    }
    else if (args.contains("value") && bounded_text(args["value"], value))
    {
        if (operation == "text")
        {
            ok = ui.set_text(target, value);
        }
        else if (operation == "value")
        {
            ok = ui.set_value(target, value);
        }
        else if (operation == "image")
        {
            ok = ui.set_image(target, value);
        }
        else if (operation == "style" && args.contains("name") &&
                 bounded_text(args["name"], key, 128) && !key.empty())
        {
            ok = ui.set_style(target, key, value);
        }
    }
    if (!ok)
    {
        error = "Invalid UI operation, value or undeclared resource";
        return false;
    }
    result = {{"changed", true}, {"scope", "view"}, {"target", handle_json(target)}};
    return true;
}
bool UiEditorCommands::State::edit_document(editor::SceneCommands& scene_commands, const Json& args,
                                            Json& result, std::unique_ptr<editor::EditorEdit>& edit,
                                            std::string& error)
{
    if (!preview.active())
    {
        error = "UI document binding requires an editable scene preview";
        return false;
    }
    if (!fields(args, {"sceneId", "nodeId", "document", "enabled"}, error))
    {
        return false;
    }
    auto* node = editor::components::target(world, args, error);
    std::string path;
    Guid guid;
    if (!node || !args.contains("document") || !bounded_text(args["document"], path, 4096) ||
        !ResourceSystem::path_to_guid(path, guid))
    {
        if (error.empty())
        {
            error = "Imported UI document is required";
        }
        return false;
    }
    auto* resources = ResourceSystem::instance();
    ResourcePtr resource;
    if (!resources || !resources->loader().load(guid, resource) || !resource || !resource->ui())
    {
        error = "Cannot load UI resource";
        return false;
    }
    auto* component = node->get_component<UiComponent>();
    std::optional<schema::ComponentDataT> before;
    if (component)
    {
        before.emplace();
        if (!component->build_data(*before))
        {
            error = "Cannot snapshot UiComponent";
            return false;
        }
    }
    schema::UiDataT data;
    data.document_guid = guid;
    schema::ComponentDataT after;
    after.payload.Set(data);
    after.enabled = !component || component->enabled();
    after.visible = !component || component->visible();
    if (args.contains("enabled"))
    {
        if (!args["enabled"].is_boolean())
        {
            error = "enabled must be boolean";
            return false;
        }
        after.enabled = args["enabled"].get<bool>();
    }
    std::vector<schema::AssetEntryT> versions{resource->info()};
    if (component && component->document())
    {
        versions.push_back(component->document()->info());
    }
    edit = std::make_unique<editor::components::ComponentEdit>(
        world, node->scene().id(), scene_commands.node_identity(node->id()), std::move(before),
        std::move(after), std::move(versions));
    result = {{"sceneId", std::to_string(node->scene().id())},
              {"nodeId", std::to_string(node->id())},
              {"document", path},
              {"guid", guid}};
    return true;
}
} // namespace engine::plugins::ui
