#include "runtime/render/scene_gpu_resources.hpp"
#include "runtime/scene/ecs_scene.hpp"
#include "runtime/scene/ecs_storage.hpp"

#include "runtime/render/render_constants.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/environment_component_runtime.hpp"
#include "runtime/scene/scene.hpp"

#include <SDL3/SDL.h>

#include <utility>

namespace engine
{

RenderSystem::SceneGpuResources::~SceneGpuResources() noexcept
{
    release();
}

bool RenderSystem::SceneGpuResources::initialize()
{
    release();
    frame_time_uniform =
        bgfx::createUniform("u_frame_time", bgfx::UniformFreq::Frame, bgfx::UniformType::Vec4);
    normal_transform = bgfx::createUniform("u_normal_transform", bgfx::UniformType::Mat4);
    camera_position =
        bgfx::createUniform("u_camera_position", bgfx::UniformFreq::View, bgfx::UniformType::Vec4);
    light_direction =
        bgfx::createUniform("u_light_direction", bgfx::UniformFreq::View, bgfx::UniformType::Vec4);
    light_color =
        bgfx::createUniform("u_light_color", bgfx::UniformFreq::View, bgfx::UniformType::Vec4);
    ambient = bgfx::createUniform("u_ambient", bgfx::UniformFreq::View, bgfx::UniformType::Vec4);
    ambient_sky =
        bgfx::createUniform("u_ambient_sky", bgfx::UniformFreq::View, bgfx::UniformType::Vec4);
    ambient_equator =
        bgfx::createUniform("u_ambient_equator", bgfx::UniformFreq::View, bgfx::UniformType::Vec4);
    ambient_ground =
        bgfx::createUniform("u_ambient_ground", bgfx::UniformFreq::View, bgfx::UniformType::Vec4);
    fog_color =
        bgfx::createUniform("u_fog_color", bgfx::UniformFreq::View, bgfx::UniformType::Vec4);
    fog_range =
        bgfx::createUniform("u_fog_range", bgfx::UniformFreq::View, bgfx::UniformType::Vec4);
    tonemap = bgfx::createUniform("u_tonemap", bgfx::UniformFreq::View, bgfx::UniformType::Vec4);
    frame_inputs =
        bgfx::createUniform("u_frame_inputs", bgfx::UniformFreq::View, bgfx::UniformType::Vec4);
    frame_valid = bgfx::createUniform("u_frame_valid", bgfx::UniformType::Vec4);
    planar_projection = bgfx::createUniform("u_planar_projection", bgfx::UniformType::Mat4);
    lighting_params = bgfx::createUniform("u_lighting_params", bgfx::UniformType::Vec4);
    probe_sh = bgfx::createUniform("u_probe_sh", bgfx::UniformType::Vec4, 9);
    lightmap_sampler = bgfx::createUniform("s_lightmap", bgfx::UniformType::Sampler);
    capture_mode =
        bgfx::createUniform("u_capture_mode", bgfx::UniformFreq::View, bgfx::UniformType::Vec4);
    surface = bgfx::createUniform("u_surface", bgfx::UniformType::Vec4);
    shadow_matrices =
        bgfx::createUniform("u_shadow_matrices", bgfx::UniformFreq::View, bgfx::UniformType::Mat4,
                            render_detail::maximum_shadow_cascades);
    shadow_rects =
        bgfx::createUniform("u_shadow_rects", bgfx::UniformFreq::View, bgfx::UniformType::Vec4,
                            render_detail::maximum_shadow_cascades);
    shadow_splits =
        bgfx::createUniform("u_shadow_splits", bgfx::UniformFreq::View, bgfx::UniformType::Vec4);
    shadow_params =
        bgfx::createUniform("u_shadow_params", bgfx::UniformFreq::View, bgfx::UniformType::Vec4);
    shadow_settings =
        bgfx::createUniform("u_shadow_settings", bgfx::UniformFreq::View, bgfx::UniformType::Vec4);
    shadow_depth = bgfx::createUniform("u_shadow_depth", bgfx::UniformType::Vec4);
    shadow_sampler = bgfx::createUniform("s_shadow_map", bgfx::UniformType::Sampler);
    ibl_params =
        bgfx::createUniform("u_ibl_params", bgfx::UniformFreq::View, bgfx::UniformType::Vec4);
    ibl_irradiance_sampler = bgfx::createUniform("s_ibl_irradiance", bgfx::UniformType::Sampler);
    ibl_prefilter_sampler = bgfx::createUniform("s_ibl_prefilter", bgfx::UniformType::Sampler);
    ibl_lut_sampler = bgfx::createUniform("s_ibl_lut", bgfx::UniformType::Sampler);
    ao_settings = bgfx::createUniform("u_ambient_occlusion", bgfx::UniformFreq::View,
                                      bgfx::UniformType::Vec4);
    ao_sampler = bgfx::createUniform("s_ambient_occlusion", bgfx::UniformType::Sampler);
    for (auto uniform : {frame_time_uniform,
                         normal_transform,
                         camera_position,
                         light_direction,
                         light_color,
                         ambient,
                         tonemap,
                         frame_inputs,
                         frame_valid,
                         planar_projection,
                         lighting_params,
                         probe_sh,
                         lightmap_sampler,
                         capture_mode,
                         surface,
                         ambient_sky,
                         ambient_equator,
                         ambient_ground,
                         fog_color,
                         fog_range,
                         shadow_matrices,
                         shadow_rects,
                         shadow_splits,
                         shadow_params,
                         shadow_settings,
                         shadow_depth,
                         shadow_sampler,
                         ibl_params,
                         ibl_irradiance_sampler,
                         ibl_prefilter_sampler,
                         ibl_lut_sampler,
                         ao_settings,
                         ao_sampler})
    {
        if (!bgfx::isValid(uniform))
        {
            release();
            return false;
        }
    }
    constexpr std::uint32_t white = 0xffffffff;
    shadow_fallback =
        bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                              BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT |
                                  BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT,
                              bgfx::copy(&white, sizeof(white)));
    constexpr std::array<std::uint32_t, 6> black_cube{};
    constexpr std::uint64_t ibl_flags =
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;
    ibl_cube_fallback = bgfx::createTextureCube(1, false, 1, bgfx::TextureFormat::RGBA8, ibl_flags,
                                                bgfx::copy(black_cube.data(), sizeof(black_cube)));
    ibl_lut_fallback = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, ibl_flags,
                                             bgfx::copy(black_cube.data(), sizeof(std::uint32_t)));
    if (!bgfx::isValid(shadow_fallback) || !bgfx::isValid(ibl_cube_fallback) ||
        !bgfx::isValid(ibl_lut_fallback) ||
        bgfx::getCaps()->limits.maxTextureSamplers <= render_detail::shadow_sampler_stage)
    {
        release();
        return false;
    }
    return true;
}

