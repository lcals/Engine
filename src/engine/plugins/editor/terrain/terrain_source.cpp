#include "plugins/editor/terrain/terrain_source.hpp"

#include "editor/asset_pipeline/toml_helpers.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <utility>

namespace engine::plugins::terrain
{
namespace
{

bool fields(const toml::table& table, std::initializer_list<std::string_view> names,
            std::string& error)
{
    for (const auto& [name, value] : table)
    {
        static_cast<void>(value);
        if (std::find(names.begin(), names.end(), name.str()) == names.end())
        {
            error = "unknown terrain source field: " + std::string(name.str());
            return false;
        }
    }
    return true;
}

bool vector(const toml::node* node, schema::Vec3& output)
{
    std::array<double, 3> values{};
    if (!editor::asset_pipeline::numbers(node, values) ||
        !std::all_of(values.begin(), values.end(), [](double value)
                     { return std::abs(value) <= std::numeric_limits<float>::max(); }))
    {
        return false;
    }
    output = schema::Vec3(static_cast<float>(values[0]), static_cast<float>(values[1]),
                          static_cast<float>(values[2]));
    return true;
}

} // namespace

bool read_terrain_source(std::string_view text, schema::TerrainGeneratorDataT& output,
                         std::string& error)
{
    output = {};
    if (text.empty() || text.size() > 1024U * 1024U)
    {
        error = "terrain source must contain between 1 byte and 1 MiB of TOML";
        return false;
    }
    auto parsed = toml::parse(text);
    if (!parsed)
    {
        error = "invalid terrain TOML: " + std::string(parsed.error().description());
        return false;
    }
    const auto& table = parsed.table();
    if (!fields(table,
                {"cells_x", "cells_y", "cells_z", "voxel_size", "chunk_cells", "origin", "height",
                 "amplitude", "frequency", "seed", "cavities"},
                error))
    {
        return false;
    }
    schema::TerrainGeneratorDataT value;
    for (const auto& [name, destination] :
         {std::pair{"cells_x", &value.cells_x}, std::pair{"cells_y", &value.cells_y},
          std::pair{"cells_z", &value.cells_z}, std::pair{"chunk_cells", &value.chunk_cells},
          std::pair{"seed", &value.seed}})
    {
        if (table.contains(name))
        {
            const auto number = table[name].value_exact<std::int64_t>();
            if (!number || *number < 0 ||
                static_cast<std::uint64_t>(*number) > std::numeric_limits<std::uint32_t>::max())
            {
                error = std::string("terrain ") + name + " must be a uint32 integer";
                return false;
            }
            *destination = static_cast<std::uint32_t>(*number);
        }
    }
    for (const auto& [name, destination] :
         {std::pair{"voxel_size", &value.voxel_size}, std::pair{"height", &value.height},
          std::pair{"amplitude", &value.amplitude}, std::pair{"frequency", &value.frequency}})
    {
        if (table.contains(name) &&
            !editor::asset_pipeline::float_number(table.get(name), *destination))
        {
            error = std::string("terrain ") + name + " must be a finite float";
            return false;
        }
    }
    if (table.contains("origin") && !vector(table.get("origin"), value.origin))
    {
        error = "terrain origin requires three finite coordinates";
        return false;
    }
    if (table.contains("cavities"))
    {
        const auto* cavities = table["cavities"].as_array();
        if (cavities == nullptr || cavities->size() > 256)
        {
            error = "terrain cavities requires an array of at most 256 sphere tables";
            return false;
        }
        for (const auto& entry : *cavities)
        {
            const auto* cavity = entry.as_table();
            if (cavity == nullptr || !fields(*cavity, {"center", "radius"}, error))
            {
                if (error.empty())
                {
                    error = "terrain cavity must be a center/radius table";
                }
                return false;
            }
            auto sphere = std::make_unique<schema::TerrainSphereT>();
            if (!vector(cavity->get("center"), sphere->center) ||
                !editor::asset_pipeline::float_number(cavity->get("radius"), sphere->radius) ||
                sphere->radius <= 0)
            {
                error = "terrain cavity requires a finite center and positive radius";
                return false;
            }
            value.cavities.push_back(std::move(sphere));
        }
    }
    output = std::move(value);
    return true;
}

} // namespace engine::plugins::terrain
