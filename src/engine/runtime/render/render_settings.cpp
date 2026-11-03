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

namespace engine
{
namespace
{

std::array<float, 3> color_channels(const Color& color)
{
    return {color.r(), color.g(), color.b()};
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
        if (environment != nullptr && environment->active() && environment->visible_in_hierarchy())
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
        if (light == nullptr || !light->active() || !light->visible_in_hierarchy() ||
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
    if (auto* camera = scene.active_camera();
        camera != nullptr && camera->active() && camera->visible_in_hierarchy())
    {
        auto* post_process = camera->node().get_component<PostProcessComponent>();
        if (post_process != nullptr && post_process->active() &&
            post_process->visible_in_hierarchy())
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
        settings.ssr_enabled = components.post_process->settings().ssr_enabled;
        settings.ssr_resolution_scale = components.post_process->settings().ssr_resolution_scale;
        settings.ssr_max_steps = components.post_process->settings().ssr_max_steps;
        settings.ssr_max_distance = components.post_process->settings().ssr_max_distance;
        settings.ssr_thickness = components.post_process->settings().ssr_thickness;
        settings.ssr_roughness_cutoff = components.post_process->settings().ssr_roughness_cutoff;
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

void RenderSystem::set_idle_clear_color(std::uint32_t rgba) noexcept
{
    idle_clear_color_ = rgba;
}

std::uint32_t RenderSystem::idle_clear_color() const noexcept
{
    return idle_clear_color_;
}

bool RenderSystem::resolve_settings(RenderSettings& settings) const noexcept
{
    settings = {};
    Scene* scene = world_ != nullptr ? world_->scene() : nullptr;
    if (scene == nullptr)
    {
        settings.clear_color = idle_clear_color_;
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
