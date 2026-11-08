#include "runtime/render/render_settings.hpp"
#include "runtime/scene/ecs_scene.hpp"
#include "runtime/scene/ecs_storage.hpp"

#include "runtime/render/render_constants.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/render/scene_gpu_resources.hpp"
#include "runtime/render/scene_render_settings.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/post_process_component.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/world/world_system.hpp"

#include <SDL3/SDL_log.h>
#include <algorithm>
#include <bgfx/bgfx.h>
#include <cmath>

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
    auto& world = scene.world();
    if (!world.refresh_scene_state())
    {
        return false;
    }
    bool valid = true;
    auto environments =
        flecs::world(world.native_world())
            .query_builder<const ecs_detail::ComponentStorage<EnvironmentComponent::Data>,
                           const ecs_detail::NodeRuntime>()
            .with<ecs_detail::Published>()
            .build();
    environments.each(
        [&](flecs::entity entity, const auto& stored, const ecs_detail::NodeRuntime& state)
        {
            if (state.scene != &scene || !state.active || !state.visible_in_hierarchy ||
                !stored.state.flags.enabled || !stored.state.flags.visible)
            {
                return;
            }
            if (output.environment)
            {
                valid = false;
                return;
            }
            output.environment = world.find_node(entity.id()).get_component<EnvironmentComponent>();
        });
    EnvironmentSettings environment;
    if (output.environment && !output.environment.get_settings(environment))
    {
        output = {};
        return false;
    }
    const auto atmosphere_sun_id = environment.atmosphere_sun_node_id;
    Light first_directional;
    std::uint64_t first_order = UINT64_MAX;
    auto lights = flecs::world(world.native_world())
                      .query_builder<const ecs_detail::ComponentStorage<Light::Data>,
                                     const ecs_detail::NodeRuntime>()
                      .with<ecs_detail::Published>()
                      .build();
    lights.each(
        [&](flecs::entity entity, const auto& stored, const ecs_detail::NodeRuntime& state)
        {
            if (state.scene != &scene || !state.active || !state.visible_in_hierarchy ||
                !stored.state.flags.enabled || !stored.state.flags.visible ||
                stored.data.kind != schema::LightKind::directional)
            {
                return;
            }
            const auto light = world.find_node(entity.id()).get_component<Light>();
            if (atmosphere_sun_id != 0)
            {
                const auto* properties = entity.try_get<schema::NodePropertiesT>();
                if (properties && atmosphere_sun_id == properties->id)
                {
                    output.atmosphere_sun = light;
                }
            }
            if (state.order < first_order)
            {
                first_order = state.order;
                first_directional = light;
            }
            if (stored.data.cast_shadows)
            {
                if (output.shadow_light)
                {
                    valid = false;
                    return;
                }
                output.shadow_light = light;
            }
        });
    if (!valid)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Scene contains multiple enabled environment "
                                              "components or shadow-casting directional lights");
        output = {};
        return false;
    }
    if (!output.shadow_light)
    {
        output.shadow_light = first_directional;
    }
    // 独立观察镜头只决定观察位置；后处理始终来自世界场景的活动相机。
    if (auto camera = scene.active_camera();
        camera && camera.active() && camera.visible_in_hierarchy())
    {
        auto post_process = camera.node().get_component<PostProcessComponent>();
        if (post_process && post_process.active() && post_process.visible_in_hierarchy())
        {
            output.post_process = post_process;
        }
    }
    return true;
}

RenderSettings render_settings(const SceneRenderComponents& components)
{
    RenderSettings settings;
    EnvironmentSettings environment;
    if (components.environment.get_settings(environment))
    {
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
        settings.atmosphere = environment;
        settings.atmosphere_scene_id = components.environment.node().scene()->id();
    }
    LightSettings light;
    const bool has_light = components.shadow_light.get_settings(light);
    LightSettings sun;
    if (components.atmosphere_sun.get_settings(sun) &&
        (!has_light || !light.cast_shadows || components.shadow_light == components.atmosphere_sun))
    {
        float matrix[16]{};
        const auto transform = components.atmosphere_sun.node().get_component<Transform>();
        if (transform && transform.world_matrix(matrix))
        {
            const float length = std::hypot(matrix[8], matrix[9], matrix[10]);
            if (std::isfinite(length) && length > 0)
            {
                settings.atmosphere_sun_valid = true;
                settings.atmosphere_sun_direction = {-matrix[8] / length, -matrix[9] / length,
                                                     -matrix[10] / length};
                settings.atmosphere_sun_radiance = {sun.color.r() * sun.intensity,
                                                    sun.color.g() * sun.intensity,
                                                    sun.color.b() * sun.intensity};
            }
        }
    }
    PostProcessSettings post_process;
    if (components.post_process.get_settings(post_process))
    {
        settings.exposure = post_process.exposure;
        settings.filmic_tonemap = post_process.filmic_tonemap;
        settings.hdr_enabled = post_process.hdr_enabled;
        settings.bloom_enabled = post_process.bloom_enabled;
        settings.bloom_threshold = post_process.bloom_threshold;
        settings.bloom_intensity = post_process.bloom_intensity;
        settings.bloom_radius = post_process.bloom_radius;
        settings.ssr_enabled = post_process.ssr_enabled;
        settings.ssr_resolution_scale = post_process.ssr_resolution_scale;
        settings.ssr_max_steps = post_process.ssr_max_steps;
        settings.ssr_max_distance = post_process.ssr_max_distance;
        settings.ssr_thickness = post_process.ssr_thickness;
        settings.ssr_roughness_cutoff = post_process.ssr_roughness_cutoff;
        settings.ao_enabled = post_process.ao_enabled;
        settings.ao_radius = post_process.ao_radius;
        settings.ao_strength = post_process.ao_strength;
        settings.ao_power = post_process.ao_power;
        settings.ao_fade_start = post_process.ao_fade_start;
        settings.ao_fade_end = post_process.ao_fade_end;
    }
    if (has_light)
    {
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
    settings.atmosphere_submitted =
        settings.atmosphere.atmosphere_enabled && settings.atmosphere_sun_valid &&
        scene_resources_ &&
        scene_resources_->atmosphere.submitted_for(settings.atmosphere_scene_id) &&
        std::any_of(scene_resources_->view_stats.begin(), scene_resources_->view_stats.end(),
                    [this](const auto& view)
                    {
                        return view.view_id == render_detail::main_view &&
                               view.frame_number == frame_number_ && view.frame_number != 0;
                    });
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
