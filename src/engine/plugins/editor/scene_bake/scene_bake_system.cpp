#include "plugins/editor/scene_bake/scene_bake_system.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/commands/editor_commands.hpp"
#include "editor/commands/editor_edit.hpp"
#include "plugins/editor/scene_bake/ibl_bake_job.hpp"
#include "plugins/editor/scene_bake/scene_bake_files.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"

#include <nlohmann/json.hpp>

#include <utility>

namespace engine::plugins::scene_bake
{
namespace
{
using Json = nlohmann::json;

bool bounded_integer(const Json& arguments, const char* name, std::uint16_t fallback,
                     std::uint16_t minimum, std::uint16_t maximum, bool power_of_two,
                     std::uint16_t& output)
{
    output = fallback;
    const auto found = arguments.find(name);
    if (found == arguments.end())
    {
        return true;
    }
    if (!found->is_number_integer() || found->get<double>() < minimum ||
        found->get<double>() > maximum)
    {
        return false;
    }
    output = found->get<std::uint16_t>();
    return !power_of_two || (output & (output - 1)) == 0;
}
} // namespace

struct SceneBakeSystem::State
{
    ResourceSystem& resources;
    RenderSystem& renderer;
    std::filesystem::path root;
    ValidateCallback validate;
    PublishCallback publish;
    std::unique_ptr<IblBakeJob> gpu;
    SceneBakeFiles files;
    ResourcePtr source;
    std::string job;
    std::string scene;
    std::string status = "idle";
    std::string error;
    std::uint64_t scene_id = 0;

    bool busy() const noexcept
    {
        return status == "running" || status == "cancelling" || status == "publishing";
    }

