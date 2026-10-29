#include "editor/asset_pipeline/asset_importers.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/scene_importer.hpp"
#include "editor/asset_pipeline/texture_importer.hpp"
#include "editor/asset_pipeline/toml_helpers.hpp"
#include "plugins/modeling/editable_mesh.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/schema/material_generated.h"
#include "runtime/schema/mesh_generated.h"
#include "runtime/schema/shader_generated.h"
#include "runtime/utils/path_utils.hpp"

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace engine::asset_pipeline
{
namespace
{

bool read_table(const std::filesystem::path& path, toml::table& table, std::string& error)
{
    std::string text;
    if (!ResourceSystem::read_text(path, text))
    {
        error = "cannot read resource descriptor: " + PathUtils::to_utf8(path);
        return false;
    }
    auto result = toml::parse(text);
    if (!result)
    {
        error = "invalid resource TOML: " + PathUtils::to_utf8(path) + ": " +
                std::string(result.error().description());
        return false;
    }
    table = std::move(result).table();
    return true;
}

bool reference(const Guid& guid, schema::AssetKind kind, const ImportContext& context,
               std::vector<Guid>& dependencies, std::string& error)
{
    std::string path;
    const auto entry = context.kinds.find(guid);
    if (!GuidMap::valid_guid(guid) || !context.identities.guid_to_path(guid, path) ||
        entry == context.kinds.end() || entry->second != kind)
    {
        error = "missing resource GUID or incompatible resource type: " + guid;
        return false;
    }
    if (std::find(dependencies.begin(), dependencies.end(), guid) == dependencies.end())
    {
        dependencies.push_back(guid);
    }
    return true;
}

bool builtin_texture(std::string_view path, const ImportContext& context,
                     std::vector<Guid>& dependencies, std::string& error)
{
    Guid guid;
    if (!path.starts_with("builtin/") || !context.identities.path_to_guid(path, guid))
    {
        error = "shader sampler default must name a builtin texture: " + std::string(path);
        return false;
    }
    return reference(guid, schema::AssetKind::texture, context, dependencies, error);
}

bool uniform_name(std::string_view name)
{
    const auto letter = [](char value)
    { return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || value == '_'; };
    return !name.empty() && letter(name.front()) &&
           std::all_of(name.begin(), name.end(), [letter](char value)
                       { return letter(value) || (value >= '0' && value <= '9'); });
}

bool reserved_uniform(std::string_view name)
{
    constexpr std::array reserved = {"u_normal_transform",
                                     "u_ambient",
                                     "u_tonemap",
                                     "u_shadow_transform",
                                     "u_shadow_settings",
                                     "u_shadow_depth",
                                     "s_shadow_map",
                                     "u_camera_position",
                                     "u_light_direction",
                                     "u_light_color",
                                     "u_light_count",
                                     "u_light_position_range",
                                     "u_light_direction_outer",
                                     "u_light_color_intensity",
                                     "u_light_kind_inner",
                                     "u_viewRect",
                                     "u_viewTexel",
                                     "u_view",
                                     "u_invView",
                                     "u_proj",
                                     "u_invProj",
                                     "u_viewProj",
                                     "u_invViewProj",
                                     "u_model",
                                     "u_modelView",
                                     "u_modelViewProj",
                                     "u_alphaRef"};
    return std::find(reserved.begin(), reserved.end(), name) != reserved.end();
}

bool parameter_values(const schema::MaterialValueUnion& value, std::array<double, 4>& values,
                      std::size_t& size)
{
    values = {};
    size = 0;
    if (value.value == nullptr)
    {
        return false;
    }
    switch (value.type)
    {
    case schema::MaterialValue::FloatValue:
        values[0] = value.AsFloatValue()->value;
        size = 1;
        break;
    case schema::MaterialValue::Vec2Value:
    {
        const auto& data = value.AsVec2Value()->value;
        values = {data.x(), data.y(), 0, 0};
        size = 2;
        break;
    }
    case schema::MaterialValue::Vec3Value:
    {
        const auto& data = value.AsVec3Value()->value;
        values = {data.x(), data.y(), data.z(), 0};
        size = 3;
        break;
    }
    case schema::MaterialValue::Vec4Value:
    {
        const auto& data = value.AsVec4Value()->value;
        values = {data.x(), data.y(), data.z(), data.w()};
        size = 4;
        break;
    }
    default:
        return false;
    }
    return std::all_of(
        values.begin(), values.begin() + static_cast<std::ptrdiff_t>(size), [](double number)
        { return std::isfinite(number) && std::abs(number) <= std::numeric_limits<float>::max(); });
}

bool valid_parameter(const schema::ShaderParameterT& definition,
                     const schema::MaterialValueUnion& value)
{
    std::array<double, 4> values{};
    std::size_t size = 0;
    return value.type == definition.default_value.type && parameter_values(value, values, size) &&
           (!definition.bounded ||
            std::all_of(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(size),
                        [&](double number)
                        { return number >= definition.minimum && number <= definition.maximum; }));
}

bool load_shader(const Guid& guid, const ImportContext& context,
                 std::unique_ptr<schema::ShaderDataT>& shader, std::string& error)
{
    std::string path;
    if (!context.identities.guid_to_path(guid, path))
    {
        error = "shader identity is unavailable: " + guid;
        return false;
    }
    if (path.starts_with("builtin/"))
    {
        const auto* definition = ResourceSystem::find_shader(path);
        if (definition == nullptr)
        {
            error = "builtin shader is unavailable: " + path;
            return false;
        }
        shader = std::make_unique<schema::ShaderDataT>(*definition);
        return true;
    }
    std::vector<std::byte> bytes;
    if (!read_bytes(context.output / guid, bytes, error))
    {
        return false;
    }
    if (bytes.size() >= 4 && bytes[0] == std::byte{'E'} && bytes[1] == std::byte{'R'} &&
        bytes[2] == std::byte{'S'} && bytes[3] == std::byte{'1'})
    {
        const schema::AssetEntryT* entry = nullptr;
        if (context.previous_catalog != nullptr)
        {
            for (const auto& candidate : context.previous_catalog->entries)
            {
                if (candidate->guid == guid)
                {
                    entry = candidate.get();
                    break;
                }
            }
        }
        std::vector<std::byte> payload;
        if (entry == nullptr || !ResourceSystem::decode_resource(*entry, bytes, payload))
        {
            error = "reused shader entity is invalid: " + path;
            return false;
        }
        bytes = std::move(payload);
    }
    flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                   bytes.size());
    if (!schema::VerifyShaderDataBuffer(verifier))
    {
        error = "shader dependency has invalid binary: " + path;
        return false;
    }
    shader.reset(schema::GetShaderData(bytes.data())->UnPack());
    return true;
}

bool convert_mesh(const ImportAsset& asset, const ImportContext& context, std::string& error)
{
    toml::table table;
    if (!read_table(asset.source, table, error))
    {
        return false;
    }
    schema::MeshDataT mesh;
    mesh.path = asset.path;
    if (table.contains("modeling"))
    {
        const auto* modeling = table["modeling"].as_table();
        const auto format =
            modeling == nullptr ? std::string{} : (*modeling)["format"].value_or(std::string{});
        const auto encoded =
            modeling == nullptr ? std::string{} : (*modeling)["data"].value_or(std::string{});
        if (format != "flatbuffers-hex" || encoded.empty() || encoded.size() % 2 != 0 ||
            encoded.size() > 128 * 1024 * 1024)
        {
            error = "modeling source requires bounded flatbuffers-hex data";
            return false;
        }
        const auto digit = [](char value) -> int
        {
            if (value >= '0' && value <= '9')
            {
                return value - '0';
            }
            if (value >= 'a' && value <= 'f')
            {
                return value - 'a' + 10;
            }
            if (value >= 'A' && value <= 'F')
            {
                return value - 'A' + 10;
            }
            return -1;
        };
        std::vector<std::uint8_t> bytes(encoded.size() / 2);
        for (std::size_t index = 0; index < bytes.size(); ++index)
        {
            const int high = digit(encoded[index * 2]);
            const int low = digit(encoded[index * 2 + 1]);
            if (high < 0 || low < 0)
            {
                error = "modeling source contains invalid hexadecimal data";
                return false;
            }
            bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
        }
        schema::EditableMeshDataT editable;
        if (!EditableMesh::deserialize(bytes, editable, error) ||
            !EditableMesh::compile(editable, asset.path, mesh, error))
        {
            return false;
        }
        flatbuffers::FlatBufferBuilder builder;
        schema::FinishMeshDataBuffer(builder, schema::MeshData::Pack(builder, &mesh));
        return write_bytes(context.output / asset.guid,
                           std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())),
                           error);
    }
    const auto* vertices = table["vertices"].as_array();
    const auto* indices = table["indices"].as_array();
    if (vertices == nullptr || vertices->empty() || indices == nullptr || indices->empty() ||
        indices->size() % 3 != 0)
    {
        error = "mesh requires vertices and triangle indices";
        return false;
    }
    for (const auto& item : *vertices)
    {
        std::array<double, 6> values{};
        if (!numbers(&item, values) ||
            std::any_of(values.begin(), values.end(), [](double value)
                        { return std::abs(value) > std::numeric_limits<float>::max(); }))
        {
            error = "mesh vertex must contain six finite float values";
            return false;
        }
        const double normal_length =
            values[3] * values[3] + values[4] * values[4] + values[5] * values[5];
        if (normal_length < 0.000001)
        {
            error = "mesh vertex normal cannot be zero";
            return false;
        }
        mesh.vertices.emplace_back(static_cast<float>(values[0]), static_cast<float>(values[1]),
                                   static_cast<float>(values[2]), static_cast<float>(values[3]),
                                   static_cast<float>(values[4]), static_cast<float>(values[5]));
    }
    for (const auto& item : *indices)
    {
        const auto index = item.value<std::int64_t>();
        if (!index || *index < 0 || static_cast<std::uint64_t>(*index) >= mesh.vertices.size() ||
            static_cast<std::uint64_t>(*index) > std::numeric_limits<std::uint32_t>::max())
        {
            error = "mesh index is out of vertex range";
            return false;
        }
        mesh.indices.push_back(static_cast<std::uint32_t>(*index));
    }
    for (const char* name : {"texcoords", "tangents", "colors"})
    {
        if (!table.contains(name))
        {
            continue;
        }
        const auto* values = table[name].as_array();
        if (values == nullptr || values->size() != mesh.vertices.size())
        {
            error = std::string(name) + " must contain one value per mesh vertex";
            return false;
        }
        for (const auto& item : *values)
        {
            if (std::string_view(name) == "texcoords")
            {
                std::array<double, 2> value{};
                if (!numbers(&item, value) ||
                    std::any_of(value.begin(), value.end(), [](double number)
                                { return std::abs(number) > std::numeric_limits<float>::max(); }))
                {
                    error = "mesh texcoord requires two finite float values";
                    return false;
                }
                mesh.texcoords.emplace_back(value[0], value[1]);
                continue;
            }
            std::array<double, 4> value{};
            if (!numbers(&item, value) ||
                std::any_of(value.begin(), value.end(), [](double number)
                            { return std::abs(number) > std::numeric_limits<float>::max(); }))
            {
                error = "mesh tangent/color requires four finite float values";
                return false;
            }
            if (std::string_view(name) == "tangents")
            {
                if ((value[0] == 0 && value[1] == 0 && value[2] == 0) ||
                    (value[3] != -1 && value[3] != 1))
                {
                    error = "mesh tangent must have nonzero direction and handedness -1 or 1";
                    return false;
                }
                mesh.tangents.emplace_back(value[0], value[1], value[2], value[3]);
            }
            else
            {
                if (std::any_of(value.begin(), value.end(),
                                [](double number) { return number < 0 || number > 1; }))
                {
                    error = "mesh color channels must be in [0, 1]";
                    return false;
                }
                mesh.colors.emplace_back(static_cast<float>(value[0]), static_cast<float>(value[1]),
                                         static_cast<float>(value[2]),
                                         static_cast<float>(value[3]));
            }
        }
    }
    if (table.contains("submeshes"))
    {
        const auto* submeshes = table["submeshes"].as_array();
        if (submeshes == nullptr || submeshes->empty())
        {
            error = "mesh submeshes must be a nonempty array";
            return false;
        }
        std::uint64_t next = 0;
        for (const auto& item : *submeshes)
        {
            const auto* range = item.as_table();
            std::uint64_t first = 0;
            std::uint64_t count = 0;
            std::uint64_t slot = 0;
            if (range == nullptr || !identifier(range->get("first_index"), first) ||
                !identifier(range->get("index_count"), count) ||
                !identifier(range->get("material_slot"), slot) || first != next || count == 0 ||
                first % 3 != 0 || count % 3 != 0 || first > mesh.indices.size() ||
                count > mesh.indices.size() - first || slot > 15)
            {
                error = "submeshes must cover ordered triangle ranges with material slots 0..15";
                return false;
            }
            mesh.submeshes.emplace_back(static_cast<std::uint32_t>(first),
                                        static_cast<std::uint32_t>(count),
                                        static_cast<std::uint32_t>(slot));
            next = first + count;
        }
        if (next != mesh.indices.size())
        {
            error = "mesh submeshes must cover every triangle";
            return false;
        }
    }
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishMeshDataBuffer(builder, schema::MeshData::Pack(builder, &mesh));
    return write_bytes(context.output / asset.guid,
                       std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())),
                       error);
}

