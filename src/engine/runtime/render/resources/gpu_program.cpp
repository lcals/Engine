#include "runtime/render/resources/gpu_program.hpp"

#include "runtime/performance/profiling.hpp"
#include "runtime/render/render_constants.hpp"
#include "runtime/render/resources/gpu_texture.hpp"
#include "runtime/resource/resource.hpp"
#include "runtime/scene/material.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace engine::render_detail
{

GpuProgram::~GpuProgram() noexcept
{
    release();
}

GpuProgram::GpuProgram(GpuProgram&& other) noexcept
    : path(std::move(other.path)), definition(std::move(other.definition)),
      vertex_shader(std::exchange(other.vertex_shader, bgfx::ShaderHandle{bgfx::kInvalidHandle})),
      fragment_shader(
          std::exchange(other.fragment_shader, bgfx::ShaderHandle{bgfx::kInvalidHandle})),
      program(std::exchange(other.program, bgfx::ProgramHandle{bgfx::kInvalidHandle})),
      surface_shader(std::exchange(other.surface_shader, bgfx::ShaderHandle{bgfx::kInvalidHandle})),
      surface_program(
          std::exchange(other.surface_program, bgfx::ProgramHandle{bgfx::kInvalidHandle})),
      instanced_vertex_shader(
          std::exchange(other.instanced_vertex_shader, bgfx::ShaderHandle{bgfx::kInvalidHandle})),
      instanced_program(
          std::exchange(other.instanced_program, bgfx::ProgramHandle{bgfx::kInvalidHandle})),
      instanced_surface_program(std::exchange(other.instanced_surface_program,
                                              bgfx::ProgramHandle{bgfx::kInvalidHandle})),
      uniforms(std::exchange(other.uniforms, {})), samplers(std::exchange(other.samplers, {}))
{
}

GpuProgram& GpuProgram::operator=(GpuProgram&& other) noexcept
{
    if (this != &other)
    {
        release();
        path = std::move(other.path);
        definition = std::move(other.definition);
        vertex_shader =
            std::exchange(other.vertex_shader, bgfx::ShaderHandle{bgfx::kInvalidHandle});
        fragment_shader =
            std::exchange(other.fragment_shader, bgfx::ShaderHandle{bgfx::kInvalidHandle});
        program = std::exchange(other.program, bgfx::ProgramHandle{bgfx::kInvalidHandle});
        surface_shader =
            std::exchange(other.surface_shader, bgfx::ShaderHandle{bgfx::kInvalidHandle});
        surface_program =
            std::exchange(other.surface_program, bgfx::ProgramHandle{bgfx::kInvalidHandle});
        instanced_vertex_shader =
            std::exchange(other.instanced_vertex_shader, bgfx::ShaderHandle{bgfx::kInvalidHandle});
        instanced_program =
            std::exchange(other.instanced_program, bgfx::ProgramHandle{bgfx::kInvalidHandle});
        instanced_surface_program = std::exchange(other.instanced_surface_program,
                                                  bgfx::ProgramHandle{bgfx::kInvalidHandle});
        uniforms = std::exchange(other.uniforms, {});
        samplers = std::exchange(other.samplers, {});
    }
    return *this;
}

bool GpuProgram::create(const schema::ShaderDataT& shader)
{
    ENGINE_PROFILE_ZONE("Render.programCreate");
    release();
    path = shader.path;
    definition = shader;
    vertex_shader = bgfx::createShader(bgfx::copy(
        shader.vertex_code.data(), static_cast<std::uint32_t>(shader.vertex_code.size())));
    fragment_shader = bgfx::createShader(bgfx::copy(
        shader.fragment_code.data(), static_cast<std::uint32_t>(shader.fragment_code.size())));
    if (!bgfx::isValid(vertex_shader) || !bgfx::isValid(fragment_shader))
    {
        return false;
    }
    program = bgfx::createProgram(vertex_shader, fragment_shader);
    if (!bgfx::isValid(program))
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot link material program: %s", path.c_str());
        return false;
    }
    if (!shader.surface_fragment_code.empty())
    {
        surface_shader = bgfx::createShader(
            bgfx::copy(shader.surface_fragment_code.data(),
                       static_cast<std::uint32_t>(shader.surface_fragment_code.size())));
        if (!bgfx::isValid(surface_shader))
        {
            return false;
        }
        surface_program = bgfx::createProgram(vertex_shader, surface_shader);
        if (!bgfx::isValid(surface_program))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot link surface program: %s", path.c_str());
            return false;
        }
    }
    if (!shader.instanced_vertex_code.empty())
    {
        instanced_vertex_shader = bgfx::createShader(
            bgfx::copy(shader.instanced_vertex_code.data(),
                       static_cast<std::uint32_t>(shader.instanced_vertex_code.size())));
        if (!bgfx::isValid(instanced_vertex_shader))
        {
            return false;
        }
        instanced_program = bgfx::createProgram(instanced_vertex_shader, fragment_shader);
        if (!bgfx::isValid(instanced_program))
        {
            return false;
        }
        if (bgfx::isValid(surface_shader))
        {
            instanced_surface_program =
                bgfx::createProgram(instanced_vertex_shader, surface_shader);
            if (!bgfx::isValid(instanced_surface_program))
            {
                return false;
            }
        }
    }
    // 反射与定义双向匹配，避免遗漏参数继承上一对象的 uniform 值。
    std::vector<bgfx::UniformInfo> reflected;
    for (auto stage : {vertex_shader, fragment_shader, surface_shader, instanced_vertex_shader})
    {
        if (!bgfx::isValid(stage))
        {
            continue;
        }
        const auto count = bgfx::getShaderUniforms(stage);
        std::vector<bgfx::UniformHandle> handles(count);
        bgfx::getShaderUniforms(stage, handles.data(), count);
        for (auto handle : handles)
        {
            bgfx::UniformInfo info{};
            bgfx::getUniformInfo(handle, info);
            const std::string_view name(info.name);
            if (name == "u_normal_transform" || name == "u_camera_position" ||
                name == "u_light_direction" || name == "u_light_color" || name == "u_ambient" ||
                name == "u_tonemap" || name == "u_surface" || name == "u_ambient_sky" ||
                name == "u_ambient_equator" || name == "u_ambient_ground" ||
                name == "u_fog_color" || name == "u_fog_range" || name == "u_shadow_matrices" ||
                name == "u_shadow_rects" || name == "u_shadow_splits" ||
                name == "u_shadow_params" || name == "u_shadow_settings" ||
                name == "u_shadow_depth" || name == "s_shadow_map" || name == "u_ibl_params" ||
                name == "s_ibl_irradiance" || name == "s_ibl_prefilter" || name == "s_ibl_lut" ||
                name == "s_ambient_occlusion" || name == "u_ambient_occlusion" ||
                name == "u_cluster_grid" || name == "u_cluster_camera" ||
                name == "u_cluster_counts" || name == "u_cluster_viewport" ||
                name == "u_frame_inputs" || name == "u_frame_time" || name == "u_frame_valid" ||
                name == "u_planar_projection" || name == "u_lighting_params" ||
                name == "u_probe_sh" || name == "s_lightmap" || name == "u_capture_mode")
            {
                const auto type = name == "u_normal_transform" || name == "u_shadow_matrices" ||
                                          name == "u_planar_projection"
                                      ? bgfx::UniformType::Mat4
                                  : name == "s_shadow_map" || name == "s_ibl_irradiance" ||
                                          name == "s_ibl_prefilter" || name == "s_ibl_lut" ||
                                          name == "s_ambient_occlusion" || name == "s_lightmap"
                                      ? bgfx::UniformType::Sampler
                                      : bgfx::UniformType::Vec4;
                const auto expected_count =
                    name == "u_probe_sh" ? 9
                    : name == "u_shadow_matrices" || name == "u_shadow_rects"
                        ? maximum_shadow_cascades
                        : 1;
                if (info.type != type || info.num != expected_count)
                {
                    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                                 "Invalid frame uniform %s in %s: type=%u count=%u expected=%u/%u",
                                 info.name, path.c_str(), static_cast<unsigned>(info.type),
                                 info.num, static_cast<unsigned>(type),
                                 static_cast<unsigned>(expected_count));
                    return false;
                }
                continue;
            }
            const bool sampler =
                std::any_of(shader.samplers.begin(), shader.samplers.end(),
                            [&name](const auto& item) { return item->uniform_name == name; });
            const bool parameter =
                std::any_of(shader.parameters.begin(), shader.parameters.end(),
                            [&name](const auto& item) { return item->uniform_name == name; });
            if (info.num != 1 || (sampler && parameter) ||
                (sampler ? info.type != bgfx::UniformType::Sampler
                         : !parameter || info.type != bgfx::UniformType::Vec4))
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "Undeclared or invalid shader uniform %s in %s", info.name,
                             path.c_str());
                return false;
            }
            reflected.push_back(info);
        }
    }
    const auto declared = [&reflected](const std::string& name)
    {
        return std::any_of(reflected.begin(), reflected.end(),
                           [&name](const auto& info) { return name == info.name; });
    };
    for (const auto& parameter : shader.parameters)
    {
        if (std::any_of(uniforms.begin(), uniforms.end(), [&parameter](const auto& uniform)
                        { return uniform.name == parameter->uniform_name; }))
        {
            continue;
        }
        if (!declared(parameter->uniform_name))
        {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                         "Shader parameter %s has no reflected uniform in %s",
                         parameter->uniform_name.c_str(), path.c_str());
            return false;
        }
        GpuUniform uniform;
        uniform.name = parameter->uniform_name;
        uniform.handle = bgfx::createUniform(uniform.name.c_str(), bgfx::UniformType::Vec4);
        uniforms.push_back(std::move(uniform));
        if (!bgfx::isValid(uniforms.back().handle))
        {
            return false;
        }
    }
    for (const auto& item : shader.samplers)
    {
        if (!declared(item->uniform_name) ||
            item->stage >= bgfx::getCaps()->limits.maxTextureSamplers ||
            (item->source == schema::ShaderTextureSource::resource
                 ? item->stage > 4
                 : item->stage != (item->source == schema::ShaderTextureSource::scene_color   ? 9
                                   : item->source == schema::ShaderTextureSource::scene_depth ? 10
                                                                                              : 8)))
        {
            return false;
        }
        GpuSampler sampler;
        sampler.name = item->name;
        sampler.uniform_name = item->uniform_name;
        sampler.stage = item->stage;
        sampler.source = item->source;
        sampler.handle =
            bgfx::createUniform(item->uniform_name.c_str(), bgfx::UniformType::Sampler);
        samplers.push_back(std::move(sampler));
        if (!bgfx::isValid(samplers.back().handle))
        {
            return false;
        }
    }
    return true;
}

