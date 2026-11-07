#include "plugins/editor/scene_bake/scene_bake_system.hpp"
#include "editor/resource_access.hpp"
#include "runtime/resource/types/texture_resource.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/scene_importer.hpp"
#include "editor/commands/editor_commands.hpp"
#include "editor/commands/editor_edit.hpp"
#include "plugins/editor/scene_bake/ibl_bake_job.hpp"
#include "plugins/editor/scene_bake/lighting_bake_job.hpp"
#include "plugins/editor/scene_bake/scene_bake_files.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/world/world_system.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <set>
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

bool lighting_settings(const Json& args, LightingBakeSettings& settings, std::string& error)
{
    std::uint16_t resolution, hemisphere, bounces, passes;
    if (!bounded_integer(args, "resolution", 64, 16, 512, true, resolution) ||
        !bounded_integer(args, "hemisphereResolution", 16, 16, 128, true, hemisphere) ||
        !bounded_integer(args, "bounces", 2, 1, 8, false, bounces) ||
        !bounded_integer(args, "interpolationPasses", 2, 0, 4, false, passes))
    {
        error = "Invalid lighting resolution, bounces, or interpolation passes";
        return false;
    }
    settings.lightmap_resolution = resolution;
    settings.hemisphere_resolution = hemisphere;
    settings.bounce_count = static_cast<std::uint8_t>(bounces);
    settings.interpolation_passes = static_cast<std::uint8_t>(passes);
    if (args.contains("interpolationThreshold"))
    {
        const auto& value = args["interpolationThreshold"];
        if (!value.is_number() || !std::isfinite(value.get<double>()) || value.get<double>() < 0 ||
            value.get<double>() > 1)
        {
            error = "Invalid interpolation threshold";
            return false;
        }
        settings.interpolation_threshold = value.get<float>();
    }
    if (args.contains("lightNodeIds"))
    {
        const auto& ids = args["lightNodeIds"];
        if (!ids.is_array() || ids.size() > 1024)
        {
            error = "lightNodeIds must be a bounded array";
            return false;
        }
        for (const auto& id : ids)
        {
            if (!id.is_number_unsigned() &&
                (!id.is_number_integer() || id.get<std::int64_t>() <= 0))
            {
                error = "Fixed light identities must be positive integers";
                return false;
            }
            if (id.get<std::uint64_t>() == 0)
            {
                error = "Fixed light identity cannot be zero";
                return false;
            }
            settings.light_node_ids.push_back(id.get<std::uint64_t>());
        }
    }
    if (args.contains("probeVolumes"))
    {
        const auto& targets = args["probeVolumes"];
        if (!targets.is_array() || targets.size() > 64)
        {
            error = "probeVolumes must be a bounded array";
            return false;
        }
        for (const auto& target : targets)
        {
            if (!target.is_object() || target.size() != 4 || !target.contains("nodeId") ||
                !target.contains("boundsMin") || !target.contains("boundsMax") ||
                !target.contains("grid"))
            {
                error = "Probe volume requires nodeId, boundsMin, boundsMax and grid";
                return false;
            }
            const auto& id = target["nodeId"];
            if ((!id.is_number_unsigned() &&
                 (!id.is_number_integer() || id.get<std::int64_t>() <= 0)) ||
                id.get<std::uint64_t>() == 0)
            {
                error = "Probe nodeId must be a positive integer";
                return false;
            }
            ProbeBakeTarget output;
            output.node_id = id.get<std::uint64_t>();
            for (const char* name : {"boundsMin", "boundsMax", "grid"})
            {
                if (!target[name].is_array() || target[name].size() != 3)
                {
                    error = "Probe bounds and grid require three numbers";
                    return false;
                }
            }
            std::array<double, 3> lo{}, hi{};
            for (std::size_t a = 0; a < 3; ++a)
            {
                const auto& l = target["boundsMin"][a];
                const auto& h = target["boundsMax"][a];
                const auto& g = target["grid"][a];
                if (!l.is_number() || !h.is_number() || !g.is_number_integer() ||
                    !std::isfinite(l.get<double>()) || !std::isfinite(h.get<double>()) ||
                    std::abs(l.get<double>()) > 1e6 || std::abs(h.get<double>()) > 1e6 ||
                    l.get<double>() >= h.get<double>() || g.get<double>() < 2 ||
                    g.get<double>() > 64)
                {
                    error = "Probe bounds must be ordered and grid dimensions must be 2..64";
                    return false;
                }
                lo[a] = l.get<double>();
                hi[a] = h.get<double>();
                output.grid[a] = g.get<std::uint32_t>();
            }
            output.bounds_min = schema::Vec3(lo[0], lo[1], lo[2]);
            output.bounds_max = schema::Vec3(hi[0], hi[1], hi[2]);
            settings.probes.push_back(output);
        }
    }
    return true;
}
} // namespace

struct SceneBakeSystem::State
{
    ResourceSystem& resources;
    RenderSystem& renderer;
    TaskSystem& tasks;
    WorldSystem& world;
    std::filesystem::path root;
    ValidateCallback validate;
    PublishCallback publish;
    std::unique_ptr<IblBakeJob> gpu;
    std::unique_ptr<LightingBakeJob> lighting;
    bool invalidated = false;
    SceneBakeFiles files;
    editor::resource_access::ResourceReference source;
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
                {"completed", lighting ? lighting->completed()
                              : gpu    ? gpu->completed()
                                       : 0},
                {"total", lighting ? lighting->total()
                          : gpu    ? gpu->total()
                                   : 0},
                {"outputs", std::move(outputs)},
                {"error", error}};
    }
};

