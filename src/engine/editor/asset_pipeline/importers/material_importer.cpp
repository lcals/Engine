#include "editor/asset_pipeline/importers/material_importer.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/toml_helpers.hpp"
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

namespace engine::editor::asset_pipeline
{
namespace
{
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

} // namespace

schema::AssetKind MaterialImporter::kind() const noexcept
{
    return schema::AssetKind::material;
}
bool MaterialImporter::matches(const std::filesystem::path& source) const
{
    return source_extension(source) == ".material";
}
bool MaterialImporter::inspect_batch(const ImportContext&, std::string& fingerprint,
                                     std::string& error)
{
    return schema_fingerprint({"material.fbs"}, fingerprint, error);
}
bool MaterialImporter::convert(const ImportAsset& asset, const ImportContext& context,
                               std::string& error) const
{
    toml::table table;
    if (!read_table(asset.source, table, error))
    {
        return false;
    }
    schema::MaterialDataT material;
    material.type_path = asset.path;
    if (table.contains("cull"))
    {
        const auto cull = table["cull"].value<std::string>();
        if (!cull || (*cull != "back" && *cull != "front" && *cull != "none"))
        {
            error = "material cull must be back, front or none";
            return false;
        }
        material.cull_mode = *cull == "back"    ? schema::CullMode::back
                             : *cull == "front" ? schema::CullMode::front
                                                : schema::CullMode::none;
    }
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

bool MaterialImporter::scan_dependencies(ImportAsset& asset, const ImportContext& context,
                                         std::string& error) const
{
    asset.build_dependencies.clear();
    asset.runtime_dependencies.clear();
    toml::table table;
    if (!read_table(asset.source, table, error) ||
        !reference(table["shader"].value_or(std::string{}), schema::AssetKind::shader, context,
                   asset.build_dependencies, error))
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
                           schema::AssetKind::texture, context, asset.build_dependencies, error))
            {
                if (error.empty())
                {
                    error = "material texture must be a table";
                }
                return false;
            }
        }
    }
    std::sort(asset.build_dependencies.begin(), asset.build_dependencies.end());
    asset.runtime_dependencies = asset.build_dependencies;
    return true;
}

} // namespace engine::editor::asset_pipeline