void GpuProgram::release() noexcept
{
    for (auto handle : {instanced_program, instanced_surface_program})
    {
        if (bgfx::isValid(handle))
        {
            bgfx::destroy(handle);
        }
    }
    if (bgfx::isValid(instanced_vertex_shader))
    {
        bgfx::destroy(instanced_vertex_shader);
    }
    instanced_vertex_shader = BGFX_INVALID_HANDLE;
    instanced_program = BGFX_INVALID_HANDLE;
    instanced_surface_program = BGFX_INVALID_HANDLE;

    if (bgfx::isValid(surface_program))
    {
        bgfx::destroy(surface_program);
        surface_program = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(surface_shader))
    {
        bgfx::destroy(surface_shader);
        surface_shader = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(program))
    {
        bgfx::destroy(program);
    }
    if (bgfx::isValid(vertex_shader))
    {
        bgfx::destroy(vertex_shader);
    }
    if (bgfx::isValid(fragment_shader))
    {
        bgfx::destroy(fragment_shader);
    }
    for (const auto& uniform : uniforms)
    {
        if (bgfx::isValid(uniform.handle))
        {
            bgfx::destroy(uniform.handle);
        }
    }
    program = BGFX_INVALID_HANDLE;
    vertex_shader = BGFX_INVALID_HANDLE;
    fragment_shader = BGFX_INVALID_HANDLE;
    uniforms.clear();
    for (const auto& sampler : samplers)
    {
        if (bgfx::isValid(sampler.handle))
        {
            bgfx::destroy(sampler.handle);
        }
    }
    samplers.clear();
}

