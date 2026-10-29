#include "plugins/editor/mcp/component_mcp_tools.hpp"

namespace engine::plugins::mcp
{
bool register_animation_tools(McpSystem& system, EditorMcpTools& editor)
{
    using Json = nlohmann::json;
    const Json boolean = {{"type", "boolean"}};
    if (!tools::component_tool(
            system, editor, "set_animator",
            "Add or edit Animator on a MeshRenderer node; animation is an independent action "
            "resource path. Settings are undoable.",
            {{"enabled", boolean},
             {"animation", {{"type", "string"}, {"maxLength", 1024}}},
             {"speed", {{"type", "number"}, {"minimum", 0}, {"maximum", 16}}},
             {"loop", boolean},
             {"playOnAwake", boolean}}) ||
        !tools::component_tool(system, editor, "get_animator", "Read Animator settings.",
                               Json::object()) ||
        !tools::component_tool(system, editor, "seek_animation",
                               "Sample an animation at seconds in [0, duration]. A stopped "
                               "Animator becomes paused; a playing Animator keeps playing.",
                               {{"seconds", {{"type", "number"}, {"minimum", 0}}}}, {"seconds"}))
    {
        return false;
    }
    for (const char* command :
         {"play_animation", "pause_animation", "stop_animation", "get_animation_state"})
    {
        if (!tools::component_tool(system, editor, command,
                                   "Control or inspect animation. Pause retains pose, stop "
                                   "restores bind mesh. playback is stopped, playing or paused; "
                                   "time is in seconds. Playback controls are transient.",
                                   Json::object()))
        {
            return false;
        }
    }
    return true;
}
} // namespace engine::plugins::mcp
