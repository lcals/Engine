#include "runtime/render/render_settings.hpp"

#include "runtime/render/render_system.hpp"
#include "runtime/render/scene_render_settings.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/post_process_component.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

#include <SDL3/SDL_log.h>
#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>

namespace engine
{
namespace
{

std::array<float, 3> color_channels(const Color& color)
{
    return {color.r(), color.g(), color.b()};
}

void update_environment(const RenderSettings& settings, EnvironmentSettings& environment)
{
    const auto color = [](const std::array<float, 3>& channels, const Color& previous)
    { return Color{channels[0], channels[1], channels[2], previous.a()}; };
    environment.ambient_color = color(settings.ambient_color, environment.ambient_color);
    environment.ambient_intensity = settings.ambient_intensity;
    environment.ambient_tricolor = settings.ambient_tricolor;
    environment.ambient_sky_color =
        color(settings.ambient_sky_color, environment.ambient_sky_color);
    environment.ambient_equator_color =
        color(settings.ambient_equator_color, environment.ambient_equator_color);
    environment.ambient_ground_color =
        color(settings.ambient_ground_color, environment.ambient_ground_color);
    environment.fog_mode = static_cast<schema::FogMode>(settings.fog_mode);
    environment.fog_color = color(settings.fog_color, environment.fog_color);
    environment.fog_start = settings.fog_start;
    environment.fog_end = settings.fog_end;
    environment.fog_density = settings.fog_density;
    environment.clear_color = settings.clear_color;
    environment.ibl_enabled = settings.ibl_enabled;
    environment.ibl_intensity = settings.ibl_intensity;
}

} // namespace

namespace render_detail
{

bool scene_render_components(Scene& scene, SceneRenderComponents& output)
{
    output = {};
    Light* first_directional = nullptr;
    for (const auto& node : scene.nodes())
    {
        auto* environment = node->get_component<EnvironmentComponent>();
        if (environment != nullptr && environment->enabled())
        {
            if (output.environment != nullptr)
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "Scene contains multiple enabled environment components");
                output = {};
                return false;
            }
            output.environment = environment;
        }
        auto* light = node->get_component<Light>();
        if (light == nullptr || !light->enabled() ||
            light->settings().kind != schema::LightKind::directional)
        {
            continue;
        }
        if (first_directional == nullptr)
        {
            first_directional = light;
        }
        if (light->settings().cast_shadows)
        {
            if (output.shadow_light != nullptr)
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "Scene contains multiple shadow-casting directional lights");
                output = {};
                return false;
            }
            output.shadow_light = light;
        }
    }
    if (output.shadow_light == nullptr)
    {
        output.shadow_light = first_directional;
    }
    // 独立观察镜头只决定观察位置；后处理始终来自世界场景的活动相机。
    if (auto* camera = scene.active_camera(); camera != nullptr && camera->enabled())
    {
        auto* post_process = camera->node().get_component<PostProcessComponent>();
        if (post_process != nullptr && post_process->enabled())
        {
            output.post_process = post_process;
        }
    }
    return true;
}

RenderSettings render_settings(const SceneRenderComponents& components)
{
    RenderSettings settings;
    if (components.environment != nullptr)
    {
        const auto& environment = components.environment->settings();
        settings.ambient_color = color_channels(environment.ambient_color);
        settings.ambient_intensity = environment.ambient_intensity;
        settings.ambient_tricolor = environment.ambient_tricolor;
        settings.ambient_sky_color = color_channels(environment.ambient_sky_color);
        settings.ambient_equator_color = color_channels(environment.ambient_equator_color);
        settings.ambient_ground_color = color_channels(environment.ambient_ground_color);
        settings.fog_mode = static_cast<FogMode>(environment.fog_mode);
        settings.fog_color = color_channels(environment.fog_color);
        settings.fog_start = environment.fog_start;
        settings.fog_end = environment.fog_end;
        settings.fog_density = environment.fog_density;
        settings.clear_color = environment.clear_color;
        settings.ibl_enabled = environment.ibl_enabled;
        settings.ibl_intensity = environment.ibl_intensity;
    }
    if (components.post_process != nullptr)
    {
        settings.exposure = components.post_process->settings().exposure;
        settings.filmic_tonemap = components.post_process->settings().filmic_tonemap;
        settings.hdr_enabled = components.post_process->settings().hdr_enabled;
        settings.bloom_enabled = components.post_process->settings().bloom_enabled;
        settings.bloom_threshold = components.post_process->settings().bloom_threshold;
        settings.bloom_intensity = components.post_process->settings().bloom_intensity;
        settings.bloom_radius = components.post_process->settings().bloom_radius;
        settings.ao_enabled = components.post_process->settings().ao_enabled;
        settings.ao_radius = components.post_process->settings().ao_radius;
        settings.ao_strength = components.post_process->settings().ao_strength;
        settings.ao_power = components.post_process->settings().ao_power;
        settings.ao_fade_start = components.post_process->settings().ao_fade_start;
        settings.ao_fade_end = components.post_process->settings().ao_fade_end;
    }
    if (components.shadow_light != nullptr)
    {
        const auto& light = components.shadow_light->settings();
        settings.directional_shadows = light.cast_shadows;
        settings.shadow_resolution = light.shadow_resolution;
        settings.shadow_bias = light.shadow_bias;
        settings.shadow_cascades = light.shadow_cascades;
        settings.shadow_distance = light.shadow_distance;
        settings.shadow_split_lambda = light.shadow_split_lambda;
        settings.shadow_cascade_blend = light.shadow_cascade_blend;
    }
    return settings;
}

} // namespace render_detail