bool GpuProgram::bind_material(const Material& material, const std::vector<GpuTexture>& textures,
                               std::span<const bgfx::TextureHandle> frame_textures,
                               bool max_anisotropy)
{
    const auto* shader = &definition;
    for (auto& uniform : uniforms)
    {
        uniform.values.fill(0.0F);
    }
    for (const auto& parameter : shader->parameters)
    {
        std::array<double, 4> values{};
        std::size_t count = 0;
        const auto& value = parameter->default_value;
        switch (value.type)
        {
        case schema::MaterialValue::FloatValue:
        {
            float scalar = value.AsFloatValue()->value;
            if (parameter->exposed && !material.get_float(parameter->name, scalar))
            {
                return false;
            }
            values[0] = scalar;
            count = 1;
            break;
        }
        case schema::MaterialValue::Vec2Value:
        {
            Vec2 vector = value.AsVec2Value()->value;
            if (parameter->exposed && !material.get_v2(parameter->name, vector))
            {
                return false;
            }
            values = {vector.x(), vector.y(), 0.0, 0.0};
            count = 2;
            break;
        }
        case schema::MaterialValue::Vec3Value:
        {
            Vec3 vector = value.AsVec3Value()->value;
            if (parameter->exposed && !material.get_v3(parameter->name, vector))
            {
                return false;
            }
            values = {vector.x(), vector.y(), vector.z(), 0.0};
            count = 3;
            break;
        }
        case schema::MaterialValue::Vec4Value:
        {
            Vec4 vector = value.AsVec4Value()->value;
            if (parameter->exposed && !material.get_v4(parameter->name, vector))
            {
                return false;
            }
            values = {vector.x(), vector.y(), vector.z(), vector.w()};
            count = 4;
            break;
        }
        default:
            return false;
        }
        auto uniform = std::find_if(uniforms.begin(), uniforms.end(), [&parameter](const auto& item)
                                    { return item.name == parameter->uniform_name; });
        if (uniform == uniforms.end() || parameter->uniform_offset + count > 4)
        {
            return false;
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            uniform->values[parameter->uniform_offset + index] = static_cast<float>(values[index]);
        }
    }
    for (const auto& uniform : uniforms)
    {
        bgfx::setUniform(uniform.handle, uniform.values.data());
    }
    for (const auto& sampler : samplers)
    {
        if (sampler.source != schema::ShaderTextureSource::resource)
        {
            const auto index = static_cast<std::size_t>(sampler.source) - 1;
            if (index >= frame_textures.size() || !bgfx::isValid(frame_textures[index]))
            {
                return false;
            }
            bgfx::setTexture(sampler.stage, sampler.handle, frame_textures[index]);
            continue;
        }
        const auto& resource = material.texture(sampler.name);
        const auto texture =
            std::find_if(textures.begin(), textures.end(),
                         [&resource](const auto& item) { return item.matches(resource); });
        if (resource.id == 0 || texture == textures.end() ||
            !texture->resource.data<TextureResource>() ||
            texture->resource.data<TextureResource>()->cube_map)
        {
            return false;
        }
        bgfx::setTexture(sampler.stage, sampler.handle, texture->handle,
                         max_anisotropy ? texture->sampler_flags | BGFX_SAMPLER_MIN_ANISOTROPIC |
                                              BGFX_SAMPLER_MAG_ANISOTROPIC
                                        : UINT32_MAX);
    }
    return true;
}

} // namespace engine::render_detail
