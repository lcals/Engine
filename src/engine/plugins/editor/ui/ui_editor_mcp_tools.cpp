#include "plugins/editor/ui/ui_editor_mcp_tools.hpp"
#include "plugins/editor/mcp/editor_tool_registration.hpp"

#include "plugins/editor/mcp/editor_mcp_tools.hpp"
#include "plugins/runtime/mcp/mcp_system.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

namespace engine::plugins::ui
{
bool register_ui_editor_tools(mcp::McpSystem& system, mcp::EditorMcpTools& editor)
{
    using Json = nlohmann::json;
    const auto add = [&](std::string name, std::string description, Json schema)
    {
        auto command = name;
        return mcp::register_editor_command_tool(
            system, editor, std::move(name), std::move(description), std::move(schema), command);
    };
    const Json id{{"type", "string"}, {"minLength", 1}, {"maxLength", 20}};
    const Json short_text{{"type", "string"}, {"minLength", 1}, {"maxLength", 128}};
    const Json path{{"type", "string"}, {"minLength", 1}, {"maxLength", 4096}};
    const Json target =
        mcp::object_schema({{"documentId", id}, {"elementId", id}}, {"documentId", "elementId"});
    const Json point{{"type", "number"}, {"minimum", 0}};
    const Json boolean{{"type", "boolean"}};
    const Json button{{"type", "string"}, {"enum", {"left", "middle", "right"}}};
    const Json clicks{{"type", "integer"}, {"minimum", 1}, {"maximum", 2}};
    const Json event = mcp::object_schema(
        {{"type",
          {{"type", "string"},
           {"enum",
            {"pointer_move", "pointer_down", "pointer_up", "wheel", "focus", "key_down", "key_up",
             "text", "wait"}}}},
         {"target", target},
         {"x", point},
         {"y", point},
         {"button", button},
         {"clickCount", clicks},
         {"deltaX", {{"type", "number"}, {"minimum", -1000}, {"maximum", 1000}}},
         {"deltaY", {{"type", "number"}, {"minimum", -1000}, {"maximum", 1000}}},
         {"key", short_text},
         {"repeat", boolean},
         {"text", {{"type", "string"}, {"minLength", 1}, {"maxLength", 4096}}},
         {"milliseconds", {{"type", "number"}, {"minimum", 0}, {"maximum", 10000}}}},
        {"type"});
    const Json fonts{
        {"type", "array"},
        {"minItems", 1},
        {"maxItems", 64},
        {"items", mcp::object_schema({{"family", short_text}, {"asset", path}, {"shader", path}},
                                     {"family", "asset"})}};
    const Json textures{{"type", "array"}, {"maxItems", 256}, {"items", path}};
    const Json hash{{"type", "string"}, {"maxLength", 128}};
    return add("get_ui_documents",
               "Query UI bindings, document identities and logical/pixel window sizes across "
               "published scenes, including persistent nodes.",
               mcp::object_schema(Json::object(), Json::array())) &&
           add("set_ui_document",
               "Add or edit a UiComponent document binding in an editable preview; supports undo "
               "and scene saving.",
               mcp::object_schema(
                   {{"sceneId", id}, {"nodeId", id}, {"document", path}, {"enabled", boolean}},
                   {"sceneId", "nodeId", "document"})) &&
           add("get_ui_tree",
               "Read the RmlUi tree, layout and element identities for the current document "
               "generation; pages may span View changes.",
               mcp::object_schema(
                   {{"nodeId", id},
                    {"documentId", id},
                    {"root", target},
                    {"offset", {{"type", "integer"}, {"minimum", 0}, {"maximum", 8192}}},
                    {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1024}}},
                    {"depth", {{"type", "integer"}, {"minimum", 0}, {"maximum", 64}}}},
                   {"nodeId", "documentId"})) &&
           add("get_ui_element",
               "Read text, effective visibility/enabled state, focus, logical bounds and computed "
               "styles using a full element handle; password values are masked.",
               mcp::object_schema(
                   {{"target", target},
                    {"styles", {{"type", "array"}, {"maxItems", 64}, {"items", short_text}}}},
                   {"target"})) &&
           add("set_ui_element",
               "Modify only the current View. text/value/visible/enabled/image use value; class "
               "uses "
               "name+active; style uses name+value. Does not change the TS Model or save the "
               "source document.",
               mcp::object_schema(
                   {{"target", target},
                    {"operation",
                     {{"type", "string"},
                      {"enum",
                       {"text", "value", "visible", "enabled", "class", "style", "image"}}}},
                    {"value", Json::object()},
                    {"name", short_text},
                    {"active", boolean}},
                   {"target", "operation"})) &&
           add("get_ui_source",
               "path starts an asynchronous read of raw RML/meta; job "
               "queries read or save results. A missing new path returns empty hashes. Runtime DOM "
               "cannot be saved as source.",
               mcp::object_schema({{"path", path}, {"job", id}}, Json::array())) &&
           add("save_ui_source",
               "Asynchronously validate and atomically save project RML and adjacent meta, "
               "preserving the "
               "GUID. Supply both hashes from the read operation; query the returned job with "
               "get_ui_source "
               "for results. source_saved/import:pending does not yet mean the View has refreshed.",
               mcp::object_schema({{"path", path},
                                   {"source", {{"type", "string"}, {"maxLength", 1048576}}},
                                   {"expectedSourceHash", hash},
                                   {"expectedMetaHash", hash},
                                   {"fonts", fonts},
                                   {"textures", textures}},
                                  {"path", "source", "expectedSourceHash", "expectedMetaHash"})) &&
           add("ui_input",
               "Real UI input sequence, at most 256 events over 10 seconds; down/up "
               "must be paired and coordinates are logical. Supports dragging, wheel, focus, "
               "Unicode text, chords and nonblocking "
               "wait; real user input cancels the sequence. Key names are A-Z/0-9 or "
               "Enter/Tab/Space/Left/Right/Up/Down/Home/End/PageUp/PageDown/Delete/Backspace/"
               "Insert/Escape/Shift/Control/Alt/Meta/RightShift/RightControl/RightAlt/RightMeta/"
               "F1-F12.",
               mcp::object_schema(
                   {{"documentId", id},
                    {"events",
                     {{"type", "array"}, {"minItems", 1}, {"maxItems", 256}, {"items", event}}}},
                   {"documentId", "events"})) &&
           add("get_ui_input",
               "Query input job status, completed event count, failure index and last focus/hit; "
               "does not consume TS business events.",
               mcp::object_schema({{"job", id}}, {"job"})) &&
           add("cancel_ui_input",
               "Stop pending input and safely release simulated presses; already delivered input "
               "is not rolled back.",
               mcp::object_schema({{"job", id}}, {"job"})) &&
           add("click_ui",
               "Compose move/down/up using the same input job; "
               "fails if the target center is obscured or not interactive. Does not call business "
               "click handlers directly.",
               mcp::object_schema({{"target", target},
                                   {"x", point},
                                   {"y", point},
                                   {"button", button},
                                   {"clickCount", clicks}},
                                  {"target"}));
}
} // namespace engine::plugins::ui
