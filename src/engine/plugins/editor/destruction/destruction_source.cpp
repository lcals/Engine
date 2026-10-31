#include "plugins/editor/destruction/destruction_source.hpp"

#include "editor/asset_pipeline/toml_helpers.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace engine::plugins::destruction
{

bool read_destruction_source(std::string_view text, schema::DestructionGeneratorDataT& output,
                             std::string& error)
{
    output = {};
    error.clear();
    if (text.empty() || text.size() > 1024U * 1024U)
    {
        error = "Destruction source must contain between 1 byte and 1 MiB of TOML";
        return false;
    }
    auto parsed = toml::parse(text);
    if (!parsed)
    {
        error = "Invalid destruction TOML: " + std::string(parsed.error().description());
        return false;
    }
    const auto& table = parsed.table();
    constexpr std::array<std::string_view, 9> fields = {"size",     "fracture",    "slices_x",
                                                        "slices_y", "slices_z",    "sites",
                                                        "seed",     "bond_health", "anchor_height"};
    for (const auto& [name, field] : table)
    {
        static_cast<void>(field);
        if (std::find(fields.begin(), fields.end(), name.str()) == fields.end())
        {
            error = "Unknown destruction source field: " + std::string(name.str());
            return false;
        }
    }
    schema::DestructionGeneratorDataT value;
    if (table.contains("size"))
    {
        std::array<double, 3> size{};
        if (!editor::asset_pipeline::numbers(table.get("size"), size) ||
            !std::all_of(size.begin(), size.end(),
                         [](double number) { return number > 0 && number <= 1000; }))
        {
            error = "Destruction size requires three finite positive dimensions up to 1000";
            return false;
        }
        value.size = schema::Vec3(static_cast<float>(size[0]), static_cast<float>(size[1]),
                                  static_cast<float>(size[2]));
    }
    if (table.contains("fracture"))
    {
        const auto fracture = table["fracture"].value<std::string>();
        if (!fracture || (*fracture != "slices" && *fracture != "voronoi"))
        {
            error = "Destruction fracture must be slices or voronoi";
            return false;
        }
        value.fracture = *fracture == "slices" ? schema::DestructionFracture::slices
                                               : schema::DestructionFracture::voronoi;
    }
    for (const auto& [name, destination] :
         {std::pair{"slices_x", &value.slices_x}, std::pair{"slices_y", &value.slices_y},
          std::pair{"slices_z", &value.slices_z}, std::pair{"sites", &value.sites},
          std::pair{"seed", &value.seed}})
    {
        if (table.contains(name))
        {
            const auto number = table[name].value_exact<std::int64_t>();
            if (!number || *number < 0 ||
                static_cast<std::uint64_t>(*number) > std::numeric_limits<std::uint32_t>::max())
            {
                error = std::string("Destruction ") + name + " must be a uint32 integer";
                return false;
            }
            *destination = static_cast<std::uint32_t>(*number);
        }
    }
    for (const auto& [name, destination] : {std::pair{"bond_health", &value.bond_health},
                                            std::pair{"anchor_height", &value.anchor_height}})
    {
        if (table.contains(name) &&
            !editor::asset_pipeline::float_number(table.get(name), *destination))
        {
            error = std::string("Destruction ") + name + " must be a finite float";
            return false;
        }
    }
    if (value.slices_x == 0 || value.slices_y == 0 || value.slices_z == 0 || value.slices_x > 256 ||
        value.slices_y > 256 || value.slices_z > 256 ||
        static_cast<std::uint64_t>(value.slices_x) * value.slices_y * value.slices_z > 256 ||
        value.sites < 2 || value.sites > 256 || value.bond_health <= 0 ||
        value.bond_health > 1000000 || value.anchor_height < 0 ||
        value.anchor_height > value.size.y())
    {
        error = "Destruction requires 1..256 slices per axis, at most 256 pieces, 2..256 sites, "
                "positive bond health and anchor height within the object";
        return false;
    }
    output = std::move(value);
    return true;
}

} // namespace engine::plugins::destruction
