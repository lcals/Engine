#include "plugins/runtime/mcp/runtime_values.hpp"
#include "runtime/app/runtime_host.hpp"
#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/utils/path_utils.hpp"
#include <SDL3/SDL.h>
#include <charconv>
#include <chrono>
#include <fstream>
#include <system_error>

namespace engine::plugins::mcp
{
using Json = nlohmann::json;
Json scene_load_value(const SceneLoadSnapshot& value)
{
    constexpr const char* states[] = {"loading",    "staged", "preparing", "ready",
                                      "activating", "active", "failed",    "cancelled"};
    constexpr const char* work_states[] = {"queued",       "working", "ready",
                                           "not_required", "failed",  "cancelled"};
    auto stages = Json::array();
    for (const auto& stage : value.stages)
    {
        stages.push_back({{"domain", stage.domain},
                          {"stage", stage.stage},
                          {"state", work_states[static_cast<unsigned>(stage.state)]},
                          {"completed", std::to_string(stage.completed)},
                          {"total", std::to_string(stage.total)},
                          {"totalKnown", stage.total_known},
                          {"inFlight", std::to_string(stage.in_flight)},
                          {"progressRevision", std::to_string(stage.progress_revision)},
                          {"lastProgressNs", std::to_string(stage.last_progress_ns)},
                          {"elapsedNs", std::to_string(stage.elapsed_ns)},
                          {"errorCode", stage.error_code},
                          {"error", stage.error}});
    }
    return {{"id",
             {{"owner", std::to_string(value.id.owner)},
              {"request", std::to_string(value.id.request)}}},
            {"scope",
             {{"worldEpoch", std::to_string(value.scope.world_epoch)},
              {"value", std::to_string(value.scope.value)}}},
            {"state", states[static_cast<unsigned>(value.state)]},
            {"sceneId", std::to_string(value.scene_id)},
            {"targetAlive", value.target_alive},
            {"progressRevision", std::to_string(value.progress_revision)},
            {"lastProgressNs", std::to_string(value.last_progress_ns)},
            {"firstSceneSubmissionId", std::to_string(value.first_scene_submission_id)},
            {"stages", std::move(stages)},
            {"errorCode", value.error_code},
            {"error", value.error}};
}
Json frame_value(const RenderFrameSnapshot& value)
{
    constexpr const char* kinds[] = {"scene", "movie", "clear"};
    return {{"deviceGeneration", std::to_string(value.device_generation)},
            {"submissionId", std::to_string(value.submission_id)},
            {"worldEpoch", std::to_string(value.world_epoch)},
            {"sceneId", std::to_string(value.scene_id)},
            {"contentRevision", std::to_string(value.content_revision)},
            {"submittedAtNs", std::to_string(value.submitted_at_ns)},
            {"kind", kinds[static_cast<unsigned>(value.kind)]},
            {"meshDraws", value.mesh_draws},
            {"uiDraws", value.ui_draws}};
}
Json screenshot_value(const ScreenshotSnapshot& value)
{
    constexpr const char* states[] = {"waiting_frame", "reading_back", "writing",
                                      "complete",      "failed",       "cancelled"};
    return {
        {"captureId",
         {{"owner", std::to_string(value.id.owner)}, {"value", std::to_string(value.id.value)}}},
        {"state", states[static_cast<unsigned>(value.state)]},
        {"frame", frame_value(value.frame)},
        {"path", value.path},
        {"errorCode", value.error_code},
        {"error", value.error}};
}
namespace
{
bool decimal(const Json& object, const char* field, std::uint64_t& output)
{
    output = 0;
    if (!object.is_object() || !object.contains(field) || !object[field].is_string())
    {
        return false;
    }
    const auto& text = object[field].get_ref<const std::string&>();
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), output);
    return !text.empty() && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
} // namespace
bool publish_observation(TaskSystem* tasks, std::future<bool>& pending,
                         const std::filesystem::path& target, const Json& status, bool finish)
{
    if (pending.valid())
    {
        if (!finish && pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            return true;
        }
        if (!pending.get())
        {
            return false;
        }
    }
    auto temporary = target;
    temporary += ".mcp-tmp";
    auto write = [target, temporary, text = status.dump() + "\n"]()
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.close();
        return output && SDL_RenamePath(PathUtils::to_utf8(temporary).c_str(),
                                        PathUtils::to_utf8(target).c_str());
    };
    if (finish)
    {
        return write();
    }
    if (!tasks)
    {
        return false;
    }
    pending = tasks->submit("Mcp.statusWrite", std::move(write));
    // Bounded admission: retry on a later poll without blocking presentation.
    return true;
}

