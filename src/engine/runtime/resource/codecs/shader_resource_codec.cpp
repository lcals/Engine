#include "runtime/resource/codecs/shader_resource_codec.hpp"
#include "runtime/resource/codecs/resource_decode.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/resource/codecs/material_value_utils.hpp"
#include "runtime/resource/types/texture_resource.hpp"
#include <set>

#include <map>
#include <utility>

namespace engine
{

namespace
{

bool valid_uniform_name(std::string_view name)
{
    const auto letter = [](char value)
    { return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || value == '_'; };
    if (name.empty() || !letter(name.front()))
    {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [letter](char value)
                       { return letter(value) || (value >= '0' && value <= '9'); });
}

bool reserved_uniform(std::string_view name)
{
    constexpr std::array names = {std::string_view("u_normal_transform"),
                                  std::string_view("u_ambient"),
                                  std::string_view("u_ambient_sky"),
                                  std::string_view("u_ambient_equator"),
                                  std::string_view("u_ambient_ground"),
                                  std::string_view("u_fog_color"),
                                  std::string_view("u_fog_range"),
                                  std::string_view("u_shadow_uv"),
                                  std::string_view("u_tonemap"),
                                  std::string_view("u_surface"),
                                  std::string_view("u_shadow_alpha"),
                                  std::string_view("s_shadow_alpha"),
                                  std::string_view("u_shadow_matrices"),
                                  std::string_view("u_shadow_rects"),
                                  std::string_view("u_shadow_splits"),
                                  std::string_view("u_shadow_params"),
                                  std::string_view("u_shadow_settings"),
                                  std::string_view("u_shadow_depth"),
                                  std::string_view("s_shadow_map"),
                                  std::string_view("u_ibl_params"),
                                  std::string_view("u_ambient_occlusion"),
                                  std::string_view("s_ambient_occlusion"),
                                  std::string_view("u_assao_params"),
                                  std::string_view("u_assao_rect"),
                                  std::string_view("u_hdr_params"),
                                  std::string_view("u_hdr_texel"),
                                  std::string_view("u_hdr_output"),
                                  std::string_view("s_hdr_scene"),
                                  std::string_view("s_hdr_bloom"),
                                  std::string_view("s_ibl_irradiance"),
                                  std::string_view("s_ibl_prefilter"),
                                  std::string_view("s_ibl_lut"),
                                  std::string_view("u_camera_position"),
                                  std::string_view("u_light_direction"),
                                  std::string_view("u_light_color"),
                                  std::string_view("u_light_count"),
                                  std::string_view("u_light_position_range"),
                                  std::string_view("u_light_direction_outer"),
                                  std::string_view("u_light_color_intensity"),
                                  std::string_view("u_light_kind_inner"),
                                  std::string_view("u_cluster_grid"),
                                  std::string_view("u_cluster_camera"),
                                  std::string_view("u_cluster_counts"),
                                  std::string_view("u_cluster_viewport"),
                                  std::string_view("u_frame_inputs"),
                                  std::string_view("u_frame_time"),
                                  std::string_view("u_frame_valid"),
                                  std::string_view("u_planar_projection"),
                                  std::string_view("u_lighting_params"),
                                  std::string_view("u_probe_sh"),
                                  std::string_view("u_capture_mode"),
                                  std::string_view("s_lightmap"),
                                  std::string_view("u_viewRect"),
                                  std::string_view("u_viewTexel"),
                                  std::string_view("u_view"),
                                  std::string_view("u_invView"),
                                  std::string_view("u_proj"),
                                  std::string_view("u_invProj"),
                                  std::string_view("u_viewProj"),
                                  std::string_view("u_invViewProj"),
                                  std::string_view("u_model"),
                                  std::string_view("u_modelView"),
                                  std::string_view("u_modelViewProj"),
                                  std::string_view("u_alphaRef")};
    return std::find(names.begin(), names.end(), name) != names.end();
}

} // namespace

bool ShaderResourceCodec::validate(const schema::ShaderDataT& data)
{
    if (!valid_path(data.path) || data.schema_version != 1 || data.vertex_code.empty() ||
        data.fragment_code.empty())
    {
        return false;
    }
    std::set<std::string_view> names;
    std::map<std::string_view, unsigned int> uniform_slots;
    for (const auto& parameter : data.parameters)
    {
        if (parameter == nullptr || parameter->name.empty() ||
            parameter->name.find('\0') != std::string::npos ||
            !names.insert(parameter->name).second || !valid_uniform_name(parameter->uniform_name) ||
            reserved_uniform(parameter->uniform_name) || !std::isfinite(parameter->minimum) ||
            !std::isfinite(parameter->maximum) || parameter->minimum > parameter->maximum ||
            !resource_codec_detail::valid_parameter_value(*parameter, parameter->default_value))
        {
            return false;
        }
        std::array<double, 4> values{};
        std::uint8_t size = 0;
        if (!resource_codec_detail::parameter_values(parameter->default_value, values, size) ||
            static_cast<unsigned int>(parameter->uniform_offset) + size > 4)
        {
            return false;
        }
        const unsigned int mask = ((1U << size) - 1U) << parameter->uniform_offset;
        unsigned int& slots = uniform_slots[parameter->uniform_name];
        if ((slots & mask) != 0)
        {
            return false;
        }
        slots |= mask;
    }
    std::set<std::uint8_t> stages;
    std::set<std::string_view> sampler_uniforms;
    for (const auto& sampler : data.samplers)
    {
        if (!sampler || sampler->name.empty() || sampler->name.find('\0') != std::string::npos ||
            !names.insert(sampler->name).second || !valid_uniform_name(sampler->uniform_name) ||
            reserved_uniform(sampler->uniform_name) ||
            uniform_slots.contains(sampler->uniform_name) ||
            !sampler_uniforms.insert(sampler->uniform_name).second ||
            !stages.insert(sampler->stage).second ||
            sampler->source < schema::ShaderTextureSource::MIN ||
            sampler->source > schema::ShaderTextureSource::MAX)
        {
            return false;
        }
        const bool resource = sampler->source == schema::ShaderTextureSource::resource;
        const std::uint8_t dynamic_stage =
            sampler->source == schema::ShaderTextureSource::planar_reflection ? 8
            : sampler->source == schema::ShaderTextureSource::scene_color     ? 9
                                                                              : 10;
        if (resource ? (sampler->stage > 4 || !sampler->default_texture.starts_with("builtin://") ||
                        !valid_path(sampler->default_texture))
                     : (sampler->stage != dynamic_stage || !sampler->default_texture.empty() ||
                        sampler->exposed))
        {
            return false;
        }
    }
    return true;
}

schema::AssetKind ShaderResourceCodec::kind() const noexcept
{
    return schema::AssetKind::shader;
}

bool ShaderResourceCodec::decode(const schema::AssetEntryT& entry, ByteView payload,
                                 std::unique_ptr<Resource>& output, std::string& error) const
{
    const auto validate = [](ByteView payload, std::string& error)
    {
        return resource_decode_detail::verify_flatbuffer(payload, schema::VerifyShaderDataBuffer,
                                                         "Invalid Shader FlatBuffer", error);
    };
    return resource_decode_detail::decode_flatbuffer<ResourceType>(
        entry, payload, 64U * 1024U * 1024U, validate,
        [](const std::uint8_t* bytes) { return schema::UnPackShaderData(bytes); }, output, error);
}

bool ShaderResourceCodec::validate_data(const Resource& resource, std::string& error) const
{
    const auto* data = resource_detail::data<ResourceType>(&resource);
    if (!data || !(data->path == resource.info().path && validate(*data)))
    {
        error = "Invalid Shader data or declared dependencies";
        return false;
    }
    error.clear();
    return true;
}

bool ShaderResourceCodec::validate_dependencies(const Resource& resource, ResourceView dependencies,
                                                std::string& error) const
{
    error.clear();
    const auto* shader = resource_detail::data<ResourceType>(&resource);
    if (!shader)
    {
        error = "Invalid shader type";
        return false;
    }
    std::set<std::string_view> expected;
    for (const auto& sampler : shader->samplers)
    {
        if (sampler->source != schema::ShaderTextureSource::resource)
        {
            continue;
        }
        const auto* texture = dependency(dependencies, sampler->default_texture);
        const auto* data = resource_detail::data<TextureResource>(texture);
        if (!data || data->cube_map || !declares(resource, texture->info().guid))
        {
            error = "Invalid shader default texture";
            return false;
        }
        expected.insert(texture->info().guid);
    }
    return expected.size() == dependencies.size();
}

} // namespace engine
