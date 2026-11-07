#include "plugins/editor/animation_tools/animation_authoring_mcp_tools.hpp"
#include "plugins/editor/mcp/editor_tool_registration.hpp"

#include "plugins/editor/mcp/editor_mcp_tools.hpp"
#include "plugins/editor/mcp/mcp_system.hpp"

#include <nlohmann/json.hpp>

namespace engine::plugins::animation_tools
{
bool register_animation_authoring_mcp_tools(mcp::McpSystem& system, mcp::EditorMcpTools& editor)
{
    using Json = nlohmann::json;
    const Json text = {{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}};
    const Json number = {{"type", "number"}};
    const auto vector = [](int count)
    {
        return Json{{"type", "array"},
                    {"items", {{"type", "number"}}},
                    {"minItems", count},
                    {"maxItems", count}};
    };
    const Json order = {{"type", "integer"}, {"minimum", 0}, {"maximum", 65535}};
    const auto entries = [](Json item, int limit)
    { return Json{{"type", "array"}, {"items", std::move(item)}, {"maxItems", limit}}; };
    const Json sockets = entries(mcp::object_schema({{"name", text},
                                                     {"joint_path", text},
                                                     {"translation", vector(3)},
                                                     {"rotation", vector(4)},
                                                     {"scale", vector(3)}},
                                                    {"name", "joint_path"}),
                                 1024);
    const Json aim = entries(mcp::object_schema({{"name", text},
                                                 {"joint_path", text},
                                                 {"order", order},
                                                 {"forward_joint", vector(3)},
                                                 {"up_joint", vector(3)},
                                                 {"offset_joint", vector(3)}},
                                                {"name", "joint_path"}),
                             1024);
    const Json two_bone = entries(
        mcp::object_schema({{"name", text},
                            {"start_path", text},
                            {"middle_path", text},
                            {"end_path", text},
                            {"order", order},
                            {"mid_axis_joint", vector(3)},
                            {"soften", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                            {"twist_radians", number}},
                           {"name", "start_path", "middle_path", "end_path"}),
        1024);
    const Json events = entries(
        mcp::object_schema(
            {{"animation", text},
             {"marker_id", {{"type", "integer"}, {"minimum", 1}, {"maximum", 4294967295ULL}}},
             {"time", {{"type", "number"}, {"minimum", 0}}},
             {"name", text},
             {"payload_bool", {{"type", "boolean"}}},
             {"payload_number", number},
             {"payload_string", {{"type", "string"}}}},
            {"animation", "marker_id", "time", "name"}),
        4096);
    const auto add = [&](const char* name, const char* description, Json schema)
    {
        return mcp::register_editor_command_tool(system, editor, name, description,
                                                 std::move(schema), name);
    };
    return add("get_animation_authoring",
               "Read FBX adjacent-meta sockets, aim/two-bone IK definitions, timeline event "
               "markers, "
               "exact joint paths, clip keys/durations and optimistic source/meta hashes. Editor "
               "only.",
               mcp::object_schema({{"path", text}}, {"path"})) &&
           add("set_animation_authoring",
               "Replace the supplied complete authoring sections in existing FBX adjacent "
               "metadata. "
               "Use hashes from get_animation_authoring; omitted sections are preserved and [] "
               "clears "
               "a section. Event payload_bool/number/string are mutually exclusive; time is "
               "clip-local "
               "seconds. Validates joints, IK and clips before an undoable EditorCommands atomic "
               "edit. "
               "The normal asset watcher/import pipeline publishes GUID resources; no cache "
               "writes.",
               mcp::object_schema({{"path", text},
                                   {"sourceHash", text},
                                   {"metaHash", text},
                                   {"skeleton_sockets", sockets},
                                   {"skeleton_aim_ik", aim},
                                   {"skeleton_two_bone_ik", two_bone},
                                   {"animation_events", events}},
                                  {"path", "sourceHash", "metaHash"}));
}
} // namespace engine::plugins::animation_tools