void RenderSystem::SceneGpuResources::release() noexcept
{
    frame_data = {};
    draw_lists = {};
    view_stats.clear();
    selected_lods.clear();
    selected_lod_revisions.clear();
    frame_time_stamp = UINT32_MAX;
    atmosphere.release();
    debug_draw.release();
    for (auto& mesh : meshes)
    {
        mesh.release();
    }
    programs.clear();
    textures.clear();
    ibl_textures.fill(nullptr);
    release_capture();
    if (bgfx::isValid(hdr_capture_readback))
    {
        bgfx::destroy(hdr_capture_readback);
        hdr_capture_readback = BGFX_INVALID_HANDLE;
    }
    hdr_capture_pixels.clear();
    for (auto& cluster : clusters)
    {
        cluster.release();
    }
    for (auto& reflection : planar)
    {
        reflection.release();
    }
    surface_pass.release();
    reflections.release();
    shadow.release();
    ambient_occlusion.release();
    hdr_post_process.release();
    if (bgfx::isValid(shadow_fallback))
    {
        bgfx::destroy(shadow_fallback);
    }
    for (auto fallback : {ibl_cube_fallback, ibl_lut_fallback})
    {
        if (bgfx::isValid(fallback))
        {
            bgfx::destroy(fallback);
        }
    }
    for (auto uniform : {frame_time_uniform,
                         normal_transform,
                         camera_position,
                         light_direction,
                         light_color,
                         ambient,
                         tonemap,
                         frame_inputs,
                         frame_valid,
                         planar_projection,
                         lighting_params,
                         probe_sh,
                         lightmap_sampler,
                         capture_mode,
                         surface,
                         ambient_sky,
                         ambient_equator,
                         ambient_ground,
                         fog_color,
                         fog_range,
                         shadow_matrices,
                         shadow_rects,
                         shadow_splits,
                         shadow_params,
                         shadow_settings,
                         shadow_depth,
                         shadow_sampler,
                         ibl_params,
                         ibl_irradiance_sampler,
                         ibl_prefilter_sampler,
                         ibl_lut_sampler,
                         ao_settings,
                         ao_sampler})
    {
        if (bgfx::isValid(uniform))
        {
            bgfx::destroy(uniform);
        }
    }

    frame_time_uniform = BGFX_INVALID_HANDLE;
    normal_transform = BGFX_INVALID_HANDLE;
    camera_position = BGFX_INVALID_HANDLE;
    light_direction = BGFX_INVALID_HANDLE;
    light_color = BGFX_INVALID_HANDLE;
    ambient = BGFX_INVALID_HANDLE;
    ambient_sky = BGFX_INVALID_HANDLE;
    ambient_equator = BGFX_INVALID_HANDLE;
    ambient_ground = BGFX_INVALID_HANDLE;
    fog_color = BGFX_INVALID_HANDLE;
    fog_range = BGFX_INVALID_HANDLE;
    tonemap = BGFX_INVALID_HANDLE;
    frame_inputs = BGFX_INVALID_HANDLE;
    frame_valid = BGFX_INVALID_HANDLE;
    planar_projection = BGFX_INVALID_HANDLE;
    lighting_params = BGFX_INVALID_HANDLE;
    probe_sh = BGFX_INVALID_HANDLE;
    lightmap_sampler = BGFX_INVALID_HANDLE;
    capture_mode = BGFX_INVALID_HANDLE;
    surface = BGFX_INVALID_HANDLE;
    shadow_matrices = BGFX_INVALID_HANDLE;
    shadow_rects = BGFX_INVALID_HANDLE;
    shadow_splits = BGFX_INVALID_HANDLE;
    shadow_params = BGFX_INVALID_HANDLE;
    shadow_settings = BGFX_INVALID_HANDLE;
    shadow_depth = BGFX_INVALID_HANDLE;
    shadow_sampler = BGFX_INVALID_HANDLE;
    shadow_fallback = BGFX_INVALID_HANDLE;
    ibl_params = BGFX_INVALID_HANDLE;
    ibl_irradiance_sampler = BGFX_INVALID_HANDLE;
    ibl_prefilter_sampler = BGFX_INVALID_HANDLE;
    ibl_lut_sampler = BGFX_INVALID_HANDLE;
    ibl_cube_fallback = BGFX_INVALID_HANDLE;
    ibl_lut_fallback = BGFX_INVALID_HANDLE;
    ao_settings = BGFX_INVALID_HANDLE;
    ao_sampler = BGFX_INVALID_HANDLE;
    programs.clear();
    meshes.clear();
    mesh_index.clear();
    geometries.clear();
    skinning.release();
    textures.clear();
    ibl_generation = 0;
}

