#include "runtime/render/resources/gpu_program.hpp"

#include "runtime/render/render_constants.hpp"
#include "runtime/render/resources/gpu_texture.hpp"
#include "runtime/resource/resource.hpp"
#include "runtime/scene/material.hpp"

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
        uniforms = std::exchange(other.uniforms, {});
        samplers = std::exchange(other.samplers, {});
    }
    return *this;
}

bool GpuProgram::create(const schema::ShaderDataT& shader)
{
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
        return false;
    }
    // 反射与定义双向匹配，避免遗漏参数继承上一对象的 uniform 值。
    std::vector<bgfx::UniformInfo> reflected;
    for (auto stage : {vertex_shader, fragment_shader})
    {
        const auto count = bgfx::getShaderUniforms(stage);
        std::vector<bgfx::UniformHandle> handles(count);
        bgfx::getShaderUniforms(stage, handles.data(), count);
        for (auto handle : handles)
        {
            bgfx::UniformInfo info{};
            bgfx::getUniformInfo(handle, info);
            const std::string_view name(info.name);
            const bool light_array =
                name == "u_light_position_range" || name == "u_light_direction_outer" ||
                name == "u_light_color_intensity" || name == "u_light_kind_inner";
            if (name == "u_normal_transform" || name == "u_camera_position" ||
                name == "u_light_direction" || name == "u_light_color" || name == "u_light_count" ||
                name == "u_ambient" || name == "u_tonemap" || name == "u_surface" ||
                name == "u_ambient_sky" || name == "u_ambient_equator" ||
                name == "u_ambient_ground" || name == "u_fog_color" || name == "u_fog_range" ||
                name == "u_shadow_matrices" || name == "u_shadow_rects" ||
                name == "u_shadow_splits" || name == "u_shadow_params" ||
                name == "u_shadow_settings" || name == "u_shadow_depth" || name == "s_shadow_map" ||
                name == "u_ibl_params" || name == "s_ibl_irradiance" || name == "s_ibl_prefilter" ||
                name == "s_ibl_lut" || name == "s_ambient_occlusion" ||
                name == "u_ambient_occlusion" || light_array)
            {
                const auto type = name == "u_normal_transform" || name == "u_shadow_matrices"
                                      ? bgfx::UniformType::Mat4
                                  : name == "s_shadow_map" || name == "s_ibl_irradiance" ||
                                          name == "s_ibl_prefilter" || name == "s_ibl_lut" ||
                                          name == "s_ambient_occlusion"
                                      ? bgfx::UniformType::Sampler
                                      : bgfx::UniformType::Vec4;
                const auto expected_count =
                    light_array ? maximum_lights
                    : name == "u_shadow_matrices" || name == "u_shadow_rects"
                        ? maximum_shadow_cascades
                        : 1;
                if (info.type != type || info.num != expected_count)
                {
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
            item->stage >= ambient_occlusion_stage)
        {
            return false;
        }
        GpuSampler sampler;
        sampler.name = item->name;
        sampler.uniform_name = item->uniform_name;
        sampler.stage = item->stage;
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

bool GpuProgram::bind_material(const Material& material, const std::vector<GpuTexture>& textures)
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
        const auto& resource = material.texture(sampler.name);
        const auto texture =
            std::find_if(textures.begin(), textures.end(),
                         [&resource](const auto& item) { return item.resource == resource; });
        if (!resource || resource->texture() == nullptr || resource->texture()->cube_map ||
            texture == textures.end())
        {
            return false;
        }
        bgfx::setTexture(sampler.stage, sampler.handle, texture->handle);
    }
    return true;
}

} // namespace engine::render_detail