SceneBakeSystem::SceneBakeSystem(ResourceSystem& resources, RenderSystem& renderer,
                                 TaskSystem& tasks, WorldSystem& world,
                                 std::filesystem::path project_root, ValidateCallback validate,
                                 PublishCallback publish)
    : state_(std::make_unique<State>(resources, renderer, tasks, world, std::move(project_root),
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
                   if (!arguments.is_object() || !arguments.contains("scene") ||
                       !arguments["scene"].is_string() || !arguments.contains("effects"))
                   {
                       error = "bake_scene requires scene and effects";
                       return false;
                   }
                   const bool lighting = arguments["effects"] == Json::array({"lighting"});
                   const bool ibl = arguments["effects"] == Json::array({"ibl"});
                   if (!lighting && !ibl)
                   {
                       error = "effects must be [ibl] or [lighting]";
                       return false;
                   }
                   for (const auto& item : arguments.items())
                   {
                       const std::set<std::string> common{"scene", "effects", "resolution"};
                       const std::set<std::string> options =
                           lighting ? std::set<std::string>{"hemisphereResolution",
                                                            "bounces",
                                                            "interpolationPasses",
                                                            "interpolationThreshold",
                                                            "lightNodeIds",
                                                            "probeVolumes"}
                                    : std::set<std::string>{"environment", "samples", "lutSize"};
                       if (!common.contains(item.key()) && !options.contains(item.key()))
                       {
                           error = "Unknown scene baking argument: " + item.key();
                           return false;
                       }
                   }
                   if (lighting)
                   {
                       LightingBakeSettings settings;
                       if (!lighting_settings(arguments, settings, error))
                       {
                           return false;
                       }
                       const auto scene = arguments["scene"].get<std::string>();
                       std::uint64_t scene_id = 0;
                       if (!s.validate(scene, scene_id, error))
                       {
                           return false;
                       }
                       s.files.cleanup();
                       s.files = {};
                       s.gpu.reset();
                       s.lighting.reset();
                       s.source.reset();
                       s.job = editor::asset_pipeline::new_guid();
                       s.scene = scene;
                       s.scene_id = scene_id;
                       s.error.clear();
                       s.status = "failed";
                       s.invalidated = false;
                       schema::SceneDataT data;
                       if (!s.files.initialize_lighting(s.root, scene, s.job, s.resources, settings,
                                                        error) ||
                           !editor::SceneImporter::read(
                               s.root / "assets" / PathUtils::from_utf8(scene), data, error))
                       {
                           s.error = error;
                           s.files.cleanup();
                           return false;
                       }
                       s.lighting =
                           std::make_unique<LightingBakeJob>(s.renderer, s.resources, s.tasks);
                       if (!s.world.scene() ||
                           !s.lighting->start(data, *s.world.scene(), settings, error))
                       {
                           s.error = error;
                           s.files.cleanup();
                           return false;
                       }
                       s.status = "running";
                       result = s.report();
                       return true;
                   }
                   if (!arguments.contains("environment") || !arguments["environment"].is_string())
                   {
                       error = "IBL baking requires environment";
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
                   s.lighting.reset();
                   s.invalidated = false;
                   s.source.reset();
                   s.job = editor::asset_pipeline::new_guid();
                   s.scene = scene;
                   s.scene_id = scene_id;
                   s.error.clear();
                   s.status = "failed";
                   Guid guid;
                   if (!s.files.initialize(s.root, scene, input, s.job, s.resources, error) ||
                       !s.resources.catalog().identities().path_to_guid(input, guid) ||
                       !editor::resource_access::load(s.world.resource_loader(), guid, s.source) ||
                       !s.source || s.source.data<TextureResource>() == nullptr)
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
                   if (!s.gpu->start(*s.source.data<TextureResource>(), resolution, samples,
                                     lut_size, error))
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
                       (state_->gpu == nullptr && state_->lighting == nullptr))
                   {
                       error = "cancel_scene_bake requires the current job identity";
                       return false;
                   }
                   if (state_->busy())
                   {
                       if (state_->gpu)
                       {
                           state_->gpu->cancel();
                       }
                       if (state_->lighting)
                       {
                           state_->lighting->cancel();
                       }
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
    if (!s.busy() || (!s.gpu && !s.lighting))
    {
        return true;
    }
    if (s.lighting && !s.invalidated && s.status == "running")
    {
        std::uint64_t current = 0;
        if (!s.validate(s.scene, current, s.error) || current != s.scene_id)
        {
            if (s.error.empty())
            {
                s.error = "Scene changed while lighting capture was running";
            }
            s.invalidated = true;
            s.lighting->cancel();
        }
    }
    const auto progress =
        s.lighting ? s.lighting->tick(s.error) : s.gpu->tick(s.renderer.frame_number(), s.error);
    if (progress == BakeProgress::running)
    {
        return true;
    }
    if (progress == BakeProgress::cancelled || progress == BakeProgress::failed)
    {
        s.status = progress == BakeProgress::cancelled && !s.invalidated ? "cancelled" : "failed";
        s.files.cleanup();
        s.source.reset();
        return true;
    }
    s.status = "publishing";
    std::uint64_t scene_id = 0;
    if (!s.validate(s.scene, scene_id, s.error) || scene_id != s.scene_id ||
        !s.files.unchanged(s.resources, s.error) ||
        !(s.lighting ? s.files.stage_lighting(s.lighting->outputs(), s.error)
                     : s.files.stage(s.gpu->outputs(), s.error)) ||
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
    if (state_->lighting)
    {
        state_->lighting->shutdown();
    }
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