std::uint32_t RenderSystem::SceneGpuResources::resource_count() const noexcept
{
    std::uint32_t count = atmosphere.resource_count() + debug_draw.resource_count() +
                          static_cast<std::uint32_t>(bgfx::isValid(frame_time_uniform)) +
                          static_cast<std::uint32_t>(bgfx::isValid(normal_transform)) +
                          static_cast<std::uint32_t>(bgfx::isValid(camera_position)) +
                          static_cast<std::uint32_t>(bgfx::isValid(light_direction)) +
                          static_cast<std::uint32_t>(bgfx::isValid(light_color)) +
                          static_cast<std::uint32_t>(bgfx::isValid(ambient)) +
                          static_cast<std::uint32_t>(bgfx::isValid(ambient_sky)) +
                          static_cast<std::uint32_t>(bgfx::isValid(ambient_equator)) +
                          static_cast<std::uint32_t>(bgfx::isValid(ambient_ground)) +
                          static_cast<std::uint32_t>(bgfx::isValid(fog_color)) +
                          static_cast<std::uint32_t>(bgfx::isValid(fog_range)) +
                          static_cast<std::uint32_t>(bgfx::isValid(tonemap)) +
                          static_cast<std::uint32_t>(bgfx::isValid(frame_inputs)) +
                          static_cast<std::uint32_t>(bgfx::isValid(frame_valid)) +
                          static_cast<std::uint32_t>(bgfx::isValid(planar_projection)) +
                          static_cast<std::uint32_t>(bgfx::isValid(lighting_params)) +
                          static_cast<std::uint32_t>(bgfx::isValid(probe_sh)) +
                          static_cast<std::uint32_t>(bgfx::isValid(lightmap_sampler)) +
                          static_cast<std::uint32_t>(bgfx::isValid(capture_mode)) +
                          static_cast<std::uint32_t>(bgfx::isValid(surface)) +
                          static_cast<std::uint32_t>(bgfx::isValid(shadow_matrices)) +
                          static_cast<std::uint32_t>(bgfx::isValid(shadow_rects)) +
                          static_cast<std::uint32_t>(bgfx::isValid(shadow_splits)) +
                          static_cast<std::uint32_t>(bgfx::isValid(shadow_params)) +
                          static_cast<std::uint32_t>(bgfx::isValid(shadow_settings)) +
                          static_cast<std::uint32_t>(bgfx::isValid(shadow_depth)) +
                          static_cast<std::uint32_t>(bgfx::isValid(shadow_sampler)) +
                          static_cast<std::uint32_t>(bgfx::isValid(shadow_fallback)) +
                          static_cast<std::uint32_t>(bgfx::isValid(ibl_params)) +
                          static_cast<std::uint32_t>(bgfx::isValid(ibl_irradiance_sampler)) +
                          static_cast<std::uint32_t>(bgfx::isValid(ibl_prefilter_sampler)) +
                          static_cast<std::uint32_t>(bgfx::isValid(ibl_lut_sampler)) +
                          static_cast<std::uint32_t>(bgfx::isValid(ibl_cube_fallback)) +
                          static_cast<std::uint32_t>(bgfx::isValid(ibl_lut_fallback)) +
                          static_cast<std::uint32_t>(bgfx::isValid(ao_settings)) +
                          static_cast<std::uint32_t>(bgfx::isValid(ao_sampler)) +
                          shadow.resource_count() + ambient_occlusion.resource_count() +
                          hdr_post_process.resource_count() + skinning.resource_count();
    count += static_cast<std::uint32_t>(bgfx::isValid(capture_sky_program)) +
             static_cast<std::uint32_t>(bgfx::isValid(capture_inverse)) +
             static_cast<std::uint32_t>(bgfx::isValid(capture_fullscreen));
    count += surface_pass.resource_count() + reflections.resource_count();
    for (const auto& cluster : clusters)
    {
        count += cluster.resource_count();
    }
    for (const auto& reflection : planar)
    {
        count += reflection.resource_count();
    }
    count += static_cast<std::uint32_t>(bgfx::isValid(capture_framebuffer));
    count += static_cast<std::uint32_t>(bgfx::isValid(hdr_capture_readback));
    for (const auto handle : {capture_color, capture_specular, capture_depth, capture_readback})
    {
        count += static_cast<std::uint32_t>(bgfx::isValid(handle));
    }
    for (const auto handle : capture_lightmaps)
    {
        count += static_cast<std::uint32_t>(bgfx::isValid(handle));
    }
    for (const auto& mesh : capture_meshes)
    {
        if (mesh)
        {
            count += mesh->resource_count();
        }
    }
    for (const auto& mesh : meshes)
    {
        count += static_cast<std::uint32_t>(bgfx::isValid(mesh.deformed_vertices)) +
                 static_cast<std::uint32_t>(bgfx::isValid(mesh.palette));
    }
    for (const auto& [source, cached] : geometries)
    {
        if (const auto geometry = cached.lock())
        {
            count += geometry->resource_count();
        }
    }
    for (const auto& [source, texture] : textures)
    {
        count += static_cast<std::uint32_t>(bgfx::isValid(texture.handle));
    }
    for (const auto& [source, program] : programs)
    {
        count += static_cast<std::uint32_t>(bgfx::isValid(program.vertex_shader)) +
                 static_cast<std::uint32_t>(bgfx::isValid(program.fragment_shader)) +
                 static_cast<std::uint32_t>(bgfx::isValid(program.program)) +
                 static_cast<std::uint32_t>(bgfx::isValid(program.surface_shader)) +
                 static_cast<std::uint32_t>(bgfx::isValid(program.surface_program)) +
                 static_cast<std::uint32_t>(bgfx::isValid(program.instanced_vertex_shader)) +
                 static_cast<std::uint32_t>(bgfx::isValid(program.instanced_program)) +
                 static_cast<std::uint32_t>(bgfx::isValid(program.instanced_surface_program));
        for (const auto& uniform : program.uniforms)
        {
            count += static_cast<std::uint32_t>(bgfx::isValid(uniform.handle));
        }
        for (const auto& sampler : program.samplers)
        {
            count += static_cast<std::uint32_t>(bgfx::isValid(sampler.handle));
        }
    }
    return count;
}

