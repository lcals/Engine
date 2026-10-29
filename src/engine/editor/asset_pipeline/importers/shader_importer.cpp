#include "editor/asset_pipeline/importers/shader_importer.hpp"
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
bool shader_target(std::string_view target, std::string& platform, std::string& profile)
{
    if (target == "macos" || target == "ios")
    {
        platform = target == "macos" ? "osx" : "ios";
        profile = "metal";
        return true;
    }
    if (target == "windows")
    {
        platform = "windows";
        profile = "s_5_0";
        return true;
    }
    if (target == "android" || target == "linux")
    {
        platform = target;
        profile = "spirv";
        return true;
    }
    return false;
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

} // namespace

schema::AssetKind ShaderImporter::kind() const noexcept
{
    return schema::AssetKind::shader;
}
bool ShaderImporter::matches(const std::filesystem::path& source) const
{
    return source_extension(source) == ".shader";
}
bool ShaderImporter::convert(const ImportAsset& asset, const ImportContext& context,
                             std::string& error) const
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
    std::string shader_platform;
    std::string shader_profile;
    if (!shader_target(context.target_platform, shader_platform, shader_profile))
    {
        error = "unsupported shader target: " + context.target_platform;
        return false;
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
                                                 shader_platform,
                                                 "--profile",
                                                 shader_profile,
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

bool ShaderImporter::inspect_batch(const ImportContext& context, std::string& fingerprint,
                                   std::string& error)
{
    std::string platform;
    std::string profile;
    std::string version;
    std::string include_hash;
    if (!shader_target(context.target_platform, platform, profile) ||
        !schema_fingerprint({"shader.fbs"}, fingerprint, error) ||
        !run_process({ENGINE_SHADERC_EXECUTABLE, "--version"}, error, &version) ||
        !file_hash(std::filesystem::path(ENGINE_BGFX_SHADER_INCLUDE_DIR) / "bgfx_shader.sh",
                   include_hash, error))
    {
        if (error.empty())
        {
            error = "cannot inspect shader compiler for target: " + context.target_platform;
        }
        return false;
    }
    fingerprint += ":" + platform + ":" + profile + ":" + version + ":" + include_hash;
    return true;
}

bool ShaderImporter::scan_dependencies(ImportAsset& asset, const ImportContext& context,
                                       std::string& error) const
{
    asset.build_dependencies.clear();
    asset.runtime_dependencies.clear();
    toml::table table;
    if (!read_table(asset.source, table, error))
    {
        return false;
    }
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
                !builtin_texture((*sampler)["default_texture"].value_or(std::string{}), context,
                                 asset.runtime_dependencies, error))
            {
                if (error.empty())
                {
                    error = "shader sampler must be a table";
                }
                return false;
            }
        }
    }
    asset.build_dependencies = asset.runtime_dependencies;
    for (const auto field : {"vertex", "fragment", "varying"})
    {
        if (!reference(table[field].value_or(std::string{}), schema::AssetKind::source, context,
                       asset.build_dependencies, error))
        {
            return false;
        }
    }
    // 保守追踪全部普通源，覆盖任意扩展名的include。
    for (const auto& [guid, kind] : context.kinds)
    {
        std::string path;
        if (kind == schema::AssetKind::source && context.identities.guid_to_path(guid, path) &&
            std::find(asset.build_dependencies.begin(), asset.build_dependencies.end(), guid) ==
                asset.build_dependencies.end())
        {
            asset.build_dependencies.push_back(guid);
        }
    }
    std::sort(asset.build_dependencies.begin(), asset.build_dependencies.end());
    return true;
}

} // namespace engine::editor::asset_pipeline