bool convert_shader(const ImportAsset& asset, const ImportContext& context, std::string& error)
{
    toml::table table;
    if (!read_table(asset.source, table, error))
    {
        return false;
    }
    schema::ShaderDataT shader;
    shader.path = asset.path;
    const auto* parameters = table["parameters"].as_array();
    if (table.contains("parameters") && parameters == nullptr)
    {
        error = "shader parameters must be an array";
        return false;
    }
    std::set<std::string> names;
    std::map<std::string, unsigned int> slots;
    if (parameters != nullptr)
    {
        for (const auto& item : *parameters)
        {
            const auto* parameter_table = item.as_table();
            if (parameter_table == nullptr)
            {
                error = "shader parameter must be a table";
                return false;
            }
            auto parameter = std::make_unique<schema::ShaderParameterT>();
            parameter->name = (*parameter_table)["name"].value_or(std::string{});
            parameter->uniform_name = (*parameter_table)["uniform"].value_or(std::string{});
            const auto offset = (*parameter_table)["offset"].value_or(std::int64_t{0});
            const auto uniform_letter = [](char character)
            {
                return (character >= 'a' && character <= 'z') ||
                       (character >= 'A' && character <= 'Z') || character == '_';
            };
            if (parameter->name.empty() || !names.insert(parameter->name).second ||
                parameter->uniform_name.empty() || !uniform_letter(parameter->uniform_name[0]) ||
                !std::all_of(parameter->uniform_name.begin(), parameter->uniform_name.end(),
                             [&](char character)
                             {
                                 return uniform_letter(character) ||
                                        (character >= '0' && character <= '9');
                             }) ||
                offset < 0 || offset > 3 ||
                (parameter_table->contains("offset") &&
                 !(*parameter_table)["offset"].is_integer()) ||
                !value_union(*parameter_table, parameter->default_value, error))
            {
                if (error.empty())
                {
                    error = "invalid or duplicate shader parameter";
                }
                return false;
            }
            if (reserved_uniform(parameter->uniform_name))
            {
                error = "shader parameter uses an engine-reserved uniform";
                return false;
            }
            parameter->uniform_offset = static_cast<std::uint8_t>(offset);
            parameter->exposed = (*parameter_table)["exposed"].value_or(true);
            parameter->bounded = (*parameter_table)["bounded"].value_or(false);
            if ((parameter_table->contains("exposed") &&
                 !(*parameter_table)["exposed"].is_boolean()) ||
                (parameter_table->contains("bounded") &&
                 !(*parameter_table)["bounded"].is_boolean()) ||
                (parameter_table->contains("minimum") &&
                 !number(parameter_table->get("minimum"), parameter->minimum)) ||
                (parameter_table->contains("maximum") &&
                 !number(parameter_table->get("maximum"), parameter->maximum)) ||
                parameter->minimum > parameter->maximum ||
                !valid_parameter(*parameter, parameter->default_value))
            {
                error = "invalid shader parameter bounds/default";
                return false;
            }
            std::array<double, 4> values{};
            std::size_t size = 0;
            if (!parameter_values(parameter->default_value, values, size) || size + offset > 4)
            {
                error = "shader parameter does not fit a vec4 uniform";
                return false;
            }
            const auto mask = static_cast<unsigned int>(((1U << size) - 1U) << offset);
            auto& occupied = slots[parameter->uniform_name];
            if ((occupied & mask) != 0)
            {
                error = "shader parameter overlaps an existing uniform slot";
                return false;
            }
            occupied |= mask;
            shader.parameters.push_back(std::move(parameter));
        }
    }
    if (table.contains("samplers"))
    {
        const auto* samplers = table["samplers"].as_array();
        if (samplers == nullptr)
        {
            error = "shader samplers must be an array";
            return false;
        }
        std::set<std::string> uniforms;
        std::set<std::int64_t> stages;
        for (const auto& item : *samplers)
        {
            const auto* sampler_table = item.as_table();
            if (sampler_table == nullptr)
            {
                error = "shader sampler must be a table";
                return false;
            }
            auto sampler = std::make_unique<schema::ShaderSamplerT>();
            sampler->name = (*sampler_table)["name"].value_or(std::string{});
            sampler->uniform_name = (*sampler_table)["uniform"].value_or(std::string{});
            sampler->default_texture = (*sampler_table)["default_texture"].value_or(std::string{});
            sampler->exposed = (*sampler_table)["exposed"].value_or(true);
            const auto stage = (*sampler_table)["stage"].value<std::int64_t>();
            std::vector<Guid> dependencies;
            if (sampler->name.empty() || !names.insert(sampler->name).second ||
                !uniform_name(sampler->uniform_name) || slots.contains(sampler->uniform_name) ||
                reserved_uniform(sampler->uniform_name) ||
                !uniforms.insert(sampler->uniform_name).second ||
                !(*sampler_table)["stage"].is_integer() || !stage || *stage < 0 || *stage >= 15 ||
                !stages.insert(*stage).second ||
                (sampler_table->contains("exposed") && !(*sampler_table)["exposed"].is_boolean()) ||
                !builtin_texture(sampler->default_texture, context, dependencies, error))
            {
                if (error.empty())
                {
                    error = "invalid or duplicate shader sampler name, uniform or stage";
                }
                return false;
            }
            sampler->stage = static_cast<std::uint8_t>(*stage);
            shader.samplers.push_back(std::move(sampler));
        }
    }
    std::string varying_path;
    if (!context.identities.guid_to_path(table["varying"].value_or(std::string{}), varying_path))
    {
        error = "shader varying definition is missing";
        return false;
    }
    const auto varying = context.assets / PathUtils::from_utf8(varying_path.substr(8));
    for (const auto stage : {std::string_view("vertex"), std::string_view("fragment")})
    {
        std::string source_path;
        if (!context.identities.guid_to_path(table[stage].value_or(std::string{}), source_path))
        {
            error = "shader source is missing";
            return false;
        }
        const auto source = context.assets / PathUtils::from_utf8(source_path.substr(8));
        auto compiled = context.output.parent_path() / (asset.guid + "." + std::string(stage));
        const std::vector<std::string> arguments{ENGINE_SHADERC_EXECUTABLE,
                                                 "-f",
                                                 PathUtils::to_utf8(source),
                                                 "-o",
                                                 PathUtils::to_utf8(compiled),
                                                 "--type",
                                                 std::string(stage),
                                                 "--platform",
                                                 context.shader_platform,
                                                 "--profile",
                                                 context.shader_profile,
                                                 "--varyingdef",
                                                 PathUtils::to_utf8(varying),
                                                 "-i",
                                                 std::string(ENGINE_BGFX_SHADER_INCLUDE_DIR) + ";" +
                                                     PathUtils::to_utf8(source.parent_path()) +
                                                     ";" + PathUtils::to_utf8(context.assets)};
        if (!run_process(arguments, error))
        {
            return false;
        }
        std::vector<std::byte> bytes;
        if (!read_bytes(compiled, bytes, error) || bytes.empty())
        {
            return false;
        }
        auto& destination = stage == "vertex" ? shader.vertex_code : shader.fragment_code;
        destination.resize(bytes.size());
        std::transform(bytes.begin(), bytes.end(), destination.begin(),
                       [](std::byte value) { return std::to_integer<std::uint8_t>(value); });
        std::error_code cleanup_error;
        std::filesystem::remove(compiled, cleanup_error);
    }
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishShaderDataBuffer(builder, schema::ShaderData::Pack(builder, &shader));
    return write_bytes(context.output / asset.guid,
                       std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())),
                       error);
}

