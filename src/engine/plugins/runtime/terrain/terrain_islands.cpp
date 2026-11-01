#include "plugins/runtime/terrain/terrain_volume.hpp"

#include "runtime/performance/profiling.hpp"

// Algorithm references: Godot Voxel Tools, MIT, commit
// 045ff9326a32fd10ac6fdac6357b5a098aaf2093, util/island_finder.h and
// edition/floating_chunks.cpp. Adapt the six-neighbor/boundary exclusion workflow;
// use our existing schema, Transvoxel mesher and Jolt convex compound descriptors.

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <utility>

namespace engine::plugins::terrain
{
namespace
{
using Data = schema::TerrainAssetDataT;
using Coordinate = std::array<unsigned, 3>;

std::size_t offset(const Data& data, unsigned x, unsigned y, unsigned z)
{
    return (static_cast<std::size_t>(z) * (data.cells_y + 1) + y) * (data.cells_x + 1) + x;
}

Coordinate coordinate(const Data& data, std::size_t index)
{
    const auto width = data.cells_x + 1;
    const auto height = data.cells_y + 1;
    return {static_cast<unsigned>(index % width),
            static_cast<unsigned>((index / width) % height),
            static_cast<unsigned>(index / (width * height))};
}

schema::Vec3 position(const Data& data, unsigned x, unsigned y, unsigned z)
{
    return {data.origin.x() + x * data.voxel_size, data.origin.y() + y * data.voxel_size,
            data.origin.z() + z * data.voxel_size};
}

bool build_collision(const Data& data, const Coordinate& cells, schema::ColliderDataT& collision,
                     std::string& error)
{
    ENGINE_PROFILE_ZONE("Terrain.fragment_collision");
    collision.shape = schema::ColliderShape::compound;
    // A convex hull for each occupied cell preserves concavity between cells. Interior cells
    // contain their eight solid corners; surface cells also contain exact SDF edge intersections.
    // Jolt constructs and validates every hull; no separate convex-decomposition implementation.
    // Only cells touching the isolated negative samples can contain geometry. The rest of the
    // padded meshing volume is air; keep both boundary cells around the sample bounds.
    for (unsigned z = 0; z < cells[2]; ++z)
    {
        for (unsigned y = 0; y < cells[1]; ++y)
        {
            for (unsigned x = 0; x < cells[0]; ++x)
            {
                std::array<float, 8> values{};
                std::array<schema::Vec3, 8> corners{};
                bool solid = false;
                for (unsigned corner = 0; corner < 8; ++corner)
                {
                    const auto px = x + (corner & 1U);
                    const auto py = y + ((corner >> 1U) & 1U);
                    const auto pz = z + ((corner >> 2U) & 1U);
                    values[corner] = data.density[offset(data, px, py, pz)];
                    solid = solid || values[corner] < 0;
                }
                if (!solid)
                {
                    continue;
                }
                // Eight corners plus at most twelve edge crossings; allocate the final vertex
                // array once after collecting the same ordered points as the full-cell scan.
                std::array<schema::Vec3, 20> points{};
                std::size_t point_count = 0;
                for (unsigned corner = 0; corner < 8; ++corner)
                {
                    corners[corner] = position(data, x + (corner & 1U),
                                               y + ((corner >> 1U) & 1U),
                                               z + ((corner >> 2U) & 1U));
                    if (values[corner] < 0)
                    {
                        points[point_count++] = corners[corner];
                    }
                }
                for (unsigned corner = 0; corner < 8; ++corner)
                {
                    for (unsigned axis = 1; axis <= 4; axis *= 2)
                    {
                        if ((corner & axis) != 0)
                        {
                            continue;
                        }
                        const auto other = corner | axis;
                        if ((values[corner] < 0) == (values[other] < 0))
                        {
                            continue;
                        }
                        const double t = values[corner] /
                                         static_cast<double>(values[corner] - values[other]);
                        const auto& a = corners[corner];
                        const auto& b = corners[other];
                        points[point_count++] = {a.x() + t * (b.x() - a.x()),
                                                a.y() + t * (b.y() - a.y()),
                                                a.z() + t * (b.z() - a.z())};
                    }
                }
                if (collision.hulls.size() >= 4096)
                {
                    error = "Terrain fragment exceeds the compound hull budget";
                    return false;
                }
                auto hull = std::make_unique<schema::ColliderHullDataT>();
                hull->vertices.assign(points.data(), points.data() + point_count);
                collision.hulls.push_back(std::move(hull));
            }
        }
    }
    return !collision.hulls.empty();
}

bool build_fragment(const Data& source, const std::vector<std::size_t>& samples,
                    TerrainFragment& output, std::string& error)
{
    ENGINE_PROFILE_ZONE("Terrain.fragment_mesh");
    Coordinate low{source.cells_x, source.cells_y, source.cells_z};
    Coordinate high{};
    for (auto index : samples)
    {
        const auto point = coordinate(source, index);
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            low[axis] = std::min(low[axis], point[axis]);
            high[axis] = std::max(high[axis], point[axis]);
        }
    }
    // Eligible samples never touch the anchoring shell, so a one-sample air margin exists.
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        --low[axis];
    }
    Data isolated;
    isolated.voxel_size = source.voxel_size;
    isolated.chunk_cells = 16;
    isolated.cells_x = ((high[0] - low[0] + 1 + 15) / 16) * 16;
    isolated.cells_y = ((high[1] - low[1] + 1 + 15) / 16) * 16;
    isolated.cells_z = ((high[2] - low[2] + 1 + 15) / 16) * 16;
    isolated.origin = position(source, low[0], low[1], low[2]);
    isolated.density.resize(static_cast<std::size_t>(isolated.cells_x + 1) *
                            (isolated.cells_y + 1) * (isolated.cells_z + 1), source.voxel_size);
    for (unsigned z = 0; z <= isolated.cells_z; ++z)
    {
        for (unsigned y = 0; y <= isolated.cells_y; ++y)
        {
            for (unsigned x = 0; x <= isolated.cells_x; ++x)
            {
                if (x + low[0] <= source.cells_x && y + low[1] <= source.cells_y &&
                    z + low[2] <= source.cells_z)
                {
                    isolated.density[offset(isolated, x, y, z)] =
                        std::max(0.00001F, std::abs(source.density[offset(
                                               source, x + low[0], y + low[1], z + low[2])]));
                }
            }
        }
    }
    for (auto index : samples)
    {
        const auto point = coordinate(source, index);
        isolated.density[offset(isolated, point[0] - low[0], point[1] - low[1],
                                 point[2] - low[2])] = source.density[index];
    }
    TerrainVolume mesh_volume;
    const Coordinate collision_cells{high[0] - low[0] + 1, high[1] - low[1] + 1,
                                      high[2] - low[2] + 1};
    if (!mesh_volume.initialize(isolated, error) || !mesh_volume.build_mesh(output.mesh, error) ||
        output.mesh.indices.empty() ||
        !build_collision(isolated, collision_cells, output.collision, error))
    {
        if (error.empty())
        {
            error = "Cannot generate a closed terrain fragment";
        }
        return false;
    }
    const double size = source.voxel_size;
    output.volume = samples.size() * size * size * size;
    return true;
}
} // namespace