bool observation_command(RuntimeHost& host, const std::filesystem::path& cache,
                         std::string_view command, const Json& args, Json& result,
                         std::string& error)
{
    result = Json::object();
    if (!args.is_object())
    {
        error = "Observation arguments must be an object";
        return false;
    }
    if (command == "get_scene_load")
    {
        SceneLoadId id;
        SceneLoadSnapshot snapshot;
        if (!args.contains("loadId") || !decimal(args["loadId"], "owner", id.owner) ||
            !decimal(args["loadId"], "request", id.request) || !host.gameplay() ||
            !host.gameplay()->scene_load_status(id, snapshot))
        {
            error = "Scene loading identity is invalid";
            return false;
        }
        result = scene_load_value(snapshot);
        return true;
    }
    auto* renderer = host.renderer();
    if (!renderer)
    {
        error = "Frame capture requires an available renderer";
        return false;
    }
    if (command == "get_frame_capture")
    {
        ScreenshotId id;
        ScreenshotSnapshot snapshot;
        if (!args.contains("captureId") || !decimal(args["captureId"], "owner", id.owner) ||
            !decimal(args["captureId"], "value", id.value) ||
            !renderer->screenshot_status(id, snapshot))
        {
            error = "Capture identity is invalid";
            return false;
        }
        result = screenshot_value(snapshot);
        return true;
    }
    if (command != "capture_frame" || !args.contains("path") || !args["path"].is_string())
    {
        error = "Capture requires a relative cache PNG path";
        return false;
    }
    const auto text = args["path"].get<std::string>();
    const auto relative = PathUtils::from_utf8(text);
    if (text.empty() || text.find('\0') != std::string::npos ||
        text.find('\\') != std::string::npos || relative.is_absolute() ||
        relative.has_root_name() || relative.extension() != ".png")
    {
        error = "Screenshot path must be a relative PNG filename";
        return false;
    }
    for (const auto& part : relative)
    {
        if (part == "." || part == "..")
        {
            error = "Screenshot path cannot traverse directories";
            return false;
        }
    }
    std::filesystem::path current;
    for (const auto& part : cache / relative)
    {
        current /= part;
        std::error_code code;
        const auto status = std::filesystem::symlink_status(current, code);
        if (code == std::errc::no_such_file_or_directory)
        {
            continue;
        }
        if (code || std::filesystem::is_symlink(status))
        {
            error = "Screenshot path contains a symlink or inaccessible entry";
            return false;
        }
    }
    ScreenshotRequest request;
    request.path = PathUtils::to_utf8(cache / relative);
    const auto selection = args.value("selection", Json("next_frame"));
    if (selection != "next_frame" && selection != "scene_scope")
    {
        error = "Unknown screenshot selection";
        return false;
    }
    if (selection == "scene_scope")
    {
        request.selection = ScreenshotSelection::scene_scope;
        if (!args.contains("scope") ||
            !decimal(args["scope"], "worldEpoch", request.scope.world_epoch) ||
            !decimal(args["scope"], "value", request.scope.value) ||
            request.scope.world_epoch == 0 || request.scope.value == 0)
        {
            error = "Scene capture requires a nonempty scope";
            return false;
        }
    }
    if ((args.contains("deviceGeneration") &&
         !decimal(args, "deviceGeneration", request.device_generation)) ||
        (args.contains("afterSubmissionId") &&
         !decimal(args, "afterSubmissionId", request.after_submission_id)))
    {
        error = "Frame bounds must be uint64 decimal strings";
        return false;
    }
    std::error_code code;
    std::filesystem::create_directories((cache / relative).parent_path(), code);
    ScreenshotId id;
    ScreenshotSnapshot snapshot;
    if (code || !renderer->request_screenshot(request, id) ||
        !renderer->screenshot_status(id, snapshot))
    {
        error = "Renderer cannot accept this capture request";
        return false;
    }
    result = screenshot_value(snapshot);
    return true;
}

} // namespace engine::plugins::mcp