bool convert_material(const ImportAsset& asset, const ImportContext& context, std::string& error)
{
    toml::table table;
    if (!read_table(asset.source, table, error))
    {
        return false;
    }
    schema::MaterialDataT material;
    material.type_path = asset.path;
    material.shader_guid = table["shader"].value_or(std::string{});
    if (!context.identities.guid_to_path(material.shader_guid, material.shader_path))
    {
        error = "material shader reference is missing";
        return false;
    }
    std::unique_ptr<schema::ShaderDataT> shader;
    if (!load_shader(material.shader_guid, context, shader, error))
    {
        return false;
    }
    const auto* parameters = table["parameters"].as_array();
    if (table.contains("parameters") && parameters == nullptr)
    {
        error = "material parameters must be an array";
        return false;
    }
    std::set<std::string> names;
    if (parameters != nullptr)
    {
        for (const auto& item : *parameters)
        {
            const auto* parameter_table = item.as_table();
            if (parameter_table == nullptr)
            {
                error = "material parameter must be a table";
                return false;
            }
            auto parameter = std::make_unique<schema::MaterialParameterT>();
            parameter->name = (*parameter_table)["name"].value_or(std::string{});
            const auto definition =
                std::find_if(shader->parameters.begin(), shader->parameters.end(),
                             [&](const auto& value) { return value->name == parameter->name; });
            if (!names.insert(parameter->name).second || definition == shader->parameters.end() ||
                !(*definition)->exposed ||
                !value_union(*parameter_table, parameter->value, error) ||
                !valid_parameter(**definition, parameter->value))
            {
                if (error.empty())
                {
                    error = "material parameter is unknown, duplicated, hidden or out of range: " +
                            parameter->name;
                }
                return false;
            }
            material.parameters.push_back(std::move(parameter));
        }
    }
    if (table.contains("textures"))
    {
        const auto* textures = table["textures"].as_array();
        if (textures == nullptr)
        {
            error = "material textures must be an array";
            return false;
        }
        for (const auto& item : *textures)
        {
            const auto* texture_table = item.as_table();
            if (texture_table == nullptr)
            {
                error = "material texture must be a table";
                return false;
            }
            auto texture = std::make_unique<schema::MaterialTextureT>();
            texture->name = (*texture_table)["name"].value_or(std::string{});
            texture->texture_guid = (*texture_table)["texture"].value_or(std::string{});
            const auto definition =
                std::find_if(shader->samplers.begin(), shader->samplers.end(),
                             [&](const auto& value) { return value->name == texture->name; });
            std::vector<Guid> dependencies;
            if (!names.insert(texture->name).second || definition == shader->samplers.end() ||
                !(*definition)->exposed ||
                !reference(texture->texture_guid, schema::AssetKind::texture, context, dependencies,
                           error) ||
                !context.identities.guid_to_path(texture->texture_guid, texture->texture_path))
            {
                if (error.empty())
                {
                    error = "material texture is unknown, duplicated, hidden or invalid: " +
                            texture->name;
                }
                return false;
            }
            material.textures.push_back(std::move(texture));
        }
    }
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishMaterialDataBuffer(builder, schema::MaterialData::Pack(builder, &material));
    return write_bytes(context.output / asset.guid,
                       std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())),
                       error);
}

} // namespace

