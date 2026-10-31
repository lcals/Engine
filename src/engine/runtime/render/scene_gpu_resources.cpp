#include "runtime/render/scene_gpu_resources.hpp"

#include "runtime/render/render_constants.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/environment_component.hpp"
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
    normal_transform = bgfx::createUniform("u_normal_transform", bgfx::UniformType::Mat4);
    camera_position = bgfx::createUniform("u_camera_position", bgfx::UniformType::Vec4);
    light_direction = bgfx::createUniform("u_light_direction", bgfx::UniformType::Vec4);
    light_color = bgfx::createUniform("u_light_color", bgfx::UniformType::Vec4);
    light_count = bgfx::createUniform("u_light_count", bgfx::UniformType::Vec4);
    light_position_range = bgfx::createUniform("u_light_position_range", bgfx::UniformType::Vec4,
                                               render_detail::maximum_lights);
    light_direction_outer = bgfx::createUniform("u_light_direction_outer", bgfx::UniformType::Vec4,
                                                render_detail::maximum_lights);
    light_color_intensity = bgfx::createUniform("u_light_color_intensity", bgfx::UniformType::Vec4,
                                                render_detail::maximum_lights);
    light_kind_inner = bgfx::createUniform("u_light_kind_inner", bgfx::UniformType::Vec4,
                                           render_detail::maximum_lights);
    ambient = bgfx::createUniform("u_ambient", bgfx::UniformType::Vec4);
    ambient_sky = bgfx::createUniform("u_ambient_sky", bgfx::UniformType::Vec4);
    ambient_equator = bgfx::createUniform("u_ambient_equator", bgfx::UniformType::Vec4);
    ambient_ground = bgfx::createUniform("u_ambient_ground", bgfx::UniformType::Vec4);
    fog_color = bgfx::createUniform("u_fog_color", bgfx::UniformType::Vec4);
    fog_range = bgfx::createUniform("u_fog_range", bgfx::UniformType::Vec4);
    tonemap = bgfx::createUniform("u_tonemap", bgfx::UniformType::Vec4);
    surface = bgfx::createUniform("u_surface", bgfx::UniformType::Vec4);
    shadow_matrices = bgfx::createUniform("u_shadow_matrices", bgfx::UniformType::Mat4,
                                          render_detail::maximum_shadow_cascades);
    shadow_rects = bgfx::createUniform("u_shadow_rects", bgfx::UniformType::Vec4,
                                       render_detail::maximum_shadow_cascades);
    shadow_splits = bgfx::createUniform("u_shadow_splits", bgfx::UniformType::Vec4);
    shadow_params = bgfx::createUniform("u_shadow_params", bgfx::UniformType::Vec4);
    shadow_settings = bgfx::createUniform("u_shadow_settings", bgfx::UniformType::Vec4);
    shadow_depth = bgfx::createUniform("u_shadow_depth", bgfx::UniformType::Vec4);
    shadow_sampler = bgfx::createUniform("s_shadow_map", bgfx::UniformType::Sampler);
    ibl_params = bgfx::createUniform("u_ibl_params", bgfx::UniformType::Vec4);
    ibl_irradiance_sampler = bgfx::createUniform("s_ibl_irradiance", bgfx::UniformType::Sampler);
    ibl_prefilter_sampler = bgfx::createUniform("s_ibl_prefilter", bgfx::UniformType::Sampler);
    ibl_lut_sampler = bgfx::createUniform("s_ibl_lut", bgfx::UniformType::Sampler);
    ao_settings = bgfx::createUniform("u_ambient_occlusion", bgfx::UniformType::Vec4);
    ao_sampler = bgfx::createUniform("s_ambient_occlusion", bgfx::UniformType::Sampler);
    for (auto uniform : {normal_transform,
                         camera_position,
                         light_direction,
                         light_color,
                         light_count,
                         light_position_range,
                         light_direction_outer,
                         light_color_intensity,
                         light_kind_inner,
                         ambient,
                         tonemap,
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
    for (auto& program : programs)
    {
        program.release();
    }
    for (auto& mesh : meshes)
    {
        mesh.release();
    }
    for (auto& texture : textures)
    {
        texture.release();
    }
    for (auto& texture : ibl_textures)
    {
        texture.release();
    }
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
    for (auto uniform : {normal_transform,
                         camera_position,
                         light_direction,
                         light_color,
                         light_count,
                         light_position_range,
                         light_direction_outer,
                         light_color_intensity,
                         light_kind_inner,
                         ambient,
                         tonemap,
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

    normal_transform = BGFX_INVALID_HANDLE;
    camera_position = BGFX_INVALID_HANDLE;
    light_direction = BGFX_INVALID_HANDLE;
    light_color = BGFX_INVALID_HANDLE;
    light_count = BGFX_INVALID_HANDLE;
    light_position_range = BGFX_INVALID_HANDLE;
    light_direction_outer = BGFX_INVALID_HANDLE;
    light_color_intensity = BGFX_INVALID_HANDLE;
    light_kind_inner = BGFX_INVALID_HANDLE;
    ambient = BGFX_INVALID_HANDLE;
    ambient_sky = BGFX_INVALID_HANDLE;
    ambient_equator = BGFX_INVALID_HANDLE;
    ambient_ground = BGFX_INVALID_HANDLE;
    fog_color = BGFX_INVALID_HANDLE;
    fog_range = BGFX_INVALID_HANDLE;
    tonemap = BGFX_INVALID_HANDLE;
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
    textures.clear();
    ibl_generation = 0;
}

std::uint32_t RenderSystem::SceneGpuResources::resource_count() const noexcept
{
    std::uint32_t count = static_cast<std::uint32_t>(bgfx::isValid(normal_transform)) +
                          static_cast<std::uint32_t>(bgfx::isValid(camera_position)) +
                          static_cast<std::uint32_t>(bgfx::isValid(light_direction)) +
                          static_cast<std::uint32_t>(bgfx::isValid(light_color)) +
                          static_cast<std::uint32_t>(bgfx::isValid(light_count)) +
                          static_cast<std::uint32_t>(bgfx::isValid(light_position_range)) +
                          static_cast<std::uint32_t>(bgfx::isValid(light_direction_outer)) +
                          static_cast<std::uint32_t>(bgfx::isValid(light_color_intensity)) +
                          static_cast<std::uint32_t>(bgfx::isValid(light_kind_inner)) +
                          static_cast<std::uint32_t>(bgfx::isValid(ambient)) +
                          static_cast<std::uint32_t>(bgfx::isValid(ambient_sky)) +
                          static_cast<std::uint32_t>(bgfx::isValid(ambient_equator)) +
                          static_cast<std::uint32_t>(bgfx::isValid(ambient_ground)) +
                          static_cast<std::uint32_t>(bgfx::isValid(fog_color)) +
                          static_cast<std::uint32_t>(bgfx::isValid(fog_range)) +
                          static_cast<std::uint32_t>(bgfx::isValid(tonemap)) +
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
                          hdr_post_process.resource_count();
    for (const auto& texture : ibl_textures)
    {
        count += static_cast<std::uint32_t>(bgfx::isValid(texture.handle));
    }
    for (const auto& mesh : meshes)
    {
        count += static_cast<std::uint32_t>(bgfx::isValid(mesh.vertices)) +
                 static_cast<std::uint32_t>(bgfx::isValid(mesh.deformed_vertices)) +
                 static_cast<std::uint32_t>(bgfx::isValid(mesh.indices));
    }
    for (const auto& texture : textures)
    {
        count += static_cast<std::uint32_t>(bgfx::isValid(texture.handle));
    }
    for (const auto& program : programs)
    {
        count += static_cast<std::uint32_t>(bgfx::isValid(program.vertex_shader)) +
                 static_cast<std::uint32_t>(bgfx::isValid(program.fragment_shader)) +
                 static_cast<std::uint32_t>(bgfx::isValid(program.program));
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

bool RenderSystem::SceneGpuResources::prepare_ibl(const Scene& scene)
{
    const EnvironmentSettings* environment = nullptr;
    for (const auto& node : scene.nodes())
    {
        const auto* component = node->get_component<EnvironmentComponent>();
        if (component != nullptr && component->enabled())
        {
            environment = &component->settings();
            break;
        }
    }
    if (environment == nullptr || environment->ibl_irradiance_guid.empty())
    {
        for (auto& texture : ibl_textures)
        {
            texture.release();
        }
        ibl_generation = 0;
        return true;
    }
    auto* resources = ResourceSystem::instance();
    if (resources == nullptr)
    {
        return false;
    }
    const std::array guids = {&environment->ibl_irradiance_guid, &environment->ibl_prefilter_guid,
                              &environment->ibl_lut_guid};
    bool changed = ibl_generation != resources->generation();
    for (std::size_t index = 0; index < guids.size(); ++index)
    {
        changed = changed || !ibl_textures[index].resource ||
                  ibl_textures[index].resource->info().guid != *guids[index];
    }
    if (!changed)
    {
        return true;
    }
    std::array<render_detail::GpuTexture, 3> candidate;
    for (std::size_t index = 0; index < guids.size(); ++index)
    {
        ResourcePtr source;
        const bool loaded = resources->loader().load(*guids[index], source);
        const auto* texture = source ? source->texture() : nullptr;
        const bool cube = index < 2;
        const auto format = cube ? schema::TextureFormat::rgba16f : schema::TextureFormat::rg16f;
        if (!loaded || texture == nullptr || texture->cube_map != cube || texture->srgb ||
            texture->format != format || !candidate[index].create(source))
        {
            for (auto& item : candidate)
            {
                item.release();
            }
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Invalid IBL texture resource: %s",
                         guids[index]->c_str());
            return false;
        }
    }
    for (std::size_t index = 0; index < candidate.size(); ++index)
    {
        ibl_textures[index].release();
        ibl_textures[index] = std::move(candidate[index]);
    }
    ibl_generation = resources->generation();
    return true;
}

} // namespace engine