bool RenderSystem::SceneGpuResources::collect_ibl(const Scene* scene,
                                                  RenderPreparation& preparation)
{
    EnvironmentSettings snapshot;
    const EnvironmentSettings* environment = nullptr;
    std::array<ResourceHandle, 3> sources{};
    if (scene)
    {
        if (!scene->world().refresh_scene_state())
        {
            return false;
        }
        auto query =
            flecs::world(scene->world().native_world())
                .query_builder<const ecs_detail::ComponentStorage<EnvironmentComponent::Data>,
                               const EnvironmentRuntime, const ecs_detail::NodeRuntime>()
                .with<ecs_detail::Published>()
                .build();
        query.each(
            [&](const auto& stored, const EnvironmentRuntime& runtime,
                const ecs_detail::NodeRuntime& state)
            {
                if (state.scene == scene && state.active && state.visible_in_hierarchy &&
                    stored.state.flags.enabled && stored.state.flags.visible)
                {
                    snapshot = stored.data;
                    environment = &snapshot;
                    for (std::size_t index = 0; index < sources.size(); ++index)
                    {
                        sources[index] = runtime.ibl[index].handle();
                    }
                }
            });
    }
    auto* loader = scene ? scene->world().resource_loader() : nullptr;
    for (std::size_t index = 0; index < sources.size(); ++index)
    {
        const TextureResource* source = nullptr;
        if (environment && !environment->ibl_irradiance_guid.empty())
        {
            const bool cube = index < 2;
            const auto format =
                cube ? schema::TextureFormat::rgba16f : schema::TextureFormat::rg16f;
            if (!loader || loader->get(sources[index], source) != ResourceError::none ||
                source->data().cube_map != cube || source->data().srgb ||
                source->data().format != format ||
                !preparation.request_texture(loader, sources[index]))
            {
                return false;
            }
        }
        auto& previous = preparation.ibl_sources[index];
        if (previous != source)
        {
            if (previous)
            {
                if (--preparation.texture_users[previous] == 0)
                {
                    preparation.unused_textures.insert(previous);
                }
            }
            if (source)
            {
                ++preparation.texture_users[source];
                preparation.unused_textures.erase(source);
            }
            previous = source;
        }
        ibl_textures[index] = nullptr;
    }
    return true;
}

} // namespace engine
