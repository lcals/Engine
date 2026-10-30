#include "plugins/editor/modeling/modeling_mesh_importer.hpp"

#include "editor/asset_pipeline/importer_utils.hpp"
#include "plugins/editor/modeling/editable_mesh.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL.h>
#include <flatbuffers/flatbuffers.h>
#include <toml++/toml.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace engine::plugins::modeling
{
namespace
{
int hex_digit(char value) noexcept
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
}

bool write_mesh(const std::filesystem::path& path, const schema::MeshDataT& mesh,
                std::string& error)
{
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishMeshDataBuffer(builder, schema::MeshData::Pack(builder, &mesh));
    SDL_IOStream* stream = SDL_IOFromFile(PathUtils::to_utf8(path).c_str(), "wb");
    if (stream == nullptr)
    {
        error = "cannot create modeling mesh output: " + PathUtils::to_utf8(path) + ": " +
                SDL_GetError();
        return false;
    }
    const bool written =
        SDL_WriteIO(stream, builder.GetBufferPointer(), builder.GetSize()) == builder.GetSize();
    const bool flushed = written && SDL_FlushIO(stream);
    const bool closed = SDL_CloseIO(stream);
    if (!written || !flushed || !closed)
    {
        error = "cannot finish modeling mesh output: " + PathUtils::to_utf8(path) + ": " +
                SDL_GetError();
        return false;
    }
    return true;
}
} // namespace

bool ModelingMeshImporter::inspect_batch(const editor::asset_pipeline::ImportContext& context,
                                         std::string& fingerprint, std::string& error)
{
    if (!MeshImporter::inspect_batch(context, fingerprint, error))
    {
        return false;
    }
    std::string schema_hash;
    if (!editor::asset_pipeline::schema_fingerprint({"modeling.fbs"}, schema_hash, error))
    {
        fingerprint.clear();
        return false;
    }
    fingerprint += ":modeling-importer-1:" + schema_hash;
    return true;
}

bool ModelingMeshImporter::convert(const editor::asset_pipeline::ImportAsset& asset,
                                   const editor::asset_pipeline::ImportContext& context,
                                   std::string& error) const
{
    std::string text;
    if (!ResourceSystem::read_text(asset.source, text))
    {
        error = "cannot read modeling mesh source: " + PathUtils::to_utf8(asset.source);
        return false;
    }
    auto parsed = toml::parse(text);
    if (!parsed)
    {
        error = "invalid modeling mesh TOML: " + PathUtils::to_utf8(asset.source) + ": " +
                std::string(parsed.error().description());
        return false;
    }
    const auto& table = parsed.table();
    if (!table.contains("modeling"))
    {
        return MeshImporter::convert(asset, context, error);
    }
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
    std::vector<std::uint8_t> bytes(encoded.size() / 2);
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        const int high = hex_digit(encoded[index * 2]);
        const int low = hex_digit(encoded[index * 2 + 1]);
        if (high < 0 || low < 0)
        {
            error = "modeling source contains invalid hexadecimal data";
            return false;
        }
        bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    schema::EditableMeshDataT editable;
    schema::MeshDataT mesh;
    if (!EditableMesh::deserialize(bytes, editable, error) ||
        !EditableMesh::compile(editable, asset.path, mesh, error))
    {
        return false;
    }
    return write_mesh(context.output / asset.guid, mesh, error);
}
} // namespace engine::plugins::modeling
