#include "editor/asset_pipeline/importers/mesh_importer.hpp"
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

#if ENGINE_HAS_MODELING_PLUGIN
#include "plugins/editor/modeling/editable_mesh.hpp"
#endif

namespace engine::asset_pipeline
{
schema::AssetKind MeshImporter::kind() const noexcept
{
    return schema::AssetKind::mesh;
}
bool MeshImporter::matches(const std::filesystem::path& source) const
{
    return source_extension(source) == ".mesh";
}
bool MeshImporter::inspect_batch(const ImportContext&, std::string& fingerprint, std::string& error)
{
    return schema_fingerprint({"mesh.fbs", "modeling.fbs"}, fingerprint, error);
}
bool MeshImporter::convert(const ImportAsset& asset, const ImportContext& context,
                           std::string& error) const
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
#if ENGINE_HAS_MODELING_PLUGIN
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
#else
        error = "modeling mesh source requires the modeling plugin: " + asset.path;
        return false;
#endif
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

} // namespace engine::asset_pipeline