    Json report() const
    {
        Json outputs = Json::array();
        if (status == "complete")
        {
            for (const auto& output : files.outputs())
            {
                outputs.push_back({{"path", output.path}, {"guid", output.guid}});
            }
        }
        return {{"job", job},
                {"state", status},
                {"scene", scene},
                {"completed", gpu == nullptr ? 0 : gpu->completed()},
                {"total", gpu == nullptr ? 0 : gpu->total()},
                {"outputs", std::move(outputs)},
                {"error", error}};
    }
};

SceneBakeSystem::SceneBakeSystem(ResourceSystem& resources, RenderSystem& renderer,
                                 std::filesystem::path project_root, ValidateCallback validate,
                                 PublishCallback publish)
    : state_(std::make_unique<State>(resources, renderer, std::move(project_root),
                                     std::move(validate), std::move(publish)))
{
}
SceneBakeSystem::~SceneBakeSystem() noexcept
{
    shutdown();
}

bool SceneBakeSystem::register_commands(editor::EditorCommands& commands)
{
    return commands.register_command(
               "bake_scene", false,
               [this](const Json& arguments, Json& result, std::unique_ptr<editor::EditorEdit>&,
                      std::string& error)
               {
                   auto& s = *state_;
                   if (!is_initialized() || !s.renderer.gpu_ready() || s.busy())
                   {
                       error = "Scene baking requires an idle bake worker and an initialized GPU";
                       return false;
                   }
                   for (const auto& item : arguments.items())
                   {
                       if (item.key() != "scene" && item.key() != "environment" &&
                           item.key() != "effects" && item.key() != "resolution" &&
                           item.key() != "samples" && item.key() != "lutSize")
                       {
                           error = "Unknown scene baking argument: " + item.key();
                           return false;
                       }
                   }
                   if (!arguments.contains("scene") || !arguments["scene"].is_string() ||
                       !arguments.contains("environment") ||
                       !arguments["environment"].is_string() || !arguments.contains("effects") ||
                       arguments["effects"] != Json::array({"ibl"}))
                   {
                       error = "bake_scene requires scene, environment and effects: [ibl]";
                       return false;
                   }
                   std::uint16_t resolution = 0;
                   std::uint16_t samples = 0;
                   std::uint16_t lut_size = 0;
                   if (!bounded_integer(arguments, "resolution", 128, 16, 512, true, resolution) ||
                       !bounded_integer(arguments, "samples", 256, 16, 1024, false, samples) ||
                       !bounded_integer(arguments, "lutSize", 256, 32, 512, true, lut_size))
                   {
                       error = "Invalid bake resolution, sample count or LUT size";
                       return false;
                   }
                   const auto scene = arguments["scene"].get<std::string>();
                   const auto input = arguments["environment"].get<std::string>();
                   std::uint64_t scene_id = 0;
                   if (!s.validate(scene, scene_id, error))
                   {
                       return false;
                   }
                   s.files.cleanup();
                   s.files = {};
                   s.gpu.reset();
                   s.source.reset();
                   s.job = editor::asset_pipeline::new_guid();
                   s.scene = scene;
                   s.scene_id = scene_id;
                   s.error.clear();
                   s.status = "failed";
                   Guid guid;
                   if (!s.files.initialize(s.root, scene, input, s.job, s.resources, error) ||
                       !s.resources.catalog().identities().path_to_guid(input, guid) ||
                       !s.resources.loader().load(guid, s.source) || s.source == nullptr ||
                       s.source->texture() == nullptr)
                   {
                       if (error.empty())
                       {
                           error = "Cannot load the requested IBL input texture";
                       }
                       s.error = error;
                       s.files.cleanup();
                       return false;
                   }
                   s.gpu = std::make_unique<IblBakeJob>();
                   if (!s.gpu->start(*s.source->texture(), resolution, samples, lut_size, error))
                   {
                       s.error = error;
                       s.files.cleanup();
                       return false;
                   }
                   s.status = "running";
                   result = s.report();
                   return true;
               }) &&
           commands.register_command(
               "get_scene_bake", false,
               [this](const Json& arguments, Json& result, std::unique_ptr<editor::EditorEdit>&,
                      std::string& error)
               {
                   if (!arguments.empty() &&
                       (arguments.size() != 1 || !arguments.contains("job") ||
                        !arguments["job"].is_string() || arguments["job"] != state_->job))
                   {
                       error = "The requested bake job is not the current job";
                       return false;
                   }
                   result = state_->report();
                   return true;
               }) &&
           commands.register_command(
               "cancel_scene_bake", false,
               [this](const Json& arguments, Json& result, std::unique_ptr<editor::EditorEdit>&,
                      std::string& error)
               {
                   if (arguments.size() != 1 || !arguments.contains("job") ||
                       !arguments["job"].is_string() || arguments["job"] != state_->job ||
                       state_->gpu == nullptr)
                   {
                       error = "cancel_scene_bake requires the current job identity";
                       return false;
                   }
                   if (state_->busy())
                   {
                       state_->gpu->cancel();
                       state_->status = "cancelling";
                   }
                   result = state_->report();
                   return true;
               });
}

bool SceneBakeSystem::busy() const noexcept
{
    return state_->busy();
}
bool SceneBakeSystem::on_initialize(const EngineConfig&)
{
    return state_->validate && state_->publish;
}
bool SceneBakeSystem::on_tick(double)
{
    auto& s = *state_;
    if (!s.busy() || s.gpu == nullptr)
    {
        return true;
    }
    const auto progress = s.gpu->tick(s.renderer.frame_number(), s.error);
    if (progress == BakeProgress::running)
    {
        return true;
    }
    if (progress == BakeProgress::cancelled || progress == BakeProgress::failed)
    {
        s.status = progress == BakeProgress::cancelled ? "cancelled" : "failed";
        s.files.cleanup();
        s.source.reset();
        return true;
    }
    s.status = "publishing";
    std::uint64_t scene_id = 0;
    if (!s.validate(s.scene, scene_id, s.error) || scene_id != s.scene_id ||
        !s.files.unchanged(s.resources, s.error) || !s.files.stage(s.gpu->outputs(), s.error) ||
        !s.files.publish(s.publish, s.error))
    {
        if (s.error.empty())
        {
            s.error = "The active scene changed before bake publication";
        }
        s.status = "failed";
        s.files.cleanup();
    }
    else
    {
        s.status = "complete";
    }
    s.source.reset();
    return true;
}
void SceneBakeSystem::on_shutdown() noexcept
{
    if (state_->gpu != nullptr)
    {
        state_->gpu->shutdown();
    }
    if (state_->busy())
    {
        state_->status = "cancelled";
    }
    state_->files.cleanup();
    state_->source.reset();
}
} // namespace engine::plugins::scene_bake