bool TerrainVolume::extract_detached(std::uint32_t min_samples, std::uint32_t max_fragments,
                                     std::uint32_t fragment_cells,
                                     std::vector<TerrainFragment>& output, std::string& error)
{
    ENGINE_PROFILE_ZONE("Terrain.extract_detached");
    output.clear();
    error.clear();
    if (chunks_.empty() || min_samples == 0 || min_samples > data_.density.size() ||
        max_fragments == 0 || max_fragments > 128 || fragment_cells == 0 || fragment_cells > 8)
    {
        error = "Invalid terrain collapse sample, fragment or cell budget";
        return false;
    }
    std::vector<std::uint8_t> visited(data_.density.size());
    std::vector<std::vector<std::size_t>> fragments;
    std::vector<std::size_t> island;
    {
        ENGINE_PROFILE_ZONE("Terrain.find_islands");
        for (std::size_t seed = 0; seed < data_.density.size(); ++seed)
        {
            if (visited[seed] || data_.density[seed] >= 0)
            {
                continue;
            }
            island.clear();
            island.push_back(seed);
            visited[seed] = 1;
            bool anchored = false;
            for (std::size_t cursor = 0; cursor < island.size(); ++cursor)
            {
                const auto point = coordinate(data_, island[cursor]);
                anchored = anchored || point[0] <= 1 || point[0] >= data_.cells_x - 1 ||
                           point[1] <= 1 || point[2] <= 1 || point[2] >= data_.cells_z - 1;
                // Six face neighbors, matching Voxel Tools' connected-component policy.
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    const unsigned dimension[] = {data_.cells_x, data_.cells_y, data_.cells_z};
                    for (int step : {-1, 1})
                    {
                        if ((step < 0 && point[axis] == 0) ||
                            (step > 0 && point[axis] == dimension[axis]))
                        {
                            continue;
                        }
                        auto neighbor = point;
                        neighbor[axis] =
                            static_cast<unsigned>(static_cast<int>(point[axis]) + step);
                        const auto index = offset(data_, neighbor[0], neighbor[1], neighbor[2]);
                        if (!visited[index] && data_.density[index] < 0)
                        {
                            visited[index] = 1;
                            island.push_back(index);
                        }
                    }
                }
            }
            if (anchored || island.size() < min_samples)
            {
                continue;
            }
            std::map<Coordinate, std::vector<std::size_t>> groups;
            for (auto index : island)
            {
                auto cell = coordinate(data_, index);
                for (auto& value : cell)
                {
                    value /= fragment_cells;
                }
                groups[cell].push_back(index);
                if (fragments.size() + groups.size() > max_fragments)
                {
                    error = "Detached terrain exceeds the requested fragment budget";
                    return false;
                }
            }
            for (auto& [cell, samples] : groups)
            {
                fragments.push_back(std::move(samples));
            }
        }
    }
    std::vector<TerrainFragment> prepared;
    prepared.reserve(fragments.size());
    for (const auto& samples : fragments)
    {
        TerrainFragment fragment;
        if (!build_fragment(data_, samples, fragment, error))
        {
            return false;
        }
        prepared.push_back(std::move(fragment));
    }
    for (const auto& samples : fragments)
    {
        for (auto index : samples)
        {
            data_.density[index] = std::max(data_.voxel_size, -data_.density[index]);
            const auto point = coordinate(data_, index);
            mark_sample_dirty(point[0], point[1], point[2]);
        }
    }
    ENGINE_PROFILE_PLOT("Terrain.extracted_fragments", static_cast<double>(prepared.size()));
    output = std::move(prepared);
    return true;
}

} // namespace engine::plugins::terrain