bool scan_dependencies(ImportAsset& asset, const ImportContext& context, std::string& error)
{
    asset.build_dependencies.clear();
    asset.runtime_dependencies.clear();
    if (asset.kind == schema::AssetKind::scene)
    {
        schema::SceneDataT scene;
        if (!SceneImporter::read(asset.source, scene, error))
        {
            return false;
        }
        for (const auto& node : scene.nodes)
        {
            for (const auto& component : node->components)
            {
                if (const auto* mesh = component->payload.AsMeshRendererData())
                {
                    if (!reference(mesh->mesh_guid, schema::AssetKind::mesh, context,
                                   asset.build_dependencies, error) ||
                        !reference(mesh->material_guid, schema::AssetKind::material, context,
                                   asset.build_dependencies, error))
                    {
                        return false;
                    }
                    for (const auto& material : mesh->additional_material_guids)
                    {
                        if (!reference(material, schema::AssetKind::material, context,
                                       asset.build_dependencies, error))
                        {
                            return false;
                        }
                    }
                }
            }
        }
    }
    else if (asset.kind == schema::AssetKind::material || asset.kind == schema::AssetKind::shader)
    {
        toml::table table;
        if (!read_table(asset.source, table, error))
        {
            return false;
        }
        if (asset.kind == schema::AssetKind::material)
        {
            if (!reference(table["shader"].value_or(std::string{}), schema::AssetKind::shader,
                           context, asset.build_dependencies, error))
            {
                return false;
            }
            if (table.contains("textures"))
            {
                const auto* textures = table["textures"].as_array();
                if (textures == nullptr)
                {
                    error = "material textures must be an array";
                    return false;
                }
                for (const auto& item : *textures)
                {
                    const auto* texture = item.as_table();
                    if (texture == nullptr ||
                        !reference((*texture)["texture"].value_or(std::string{}),
                                   schema::AssetKind::texture, context, asset.build_dependencies,
                                   error))
                    {
                        if (error.empty())
                        {
                            error = "material texture must be a table";
                        }
                        return false;
                    }
                }
            }
        }
        else
        {
            if (table.contains("samplers"))
            {
                const auto* samplers = table["samplers"].as_array();
                if (samplers == nullptr)
                {
                    error = "shader samplers must be an array";
                    return false;
                }
                for (const auto& item : *samplers)
                {
                    const auto* sampler = item.as_table();
                    if (sampler == nullptr ||
                        !builtin_texture((*sampler)["default_texture"].value_or(std::string{}),
                                         context, asset.runtime_dependencies, error))
                    {
                        if (error.empty())
                        {
                            error = "shader sampler must be a table";
                        }
                        return false;
                    }
                }
                asset.build_dependencies = asset.runtime_dependencies;
            }
            for (const auto field : {"vertex", "fragment", "varying"})
            {
                if (!reference(table[field].value_or(std::string{}), schema::AssetKind::source,
                               context, asset.build_dependencies, error))
                {
                    return false;
                }
            }
            // #include 不限制扩展名，追踪全部普通源以覆盖 .inc/.h 等依赖。
            for (const auto& [guid, kind] : context.kinds)
            {
                std::string path;
                if (kind == schema::AssetKind::source &&
                    context.identities.guid_to_path(guid, path) &&
                    std::find(asset.build_dependencies.begin(), asset.build_dependencies.end(),
                              guid) == asset.build_dependencies.end())
                {
                    asset.build_dependencies.push_back(guid);
                }
            }
        }
    }
    std::sort(asset.build_dependencies.begin(), asset.build_dependencies.end());
    if (asset.kind == schema::AssetKind::scene || asset.kind == schema::AssetKind::material)
    {
        asset.runtime_dependencies = asset.build_dependencies;
    }
    return true;
}

bool convert_asset(const ImportAsset& asset, const ImportContext& context, std::string& error)
{
    if (asset.kind == schema::AssetKind::texture)
    {
        return TextureImporter::convert(asset, context, error);
    }
    if (asset.kind == schema::AssetKind::mesh)
    {
        return convert_mesh(asset, context, error);
    }
    if (asset.kind == schema::AssetKind::shader)
    {
        return convert_shader(asset, context, error);
    }
    if (asset.kind == schema::AssetKind::material)
    {
        return convert_material(asset, context, error);
    }
    if (asset.kind == schema::AssetKind::scene)
    {
        schema::SceneDataT scene;
        if (!SceneImporter::read(asset.source, scene, error))
        {
            return false;
        }
        flatbuffers::FlatBufferBuilder builder;
        schema::FinishSceneDataBuffer(builder, schema::SceneData::Pack(builder, &scene));
        return write_bytes(context.output / asset.guid,
                           std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())),
                           error);
    }
    std::vector<std::byte> bytes;
    return read_bytes(asset.source, bytes, error) &&
           write_bytes(context.output / asset.guid, bytes, error);
}

} // namespace engine::asset_pipeline