bool RenderSystem::set_settings(const RenderSettings& settings)
{
    const auto valid_color = [](const std::array<float, 3>& color)
    {
        return std::all_of(color.begin(), color.end(), [](float value)
                           { return std::isfinite(value) && value >= 0.0F && value <= 1.0F; });
    };
    if (!valid_color(settings.ambient_color) || !valid_color(settings.ambient_sky_color) ||
        !valid_color(settings.ambient_equator_color) ||
        !valid_color(settings.ambient_ground_color) || !valid_color(settings.fog_color) ||
        !std::isfinite(settings.fog_start) || !std::isfinite(settings.fog_end) ||
        settings.fog_start < 0.0F || settings.fog_end <= settings.fog_start ||
        !std::isfinite(settings.fog_density) || settings.fog_density < 0.0F ||
        (settings.fog_mode != FogMode::none && settings.fog_mode != FogMode::linear &&
         settings.fog_mode != FogMode::exponential &&
         settings.fog_mode != FogMode::exponential_squared) ||
        !std::isfinite(settings.ambient_intensity) || settings.ambient_intensity < 0.0F ||
        settings.ambient_intensity > 1000.0F || !std::isfinite(settings.exposure) ||
        settings.exposure <= 0.0F || settings.exposure > 100.0F ||
        (settings.bloom_enabled && !settings.hdr_enabled) ||
        !std::isfinite(settings.bloom_threshold) || settings.bloom_threshold < 0.0F ||
        settings.bloom_threshold > 100.0F || !std::isfinite(settings.bloom_intensity) ||
        settings.bloom_intensity < 0.0F || settings.bloom_intensity > 10.0F ||
        !std::isfinite(settings.bloom_radius) || settings.bloom_radius < 0.25F ||
        settings.bloom_radius > 4.0F || !std::isfinite(settings.ao_radius) ||
        settings.ao_radius <= 0.0F || settings.ao_radius > 100.0F ||
        !std::isfinite(settings.ao_strength) || settings.ao_strength < 0.0F ||
        settings.ao_strength > 2.0F || !std::isfinite(settings.ao_power) ||
        settings.ao_power < 0.1F || settings.ao_power > 8.0F ||
        !std::isfinite(settings.ao_fade_start) || !std::isfinite(settings.ao_fade_end) ||
        settings.ao_fade_start < 0.0F || settings.ao_fade_end <= settings.ao_fade_start ||
        settings.ao_fade_end > 100000.0F || !std::isfinite(settings.ibl_intensity) ||
        settings.ibl_intensity < 0.0F || settings.ibl_intensity > 1000.0F ||
        !std::isfinite(settings.shadow_bias) || settings.shadow_bias < 0.0F ||
        settings.shadow_bias > 0.1F || settings.shadow_resolution < 128 ||
        settings.shadow_resolution > 8192 ||
        (settings.shadow_resolution & (settings.shadow_resolution - 1)) != 0 ||
        (settings.shadow_cascades != 1 && settings.shadow_cascades != 2 &&
         settings.shadow_cascades != 4) ||
        !std::isfinite(settings.shadow_distance) || settings.shadow_distance <= 0.0F ||
        settings.shadow_distance > 100000.0F || !std::isfinite(settings.shadow_split_lambda) ||
        settings.shadow_split_lambda < 0.0F || settings.shadow_split_lambda > 1.0F ||
        !std::isfinite(settings.shadow_cascade_blend) || settings.shadow_cascade_blend < 0.0F ||
        settings.shadow_cascade_blend > 0.3F ||
        (renderer_initialized_ && settings.directional_shadows &&
         settings.shadow_resolution > bgfx::getCaps()->limits.maxTextureSize))
    {
        return false;
    }
    Scene* scene = world_ != nullptr ? world_->scene() : nullptr;
    if (scene == nullptr)
    {
        idle_settings_ = settings;
        return true;
    }
    render_detail::SceneRenderComponents components;
    if (!render_detail::scene_render_components(*scene, components))
    {
        return false;
    }

    const EnvironmentSettings before_environment = components.environment != nullptr
                                                       ? components.environment->settings()
                                                       : EnvironmentSettings{};
    auto after_environment = before_environment;
    update_environment(settings, after_environment);
    const PostProcessSettings before_post_process = components.post_process != nullptr
                                                        ? components.post_process->settings()
                                                        : PostProcessSettings{};
    auto after_post_process = before_post_process;
    after_post_process.exposure = settings.exposure;
    after_post_process.filmic_tonemap = settings.filmic_tonemap;
    after_post_process.hdr_enabled = settings.hdr_enabled;
    after_post_process.bloom_enabled = settings.bloom_enabled;
    after_post_process.bloom_threshold = settings.bloom_threshold;
    after_post_process.bloom_intensity = settings.bloom_intensity;
    after_post_process.bloom_radius = settings.bloom_radius;
    after_post_process.ao_enabled = settings.ao_enabled;
    after_post_process.ao_radius = settings.ao_radius;
    after_post_process.ao_strength = settings.ao_strength;
    after_post_process.ao_power = settings.ao_power;
    after_post_process.ao_fade_start = settings.ao_fade_start;
    after_post_process.ao_fade_end = settings.ao_fade_end;
    const LightSettings before_light =
        components.shadow_light != nullptr ? components.shadow_light->settings() : LightSettings{};
    auto after_light = before_light;
    after_light.cast_shadows = settings.directional_shadows;
    after_light.shadow_resolution = settings.shadow_resolution;
    after_light.shadow_bias = settings.shadow_bias;
    after_light.shadow_cascades = settings.shadow_cascades;
    after_light.shadow_distance = settings.shadow_distance;
    after_light.shadow_split_lambda = settings.shadow_split_lambda;
    after_light.shadow_cascade_blend = settings.shadow_cascade_blend;

    const bool environment_changed = before_environment != after_environment;
    const bool post_process_changed = before_post_process != after_post_process;
    const bool light_changed = before_light != after_light;
    if ((environment_changed && components.environment == nullptr) ||
        (post_process_changed && components.post_process == nullptr) ||
        (light_changed && components.shadow_light == nullptr))
    {
        return false;
    }
    if (environment_changed && !components.environment->set_settings(after_environment))
    {
        return false;
    }
    if (post_process_changed && !components.post_process->set_settings(after_post_process))
    {
        if (environment_changed && !components.environment->set_settings(before_environment))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot restore previous environment settings");
        }
        return false;
    }
    if (light_changed && !components.shadow_light->set_settings(after_light))
    {
        if (post_process_changed && !components.post_process->set_settings(before_post_process))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot restore previous post-process settings");
        }
        if (environment_changed && !components.environment->set_settings(before_environment))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot restore previous environment settings");
        }
        return false;
    }
    return true;
}

bool RenderSystem::resolve_settings(RenderSettings& settings) const noexcept
{
    settings = {};
    Scene* scene = world_ != nullptr ? world_->scene() : nullptr;
    if (scene == nullptr)
    {
        settings = idle_settings_;
        return true;
    }
    render_detail::SceneRenderComponents components;
    if (!render_detail::scene_render_components(*scene, components))
    {
        return false;
    }
    settings = render_detail::render_settings(components);
    if (renderer_initialized_ && settings.directional_shadows &&
        settings.shadow_resolution > bgfx::getCaps()->limits.maxTextureSize)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Directional shadow resolution exceeds device limit");
        settings = {};
        return false;
    }
    return true;
}

RenderSettings RenderSystem::settings() const noexcept
{
    RenderSettings current;
    if (!resolve_settings(current))
    {
        return {};
    }
    return current;
}

} // namespace engine
