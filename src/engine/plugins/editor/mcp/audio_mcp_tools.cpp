#include "plugins/editor/mcp/component_mcp_tools.hpp"
namespace engine::plugins::mcp
{
bool register_audio_tools(McpSystem& system, EditorMcpTools& editor)
{
    using Json = nlohmann::json;
    const Json number = {{"type", "number"}};
    const Json enabled = {{"type", "boolean"}};
    if (!tools::component_tool(system, editor, "set_audio_source",
                               "Add or edit an audio source; clip uses a resource path. Undoable.",
                               {{"enabled", enabled},
                                {"clip", {{"type", "string"}, {"maxLength", 1024}}},
                                {"volume", number},
                                {"pitch", number},
                                {"loop", enabled},
                                {"playOnAwake", enabled},
                                {"spatial", enabled},
                                {"minDistance", number},
                                {"maxDistance", number}}) ||
        !tools::component_tool(system, editor, "get_audio_source", "Read audio source settings.",
                               Json::object()) ||
        !tools::component_tool(system, editor, "set_audio_listener",
                               "Add or edit the scene listener; undoable.",
                               {{"enabled", enabled}, {"volume", number}}) ||
        !tools::component_tool(system, editor, "get_audio_listener", "Read listener settings.",
                               Json::object()))
    {
        return false;
    }
    for (const char* command : {"play_audio", "pause_audio", "stop_audio", "get_audio_state"})
    {
        if (!tools::component_tool(system, editor, command,
                                   "Control or inspect playback. Play accepts a request; state 1 "
                                   "is pending, 2 playing, 3 paused, 4 failed, 0 stopped.",
                                   Json::object()))
        {
            return false;
        }
    }
    return true;
}
} // namespace engine::plugins::mcp
