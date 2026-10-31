#include "runtime/resource/codecs/shader_resource_codec.hpp"

#include "runtime/resource/codecs/material_value_utils.hpp"
#include "runtime/resource/resource_catalog.hpp"
#include "runtime/resource/resource_system.hpp"

#include <flatbuffers/verifier.h>

#include <map>
#include <set>
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
            !sampler_uniforms.insert(sampler->uniform_name).second || sampler->stage >= 11 ||
            !stages.insert(sampler->stage).second ||
            !sampler->default_texture.starts_with("builtin/") ||
            !valid_path(sampler->default_texture))
        {
            return false;
        }
    }
    return true;
}

bool ShaderResourceCodec::decode(const ResourceCatalog& catalog, ByteView payload,
                                 Resource& output) const
{
    const auto& entry = output.info();
    std::unique_ptr<schema::ShaderDataT> data;
    if (entry.path.starts_with("builtin/"))
    {
        const auto* builtin = ResourceSystem::find_shader(entry.path);
        if (!builtin)
        {
            return false;
        }
        data = std::make_unique<schema::ShaderDataT>(*builtin);
    }
    else
    {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        flatbuffers::Verifier verifier(bytes, payload.size());
        if (!schema::VerifyShaderDataBuffer(verifier))
        {
            return false;
        }
        data = schema::UnPackShaderData(bytes);
    }
    if (data->path != entry.path || !validate(*data))
    {
        return false;
    }
    for (const auto& sampler : data->samplers)
    {
        Guid guid;
        if (!catalog.identities().path_to_guid(sampler->default_texture, guid) ||
            !declares(catalog, output, guid, schema::AssetKind::texture))
        {
            return false;
        }
    }
    store(output, std::move(data));
    return true;
}

bool ShaderResourceCodec::validate_dependencies(const ResourceCatalog& catalog,
                                                const Resource& resource,
                                                std::span<const ResourcePtr> dependencies) const
{
    const auto* shader = resource.shader();
    if (!shader)
    {
        return false;
    }
    for (const auto& sampler : shader->samplers)
    {
        Guid guid;
        if (!catalog.identities().path_to_guid(sampler->default_texture, guid))
        {
            return false;
        }
        const auto* texture = dependency(dependencies, guid);
        if (!texture || !texture->texture() || texture->texture()->cube_map)
        {
            return false;
        }
    }
    return true;
}

} // namespace engine
