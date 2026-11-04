#include "plugins/editor/asset_build/builtin_resources.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/editor_tools.hpp"
#include "runtime/resource/builtin_resources.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/schema/shader_generated.h"
#include "runtime/utils/path_utils.hpp"

#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <array>
#include <memory>
#include <span>
#include <system_error>
#include <vector>

namespace engine::plugins::asset_build
{
namespace
{

namespace pipeline = engine::editor::asset_pipeline;

bool inspect_cache_path(const std::filesystem::path& path, std::string& error)
{
    std::filesystem::path current;
    std::error_code code;
    for (const auto& part : path)
    {
        current /= part;
        const auto status = std::filesystem::symlink_status(current, code);
        if (code == std::errc::no_such_file_or_directory)
        {
            code.clear();
            continue;
        }
        if (code || std::filesystem::is_symlink(status))
        {
            error = "builtin shader cache is inaccessible or contains a symlink: " +
                    PathUtils::to_utf8(current);
            return false;
        }
    }
    return true;
}

bool compile_shaders(std::string_view target, const std::filesystem::path& cache,
                     std::map<std::string, std::vector<std::uint8_t>>& shaders, std::string& error)
{
    std::filesystem::path compiler;
    std::filesystem::path includes;
    std::filesystem::path sources;
    std::string platform;
    std::string profile;
    if (!pipeline::shader_target(target, platform, profile) ||
        !pipeline::editor_tool_path("shaderc", ENGINE_SHADERC_EXECUTABLE, compiler, error) ||
        !pipeline::editor_tool_data_path("bgfx", ENGINE_BGFX_SHADER_INCLUDE_DIR, includes, error) ||
        !pipeline::editor_tool_data_path("builtin-shaders", ENGINE_BUILTIN_SHADER_SOURCE_DIR,
                                         sources, error))
    {
        if (error.empty())
        {
            error = "unsupported builtin shader target: " + std::string(target);
        }
        return false;
    }
    std::vector<std::filesystem::path> inputs{compiler};
    for (const auto name : {"vs_cube.sc", "fs_cube.sc", "vs_font.sc", "fs_font.sc",
                            "varying.def.sc", "font_varying.def.sc", "pbr_helpers.sh",
                            "pbr_surface.sh", "pbr_lighting.sh", "dfg_approx.sh", "color_space.sh",
                            "material_helpers.sh", "shadow_helpers.sh", "csm_helpers.sh",
                            "local_shadow_helpers.sh", "cluster/lighting.sh"})
    {
        inputs.push_back(sources / name);
    }
    std::error_code code;
    std::filesystem::directory_iterator iterator(includes, code);
    if (code)
    {
        error = "cannot inspect builtin shader includes: " + code.message();
        return false;
    }
    for (const auto end = std::filesystem::directory_iterator(); iterator != end;
         iterator.increment(code))
    {
        if (code)
        {
            break;
        }
        if (iterator->path().extension() == ".sh")
        {
            inputs.push_back(iterator->path());
        }
    }
    if (code)
    {
        error = "cannot enumerate builtin shader includes: " + code.message();
        return false;
    }
    std::sort(inputs.begin(), inputs.end());
    std::string fingerprint = "builtin-shaders-1:" + platform + ":" + profile;
    for (const auto& input : inputs)
    {
        ByteBuffer bytes;
        if (!pipeline::read_bytes(input, bytes, error))
        {
            return false;
        }
        fingerprint +=
            ":" + PathUtils::to_utf8(input.filename()) + ":" + pipeline::hash_bytes(bytes);
    }
    const auto directory = cache / "build" / ".builtin-shaders" / std::string(target) /
                           pipeline::hash_text(fingerprint);
    if (!inspect_cache_path(directory, error))
    {
        return false;
    }
    std::filesystem::create_directories(directory, code);
    if (code)
    {
        error = "cannot prepare builtin shader cache: " + code.message();
        return false;
    }
    for (const auto name : {"vs_cube", "fs_cube", "vs_font", "fs_font"})
    {
        const std::string shader_name(name);
        const auto output = directory / name;
        const auto hash_path = directory / (shader_name + ".hash");
        if (!inspect_cache_path(output, error) || !inspect_cache_path(hash_path, error))
        {
            return false;
        }
        ByteBuffer bytes;
        std::string previous_hash;
        const bool reusable = ResourceSystem::read_binary(output, bytes) && !bytes.empty() &&
                              ResourceSystem::read_text(hash_path, previous_hash) &&
                              pipeline::hash_bytes(bytes) == previous_hash;
        if (!reusable)
        {
            const auto varying = sources / (shader_name.ends_with("font") ? "font_varying.def.sc"
                                                                          : "varying.def.sc");
            const std::vector<std::string> arguments{
                PathUtils::to_utf8(compiler),
                "-f",
                PathUtils::to_utf8(sources / (shader_name + ".sc")),
                "-o",
                PathUtils::to_utf8(output),
                "--type",
                shader_name.starts_with("vs_") ? "vertex" : "fragment",
                "--platform",
                platform,
                "--profile",
                profile,
                "--varyingdef",
                PathUtils::to_utf8(varying),
                "-i",
                PathUtils::to_utf8(includes) + ";" + PathUtils::to_utf8(sources)};
            if (!pipeline::run_process(arguments, error) ||
                !pipeline::read_bytes(output, bytes, error) || bytes.empty() ||
                !pipeline::write_text(hash_path, pipeline::hash_bytes(bytes), error))
            {
                if (error.empty())
                {
                    error = "builtin shader compiler produced no data: " + shader_name;
                }
                return false;
            }
        }
        auto& destination = shaders[shader_name];
        destination.resize(bytes.size());
        std::transform(bytes.begin(), bytes.end(), destination.begin(),
                       [](std::byte value) { return std::to_integer<std::uint8_t>(value); });
    }
    return true;
}

} // namespace

bool prepare_builtin_resources(std::string_view target, const std::filesystem::path& cache,
                               schema::AssetCatalogDataT& catalog,
                               std::map<Guid, ByteBuffer>& entities, std::string& error)
{
    entities.clear();
    std::map<std::string, std::vector<std::uint8_t>> shaders;
    if (!compile_shaders(target, cache, shaders, error))
    {
        return false;
    }
    auto candidate = catalog;
    std::map<Guid, ByteBuffer> candidate_entities;
    for (const auto& definition : builtin_resource_catalog().entries)
    {
        ByteBuffer payload;
        if (!builtin_resource_payload(definition->guid, payload))
        {
            error = "cannot serialize builtin definition: " + definition->path;
            return false;
        }
        if (definition->kind == schema::AssetKind::shader)
        {
            auto shader = schema::UnPackShaderData(payload.data());
            const auto name = definition->path == "builtin://shaders/font" ? "font" : "cube";
            shader->vertex_code = shaders.find(std::string("vs_") + name)->second;
            shader->fragment_code = shaders.find(std::string("fs_") + name)->second;
            flatbuffers::FlatBufferBuilder builder;
            schema::FinishShaderDataBuffer(builder,
                                           schema::ShaderData::Pack(builder, shader.get()));
            const auto* begin = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
            payload.assign(begin, begin + builder.GetSize());
        }
        auto entry = std::make_unique<schema::AssetEntryT>(*definition);
        auto& entity = candidate_entities[entry->guid];
        if (!ResourceSystem::encode_resource(entry->guid, entry->kind, payload, entity,
                                             entry->header_size))
        {
            error = "cannot encode builtin resource: " + entry->path;
            return false;
        }
        entry->format_version = 1;
        entry->content_hash = pipeline::hash_bytes(entity);
        entry->content_size = entity.size();
        entry->location = std::make_unique<schema::FileLocationT>();
        entry->location->mount = "content";
        entry->location->path = "gen/" + entry->guid;
        entry->location->length = entry->content_size;
        candidate.entries.push_back(std::move(entry));
    }
    catalog = std::move(candidate);
    entities = std::move(candidate_entities);
    return true;
}

} // namespace engine::plugins::asset_build
