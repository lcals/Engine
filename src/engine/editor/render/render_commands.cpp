#include "editor/render/render_commands.hpp"

#include "editor/commands/editor_commands.hpp"
#include "editor/commands/editor_edit.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/post_process_component.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/world/world_system.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace engine::editor
{
namespace
{
using Json = nlohmann::json;
namespace fs = std::filesystem;

const char* fog_mode_name(FogMode mode)
{
    switch (mode)
    {
    case FogMode::linear:
        return "linear";
    case FogMode::exponential:
        return "exponential";
    case FogMode::exponential_squared:
        return "exponential_squared";
    default:
        return "none";
    }
}

Json settings_json(const RenderSettings& value)
{
    std::array<double, 4> clear{};
    for (std::size_t index = 0; index < 4; ++index)
    {
        clear[index] = static_cast<double>((value.clear_color >> (24 - index * 8)) & 255) / 255.0;
    }
    return {{"ambientColor", value.ambient_color},
            {"ambientIntensity", value.ambient_intensity},
            {"iblEnabled", value.ibl_enabled},
            {"iblIntensity", value.ibl_intensity},
            {"ambientTricolor", value.ambient_tricolor},
            {"ambientSkyColor", value.ambient_sky_color},
            {"ambientEquatorColor", value.ambient_equator_color},
            {"ambientGroundColor", value.ambient_ground_color},
            {"linearFog", value.fog_mode == FogMode::linear},
            {"fogMode", fog_mode_name(value.fog_mode)},
            {"fogColor", value.fog_color},
            {"fogStart", value.fog_start},
            {"fogEnd", value.fog_end},
            {"fogDensity", value.fog_density},
            {"exposure", value.exposure},
            {"filmicTonemap", value.filmic_tonemap},
            {"aoEnabled", value.ao_enabled},
            {"aoRadius", value.ao_radius},
            {"aoStrength", value.ao_strength},
            {"aoPower", value.ao_power},
            {"aoFadeStart", value.ao_fade_start},
            {"aoFadeEnd", value.ao_fade_end},
            {"hdrEnabled", value.hdr_enabled},
            {"bloomEnabled", value.bloom_enabled},
            {"bloomThreshold", value.bloom_threshold},
            {"bloomIntensity", value.bloom_intensity},
            {"bloomRadius", value.bloom_radius},
            {"directionalShadows", value.directional_shadows},
            {"shadowResolution", value.shadow_resolution},
            {"shadowBias", value.shadow_bias},
            {"shadowCascades", value.shadow_cascades},
            {"shadowDistance", value.shadow_distance},
            {"shadowSplitLambda", value.shadow_split_lambda},
            {"shadowCascadeBlend", value.shadow_cascade_blend},
            {"clearColor", clear}};
}

bool read_settings(const Json& arguments, RenderSettings& settings, std::string& error)
{
    if (!arguments.is_object())
    {
        error = "Render settings must be an object";
        return false;
    }
    const auto allowed = settings_json(settings);
    for (auto item = arguments.begin(); item != arguments.end(); ++item)
    {
        if (!allowed.contains(item.key()))
        {
            error = "Unknown render setting: " + item.key();
            return false;
        }
        if (item.key() == "ambientColor" || item.key() == "clearColor" ||
            item.key() == "ambientSkyColor" || item.key() == "ambientEquatorColor" ||
            item.key() == "ambientGroundColor" || item.key() == "fogColor")
        {
            const auto count = item.key() == "clearColor" ? 4U : 3U;
            if (!item->is_array() || item->size() != count)
            {
                error = "Render color requires the declared component count";
                return false;
            }
            std::uint32_t packed = 0;
            for (std::size_t index = 0; index < count; ++index)
            {
                const auto& component = (*item)[index];
                if (!component.is_number())
                {
                    error = "Render color components must be numbers";
                    return false;
                }
                const double value = component.get<double>();
                if (!std::isfinite(value) || value < 0 || value > 1)
                {
                    error = "Render color components must be within 0..1";
                    return false;
                }
                if (count == 3)
                {
                    auto& color =
                        item.key() == "ambientSkyColor"       ? settings.ambient_sky_color
                        : item.key() == "ambientEquatorColor" ? settings.ambient_equator_color
                        : item.key() == "ambientGroundColor"  ? settings.ambient_ground_color
                        : item.key() == "fogColor"            ? settings.fog_color
                                                              : settings.ambient_color;
                    color[index] = static_cast<float>(value);
                }
                else
                {
                    packed = (packed << 8) | static_cast<std::uint32_t>(std::lround(value * 255));
                }
            }
            if (count == 4)
            {
                settings.clear_color = packed;
            }
        }
        else if (item.key() == "fogMode")
        {
            if (*item == "none")
            {
                settings.fog_mode = FogMode::none;
            }
            else if (*item == "linear")
            {
                settings.fog_mode = FogMode::linear;
            }
            else if (*item == "exponential")
            {
                settings.fog_mode = FogMode::exponential;
            }
            else if (*item == "exponential_squared")
            {
                settings.fog_mode = FogMode::exponential_squared;
            }
            else
            {
                error = "Unknown fog mode";
                return false;
            }
        }
        else if (item.key() == "linearFog")
        {
            if (!item->is_boolean())
            {
                error = "linearFog must be boolean";
                return false;
            }
            if (!arguments.contains("fogMode"))
            {
                settings.fog_mode = item->get<bool>() ? FogMode::linear : FogMode::none;
            }
        }
        else if (item.key() == "filmicTonemap" || item.key() == "directionalShadows" ||
                 item.key() == "ambientTricolor" || item.key() == "iblEnabled" ||
                 item.key() == "aoEnabled" || item.key() == "hdrEnabled" ||
                 item.key() == "bloomEnabled")
        {
            if (!item->is_boolean())
            {
                error = "Render option must be boolean";
                return false;
            }
            bool& value = item.key() == "filmicTonemap"     ? settings.filmic_tonemap
                          : item.key() == "ambientTricolor" ? settings.ambient_tricolor
                          : item.key() == "iblEnabled"      ? settings.ibl_enabled
                          : item.key() == "aoEnabled"       ? settings.ao_enabled
                          : item.key() == "hdrEnabled"      ? settings.hdr_enabled
                          : item.key() == "bloomEnabled"    ? settings.bloom_enabled
                                                            : settings.directional_shadows;
            value = item->get<bool>();
        }
        else if (item.key() == "shadowResolution")
        {
            if (!item->is_number_integer() || item->get<double>() < 1 ||
                item->get<double>() > std::numeric_limits<std::uint16_t>::max())
            {
                error = "Invalid shadow resolution";
                return false;
            }
            settings.shadow_resolution = item->get<std::uint16_t>();
        }
        else if (item.key() == "shadowCascades")
        {
            if (!item->is_number_integer() || (*item != 1 && *item != 2 && *item != 4))
            {
                error = "shadowCascades must be an integer: 1, 2 or 4";
                return false;
            }
            settings.shadow_cascades = item->get<std::uint8_t>();
        }
        else
        {
            if (!item->is_number() || !std::isfinite(item->get<double>()) ||
                std::abs(item->get<double>()) > std::numeric_limits<float>::max())
            {
                error = "Render setting requires a finite float";
                return false;
            }
            float& value = item.key() == "ambientIntensity"     ? settings.ambient_intensity
                           : item.key() == "iblIntensity"       ? settings.ibl_intensity
                           : item.key() == "exposure"           ? settings.exposure
                           : item.key() == "aoRadius"           ? settings.ao_radius
                           : item.key() == "aoStrength"         ? settings.ao_strength
                           : item.key() == "aoPower"            ? settings.ao_power
                           : item.key() == "aoFadeStart"        ? settings.ao_fade_start
                           : item.key() == "aoFadeEnd"          ? settings.ao_fade_end
                           : item.key() == "bloomThreshold"     ? settings.bloom_threshold
                           : item.key() == "bloomIntensity"     ? settings.bloom_intensity
                           : item.key() == "bloomRadius"        ? settings.bloom_radius
                           : item.key() == "fogStart"           ? settings.fog_start
                           : item.key() == "fogEnd"             ? settings.fog_end
                           : item.key() == "fogDensity"         ? settings.fog_density
                           : item.key() == "shadowDistance"     ? settings.shadow_distance
                           : item.key() == "shadowSplitLambda"  ? settings.shadow_split_lambda
                           : item.key() == "shadowCascadeBlend" ? settings.shadow_cascade_blend
                                                                : settings.shadow_bias;
            value = item->get<float>();
        }
    }
    if (arguments.contains("fogMode") && arguments.contains("linearFog") &&
        arguments["linearFog"].get<bool>() != (settings.fog_mode == FogMode::linear))
    {
        error = "linearFog and fogMode disagree";
        return false;
    }
    return true;
}

class SettingsEdit final : public EditorEdit
{
public:
    SettingsEdit(RenderSystem& renderer, WorldSystem& world, RenderSettings before,
                 RenderSettings after, const Json& arguments)
        : renderer_(renderer), world_(world), before_(before), after_(after),
          scene_id_(world.scene() == nullptr ? 0 : world.scene()->id())
    {
        for (const auto& item : arguments.items())
        {
            if (item.key() == "exposure" || item.key() == "filmicTonemap" ||
                item.key() == "aoEnabled" || item.key() == "aoRadius" ||
                item.key() == "aoStrength" || item.key() == "aoPower" ||
                item.key() == "aoFadeStart" || item.key() == "aoFadeEnd" ||
                item.key() == "hdrEnabled" || item.key() == "bloomEnabled" ||
                item.key() == "bloomThreshold" || item.key() == "bloomIntensity" ||
                item.key() == "bloomRadius")
            {
                post_process_ = true;
            }
            else if (item.key() == "directionalShadows" || item.key() == "shadowResolution" ||
                     item.key() == "shadowBias" || item.key() == "shadowCascades" ||
                     item.key() == "shadowDistance" || item.key() == "shadowSplitLambda" ||
                     item.key() == "shadowCascadeBlend")
            {
                shadows_ = true;
            }
            else
            {
                environment_ = true;
            }
        }
    }

    bool affects_scene() const noexcept override
    {
        return scene_id_ != 0;
    }

    bool apply(std::string& error) override
    {
        auto* scene = world_.scene();
        if ((scene == nullptr ? 0 : scene->id()) != scene_id_ ||
            settings_json(renderer_.settings()) != settings_json(before_))
        {
            error = "Render settings or the active scene changed outside this edit";
            return false;
        }
        records_.clear();
        if (scene != nullptr && !prepare_components(*scene, error))
        {
            if (!restore_records(*scene))
            {
                error += "; cannot restore scene component setup";
            }
            return false;
        }
        if (!renderer_.set_settings(after_))
        {
            error =
                "Invalid scene render settings, missing camera/light, or conflicting components";
            if (scene != nullptr && !restore_records(*scene))
            {
                error += "; cannot restore scene component setup";
            }
            return false;
        }
        for (auto& record : records_)
        {
            const auto* node = scene->find_node(record.node_id);
            const auto* component = node == nullptr ? nullptr : node->get_component(record.type);
            if (component == nullptr || !component->build_data(record.after))
            {
                error = "Cannot snapshot the edited scene component";
                if (!restore_records(*scene))
                {
                    error += "; cannot restore scene component settings";
                }
                return false;
            }
        }
        return true;
    }

    bool revert(std::string& error) override
    {
        auto* scene = world_.scene();
        if ((scene == nullptr ? 0 : scene->id()) != scene_id_)
        {
            error = "The edited scene is no longer active";
            return false;
        }
        if (scene == nullptr)
        {
            if (settings_json(renderer_.settings()) != settings_json(after_) ||
                !renderer_.set_settings(before_))
            {
                error = "Idle render settings changed outside this edit";
                return false;
            }
            return true;
        }
        for (const auto& record : records_)
        {
            const auto* node = scene->find_node(record.node_id);
            const auto* component = node == nullptr ? nullptr : node->get_component(record.type);
            schema::ComponentDataT snapshot;
            if (component == nullptr || !component->build_data(snapshot) ||
                snapshot != record.after)
            {
                error = "Scene render component changed outside this edit";
                return false;
            }
            if (record.created_node && !can_remove_environment(*scene, *node))
            {
                error = "The automatically created environment node has other edits";
                return false;
            }
        }
        if (!restore_records(*scene))
        {
            error = "Cannot restore scene render components";
            return false;
        }
        return true;
    }

private:
    struct Record
    {
        std::uint64_t node_id = 0;
        ComponentType type = ComponentType::NONE;
        bool created_node = false;
        std::optional<schema::ComponentDataT> before;
        schema::ComponentDataT after;
    };

    static bool can_remove_environment(const Scene& scene, const Node& node)
    {
        if (node.parent() != nullptr || node.components().size() != 2)
        {
            return false;
        }
        const auto* transform = node.get_component<Transform>();
        if (transform == nullptr || !transform->enabled() ||
            transform->settings() != TransformSettings{})
        {
            return false;
        }
        for (const auto& candidate : scene.nodes())
        {
            if (candidate->parent() == &node)
            {
                return false;
            }
        }
        return true;
    }

    bool record(Component& component, bool created_component, bool created_node, std::string& error)
    {
        Record value;
        value.node_id = component.node().id();
        value.type = component.type();
        value.created_node = created_node;
        if (!created_component)
        {
            value.before.emplace();
            if (!component.build_data(*value.before))
            {
                error = "Cannot snapshot the original scene component";
                return false;
            }
        }
        records_.push_back(std::move(value));
        return true;
    }

    bool prepare_components(Scene& scene, std::string& error)
    {
        if (environment_)
        {
            EnvironmentComponent* environment = nullptr;
            for (const auto& node : scene.nodes())
            {
                auto* candidate = node->get_component<EnvironmentComponent>();
                if (candidate != nullptr && candidate->enabled())
                {
                    if (environment != nullptr)
                    {
                        error = "The scene has more than one enabled environment component";
                        return false;
                    }
                    environment = candidate;
                }
            }
            bool created = false;
            if (environment == nullptr)
            {
                auto* node = scene.create_node("Environment");
                if (node == nullptr)
                {
                    error = "Cannot create the scene environment node";
                    return false;
                }
                environment = node->add_component<EnvironmentComponent>();
                if (environment == nullptr)
                {
                    error = "Cannot create the scene environment component";
                    if (!scene.destroy_node(node->id()))
                    {
                        error += "; cannot remove the incomplete node";
                    }
                    return false;
                }
                created = true;
            }
            if (!record(*environment, created, created, error))
            {
                return false;
            }
        }
        if (post_process_)
        {
            auto* camera = scene.active_camera();
            if (camera == nullptr)
            {
                error = "Post-processing requires an active scene camera";
                return false;
            }
            auto* component = camera->node().get_component<PostProcessComponent>();
            const bool created = component == nullptr;
            if (created)
            {
                component = camera->node().add_component<PostProcessComponent>();
            }
            if (component == nullptr || !component->enabled())
            {
                error = "The active camera needs an enabled post-process component";
                return false;
            }
            if (!record(*component, created, false, error))
            {
                return false;
            }
        }
        if (shadows_)
        {
            Light* first = nullptr;
            Light* caster = nullptr;
            for (const auto& node : scene.nodes())
            {
                auto* light = node->get_component<Light>();
                if (light == nullptr || !light->enabled() ||
                    light->settings().kind != schema::LightKind::directional)
                {
                    continue;
                }
                if (first == nullptr)
                {
                    first = light;
                }
                if (light->settings().cast_shadows)
                {
                    if (caster != nullptr)
                    {
                        error = "Only one directional shadow caster is supported";
                        return false;
                    }
                    caster = light;
                }
            }
            auto* light = caster != nullptr ? caster : first;
            if (light == nullptr)
            {
                if (!after_.directional_shadows &&
                    after_.shadow_resolution == before_.shadow_resolution &&
                    after_.shadow_bias == before_.shadow_bias &&
                    after_.shadow_cascades == before_.shadow_cascades &&
                    after_.shadow_distance == before_.shadow_distance &&
                    after_.shadow_split_lambda == before_.shadow_split_lambda &&
                    after_.shadow_cascade_blend == before_.shadow_cascade_blend)
                {
                    return true;
                }
                error = "Shadow settings require an enabled directional light in the scene";
                return false;
            }
            if (!record(*light, false, false, error))
            {
                return false;
            }
        }
        return true;
    }

    bool restore_records(Scene& scene)
    {
        bool restored = true;
        for (auto item = records_.rbegin(); item != records_.rend(); ++item)
        {
            auto* node = scene.find_node(item->node_id);
            auto* component = node == nullptr ? nullptr : node->get_component(item->type);
            if (node == nullptr || component == nullptr)
            {
                restored = false;
            }
            else if (item->created_node)
            {
                restored = scene.destroy_node(item->node_id) && restored;
            }
            else if (!item->before)
            {
                restored = node->remove_component(item->type) && restored;
            }
            else
            {
                restored = component->restore_data(*item->before) && restored;
            }
        }
        return restored;
    }

    RenderSystem& renderer_;
    WorldSystem& world_;
    RenderSettings before_;
    RenderSettings after_;
    std::uint64_t scene_id_ = 0;
    std::vector<Record> records_;
    bool environment_ = false;
    bool post_process_ = false;
    bool shadows_ = false;
};

const char* capture_state(ScreenshotStatus status)
{
    switch (status)
    {
    case ScreenshotStatus::pending:
        return "pending";
    case ScreenshotStatus::complete:
        return "complete";
    case ScreenshotStatus::failed:
        return "failed";
    default:
        return "idle";
    }
}
} // namespace

RenderCommands::RenderCommands(RenderSystem& renderer, WorldSystem& world, fs::path capture_root)
    : renderer_(renderer), world_(world), capture_root_(std::move(capture_root))
{
}

bool RenderCommands::register_commands(EditorCommands& commands)
{
    return commands.register_command("get_render_settings", false,
                                     [this](const Json& arguments, Json& result,
                                            std::unique_ptr<EditorEdit>&, std::string& error)
                                     {
                                         if (!arguments.empty())
                                         {
                                             error = "get_render_settings has no arguments";
                                             return false;
                                         }
                                         result = settings_json(renderer_.settings());
                                         return true;
                                     }) &&
           commands.register_command(
               "set_render_settings", true,
               [this](const Json& arguments, Json& result, std::unique_ptr<EditorEdit>& edit,
                      std::string& error)
               {
                   const auto before = renderer_.settings();
                   auto after = before;
                   if (arguments.empty() || !read_settings(arguments, after, error))
                   {
                       if (error.empty())
                       {
                           error = "At least one render setting is required";
                       }
                       return false;
                   }
                   edit =
                       std::make_unique<SettingsEdit>(renderer_, world_, before, after, arguments);
                   auto resolved_after = after;
                   // 关闭非首方向灯的阴影后，查询会采用首方向灯的非活动阴影参数。
                   if (world_.scene() != nullptr && before.directional_shadows &&
                       !after.directional_shadows)
                   {
                       for (const auto& node : world_.scene()->nodes())
                       {
                           const auto* light = node->get_component<Light>();
                           if (light == nullptr || !light->enabled() ||
                               light->settings().kind != schema::LightKind::directional)
                           {
                               continue;
                           }
                           if (!light->settings().cast_shadows)
                           {
                               resolved_after.shadow_resolution =
                                   light->settings().shadow_resolution;
                               resolved_after.shadow_bias = light->settings().shadow_bias;
                               resolved_after.shadow_cascades = light->settings().shadow_cascades;
                               resolved_after.shadow_distance = light->settings().shadow_distance;
                               resolved_after.shadow_split_lambda =
                                   light->settings().shadow_split_lambda;
                               resolved_after.shadow_cascade_blend =
                                   light->settings().shadow_cascade_blend;
                           }
                           break;
                       }
                   }
                   result = settings_json(resolved_after);
                   return true;
               }) &&
           commands.register_command(
               "set_render_stats", false,
               [this](const Json& arguments, Json& result, std::unique_ptr<EditorEdit>&,
                      std::string& error)
               {
                   if (!arguments.is_object() || arguments.size() != 1 ||
                       !arguments.contains("enabled") || !arguments["enabled"].is_boolean())
                   {
                       error = "set_render_stats requires an enabled boolean";
                       return false;
                   }
                   renderer_.set_stats_visible(arguments["enabled"].get<bool>());
                   result = {{"enabled", renderer_.stats_visible()}};
                   return true;
               }) &&
           commands.register_command(
               "capture_frame", false,
               [this](const Json& arguments, Json& result, std::unique_ptr<EditorEdit>&,
                      std::string& error)
               {
                   if (arguments.size() != 1 || !arguments.contains("path") ||
                       !arguments["path"].is_string())
                   {
                       error = "capture_frame requires a path relative to the project cache";
                       return false;
                   }
                   const auto& text = arguments["path"].get_ref<const std::string&>();
                   const auto relative = PathUtils::from_utf8(text);
                   if (text.empty() || text.find('\0') != std::string::npos ||
                       text.find('\\') != std::string::npos || relative.is_absolute() ||
                       relative.has_root_name() || relative.extension() != ".png")
                   {
                       error = "Screenshot path must be a relative .png filename";
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
                   std::error_code code;
                   auto destination = fs::canonical(capture_root_, code);
                   if (code)
                   {
                       error = "Screenshot cache directory is unavailable";
                       return false;
                   }
                   for (const auto& part : relative)
                   {
                       destination /= part;
                       const auto status = fs::symlink_status(destination, code);
                       if (code == std::errc::no_such_file_or_directory)
                       {
                           code.clear();
                       }
                       if (code || fs::is_symlink(status))
                       {
                           error = "Screenshot path contains a symlink or inaccessible entry";
                           return false;
                       }
                   }
                   fs::create_directories(destination.parent_path(), code);
                   if (code || !renderer_.request_screenshot(PathUtils::to_utf8(destination)))
                   {
                       error = "Renderer cannot accept this screenshot request";
                       return false;
                   }
                   capture_path_ = destination;
                   result = {{"state", "pending"}, {"path", PathUtils::to_utf8(capture_path_)}};
                   return true;
               }) &&
           commands.register_command(
               "get_frame_capture", false,
               [this](const Json& arguments, Json& result, std::unique_ptr<EditorEdit>&,
                      std::string& error)
               {
                   if (!arguments.empty())
                   {
                       error = "get_frame_capture has no arguments";
                       return false;
                   }
                   result = {{"state", capture_state(renderer_.screenshot_status())},
                             {"path", PathUtils::to_utf8(capture_path_)}};
                   return true;
               });
}
} // namespace engine::editor
