#include "editor/render/render_commands.hpp"

#include "editor/commands/editor_commands.hpp"
#include "editor/commands/editor_edit.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>

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
            {"directionalShadows", value.directional_shadows},
            {"shadowResolution", value.shadow_resolution},
            {"shadowBias", value.shadow_bias},
            {"clearColor", clear}};
}

bool read_settings(const Json& arguments, RenderSettings& settings, std::string& error)
{
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
                 item.key() == "ambientTricolor")
        {
            if (!item->is_boolean())
            {
                error = "Render option must be boolean";
                return false;
            }
            bool& value = item.key() == "filmicTonemap" ? settings.filmic_tonemap
                          : item.key() == "ambientTricolor" ? settings.ambient_tricolor
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
        else
        {
            if (!item->is_number() || !std::isfinite(item->get<double>()) ||
                std::abs(item->get<double>()) > std::numeric_limits<float>::max())
            {
                error = "Render setting requires a finite float";
                return false;
            }
            float& value = item.key() == "ambientIntensity" ? settings.ambient_intensity
                           : item.key() == "exposure"       ? settings.exposure
                           : item.key() == "fogStart"       ? settings.fog_start
                           : item.key() == "fogEnd"         ? settings.fog_end
                           : item.key() == "fogDensity"     ? settings.fog_density
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
    SettingsEdit(RenderSystem& renderer, RenderSettings before, RenderSettings after)
        : renderer_(renderer), before_(before), after_(after)
    {
    }
    bool affects_scene() const noexcept override
    {
        return false;
    }
    bool apply(std::string& error) override
    {
        return change(before_, after_, error);
    }
    bool revert(std::string& error) override
    {
        return change(after_, before_, error);
    }

private:
    bool change(const RenderSettings& expected, const RenderSettings& value, std::string& error)
    {
        if (settings_json(renderer_.settings()) != settings_json(expected) ||
            !renderer_.set_settings(value))
        {
            error = "Render settings changed externally or the requested settings are unsupported";
            return false;
        }
        return true;
    }
    RenderSystem& renderer_;
    RenderSettings before_;
    RenderSettings after_;
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

RenderCommands::RenderCommands(RenderSystem& renderer, fs::path capture_root)
    : renderer_(renderer), capture_root_(std::move(capture_root))
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
                   edit = std::make_unique<SettingsEdit>(renderer_, before, after);
                   result = settings_json(after);
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
