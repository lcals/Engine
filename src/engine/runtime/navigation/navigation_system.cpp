#include "runtime/navigation/navigation_system.hpp"

#include "runtime/schema/mesh_generated.h"

#include <recastnavigation/DetourAlloc.h>
#include <recastnavigation/DetourNavMesh.h>
#include <recastnavigation/DetourNavMeshBuilder.h>
#include <recastnavigation/DetourNavMeshQuery.h>
#include <recastnavigation/Recast.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <utility>

// Tile construction adapts Recast Navigation 1.6.0 RecastDemo/Source/Sample_TileMesh.cpp.
// Changes: caller-owned MeshData inputs, bounded synchronous tile replacement and failure
// invalidation; no demo UI, scene discovery, serialization or custom pathfinding algorithm.
//
// Copyright (c) 2009-2010 Mikko Mononen memon@inside.org
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//

namespace engine
{
namespace
{

constexpr std::size_t maximum_geometry = 4096;
constexpr std::size_t maximum_vertices = 1000000;
constexpr std::size_t maximum_triangles = 1000000;
constexpr int maximum_tiles = 1024;
constexpr int maximum_polygons = 4096;
constexpr int maximum_query_nodes = 8192;
constexpr int maximum_path = 1024;
constexpr double maximum_coordinate = 1000000;

using NavMeshPtr = std::unique_ptr<dtNavMesh, decltype(&dtFreeNavMesh)>;
using NavQueryPtr = std::unique_ptr<dtNavMeshQuery, decltype(&dtFreeNavMeshQuery)>;

struct InputGeometry
{
    std::vector<float> vertices;
    std::vector<int> triangles;
    std::vector<std::array<float, 6>> bounds;
};

struct TileData
{
    int x = 0;
    int z = 0;
    std::unique_ptr<unsigned char, decltype(&dtFree)> data{nullptr, dtFree};
    int size = 0;
};

struct NavigationData
{
    NavMeshPtr mesh{nullptr, dtFreeNavMesh};
    NavQueryPtr query{nullptr, dtFreeNavMeshQuery};
    NavigationBounds bounds;
    NavigationSettings settings;
    int columns = 0;
    int rows = 0;
};

bool bounded(double value, double minimum, double maximum)
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

bool valid_vector(const Vec3& value)
{
    return bounded(value.x(), -maximum_coordinate, maximum_coordinate) &&
           bounded(value.y(), -maximum_coordinate, maximum_coordinate) &&
           bounded(value.z(), -maximum_coordinate, maximum_coordinate);
}

bool valid_bounds(const NavigationBounds& bounds, bool volume)
{
    if (!valid_vector(bounds.minimum) || !valid_vector(bounds.maximum))
    {
        return false;
    }
    return volume ? bounds.minimum.x() < bounds.maximum.x() &&
                        bounds.minimum.y() < bounds.maximum.y() &&
                        bounds.minimum.z() < bounds.maximum.z()
                  : bounds.minimum.x() <= bounds.maximum.x() &&
                        bounds.minimum.y() <= bounds.maximum.y() &&
                        bounds.minimum.z() <= bounds.maximum.z();
}

bool valid_settings(const NavigationSettings& settings, const NavigationBounds& bounds,
                    int& columns, int& rows)
{
    if (!valid_bounds(bounds, true) || !bounded(settings.cell_size, 0.05F, 2) ||
        !bounded(settings.cell_height, 0.025F, 1) || !bounded(settings.agent_height, 0.1F, 10) ||
        !bounded(settings.agent_radius, 0, 4) ||
        !bounded(settings.agent_max_climb, 0, settings.agent_height) ||
        !bounded(settings.agent_max_slope, 0, 85) || settings.tile_size < 8 ||
        settings.tile_size > 128 || !bounded(settings.query_half_extents.x(), 0.01, 10) ||
        !bounded(settings.query_half_extents.y(), 0.01, 100) ||
        !bounded(settings.query_half_extents.z(), 0.01, 10))
    {
        return false;
    }
    const double height = std::ceil(settings.agent_height / settings.cell_height);
    const double radius = std::ceil(settings.agent_radius / settings.cell_size);
    const double vertical = (bounds.maximum.y() - bounds.minimum.y()) / settings.cell_height;
    const double width = static_cast<double>(settings.tile_size) * settings.cell_size;
    const double x = std::ceil((bounds.maximum.x() - bounds.minimum.x()) / width);
    const double z = std::ceil((bounds.maximum.z() - bounds.minimum.z()) / width);
    if (height < 3 || height > 255 || radius > 64 || settings.tile_size + 2 * (radius + 3) > 256 ||
        vertical > RC_SPAN_MAX_HEIGHT || x < 1 || z < 1 || x * z > maximum_tiles)
    {
        return false;
    }
    columns = static_cast<int>(x);
    rows = static_cast<int>(z);
    return true;
}

bool prepare_geometry(std::span<const NavigationGeometry> sources, InputGeometry& output,
                      std::string& error)
{
    output = {};
    if (sources.size() > maximum_geometry)
    {
        error = "Navigation accepts at most 4096 geometry inputs";
        return false;
    }
    for (const auto& source : sources)
    {
        const auto* mesh = source.mesh;
        const auto& m = source.transform;
        if (!mesh || mesh->schema_version != 1 || mesh->indices.size() % 3 != 0 ||
            mesh->vertices.empty() != mesh->indices.empty() ||
            mesh->vertices.size() > maximum_vertices - output.vertices.size() / 3 ||
            mesh->indices.size() / 3 > maximum_triangles - output.triangles.size() / 3 ||
            !std::all_of(m.begin(), m.end(), [](float value) { return std::isfinite(value); }) ||
            std::abs(m[3]) > 0.000001F || std::abs(m[7]) > 0.000001F ||
            std::abs(m[11]) > 0.000001F || std::abs(m[15] - 1) > 0.000001F)
        {
            error = "Invalid navigation mesh, affine transform or 1M geometry limit exceeded";
            return false;
        }
        const double determinant =
            static_cast<double>(m[0]) *
                (static_cast<double>(m[5]) * m[10] - static_cast<double>(m[9]) * m[6]) -
            static_cast<double>(m[4]) *
                (static_cast<double>(m[1]) * m[10] - static_cast<double>(m[9]) * m[2]) +
            static_cast<double>(m[8]) *
                (static_cast<double>(m[1]) * m[6] - static_cast<double>(m[5]) * m[2]);
        if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-12)
        {
            error = "Navigation rejects degenerate transforms";
            return false;
        }
        const auto offset = static_cast<int>(output.vertices.size() / 3);
        for (const auto& vertex : mesh->vertices)
        {
            const Vec3 value{
                static_cast<double>(m[0]) * vertex.x() + static_cast<double>(m[4]) * vertex.y() +
                    static_cast<double>(m[8]) * vertex.z() + m[12],
                static_cast<double>(m[1]) * vertex.x() + static_cast<double>(m[5]) * vertex.y() +
                    static_cast<double>(m[9]) * vertex.z() + m[13],
                static_cast<double>(m[2]) * vertex.x() + static_cast<double>(m[6]) * vertex.y() +
                    static_cast<double>(m[10]) * vertex.z() + m[14]};
            if (!valid_vector(value))
            {
                error = "Navigation world geometry is nonfinite or exceeds coordinate bounds";
                return false;
            }
            output.vertices.insert(output.vertices.end(),
                                   {static_cast<float>(value.x()), static_cast<float>(value.y()),
                                    static_cast<float>(value.z())});
        }
        for (std::size_t index = 0; index < mesh->indices.size(); index += 3)
        {
            std::array<int, 3> triangle{};
            for (std::size_t corner = 0; corner < 3; ++corner)
            {
                const auto vertex = mesh->indices[index + corner];
                if (vertex >= mesh->vertices.size())
                {
                    error = "Navigation triangle references a missing vertex";
                    return false;
                }
                triangle[corner] = offset + static_cast<int>(vertex);
            }
            if (determinant < 0)
            {
                std::swap(triangle[1], triangle[2]);
            }
            std::array<float, 6> bounds{};
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                const float a = output.vertices[static_cast<std::size_t>(triangle[0]) * 3 + axis];
                const float b = output.vertices[static_cast<std::size_t>(triangle[1]) * 3 + axis];
                const float c = output.vertices[static_cast<std::size_t>(triangle[2]) * 3 + axis];
                bounds[axis] = std::min({a, b, c});
                bounds[axis + 3] = std::max({a, b, c});
            }
            output.triangles.insert(output.triangles.end(), triangle.begin(), triangle.end());
            output.bounds.push_back(bounds);
        }
    }
    return true;
}

rcConfig tile_config(const NavigationData& data, int x, int z)
{
    const auto& settings = data.settings;
    rcConfig config{};
    config.cs = settings.cell_size;
    config.ch = settings.cell_height;
    config.walkableSlopeAngle = settings.agent_max_slope;
    config.walkableHeight = static_cast<int>(std::ceil(settings.agent_height / config.ch));
    config.walkableClimb = static_cast<int>(std::floor(settings.agent_max_climb / config.ch));
    config.walkableRadius = static_cast<int>(std::ceil(settings.agent_radius / config.cs));
    config.tileSize = static_cast<int>(settings.tile_size);
    config.borderSize = config.walkableRadius + 3;
    config.width = config.height = config.tileSize + 2 * config.borderSize;
    config.maxEdgeLen = config.tileSize;
    config.maxSimplificationError = 1.3F;
    config.minRegionArea = 8 * 8;
    config.mergeRegionArea = 20 * 20;
    config.maxVertsPerPoly = DT_VERTS_PER_POLYGON;
    config.detailSampleDist = config.cs * 6;
    config.detailSampleMaxError = config.ch;
    const float width = config.tileSize * config.cs;
    const float border = config.borderSize * config.cs;
    config.bmin[0] = static_cast<float>(data.bounds.minimum.x()) + x * width - border;
    config.bmin[1] = static_cast<float>(data.bounds.minimum.y());
    config.bmin[2] = static_cast<float>(data.bounds.minimum.z()) + z * width - border;
    config.bmax[0] = static_cast<float>(data.bounds.minimum.x()) + (x + 1) * width + border;
    config.bmax[1] = static_cast<float>(data.bounds.maximum.y());
    config.bmax[2] = static_cast<float>(data.bounds.minimum.z()) + (z + 1) * width + border;
    return config;
}

bool build_tile(const NavigationData& data, const InputGeometry& input, TileData& tile,
                std::string& error)
{
    const auto config = tile_config(data, tile.x, tile.z);
    std::vector<int> triangles;
    for (std::size_t index = 0; index < input.bounds.size(); ++index)
    {
        const auto& bounds = input.bounds[index];
        if (bounds[0] > config.bmax[0] || bounds[3] < config.bmin[0] ||
            bounds[1] > config.bmax[1] || bounds[4] < config.bmin[1] ||
            bounds[2] > config.bmax[2] || bounds[5] < config.bmin[2])
        {
            continue;
        }
        triangles.insert(triangles.end(), input.triangles.begin() + index * 3,
                         input.triangles.begin() + index * 3 + 3);
    }
    if (triangles.empty())
    {
        return true;
    }
    rcContext context(false);
    std::unique_ptr<rcHeightfield, decltype(&rcFreeHeightField)> solid(rcAllocHeightfield(),
                                                                       rcFreeHeightField);
    std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)> compact(
        rcAllocCompactHeightfield(), rcFreeCompactHeightfield);
    std::unique_ptr<rcContourSet, decltype(&rcFreeContourSet)> contours(rcAllocContourSet(),
                                                                        rcFreeContourSet);
    std::unique_ptr<rcPolyMesh, decltype(&rcFreePolyMesh)> polygons(rcAllocPolyMesh(),
                                                                    rcFreePolyMesh);
    std::unique_ptr<rcPolyMeshDetail, decltype(&rcFreePolyMeshDetail)> detail(
        rcAllocPolyMeshDetail(), rcFreePolyMeshDetail);
    if (!solid || !compact || !contours || !polygons || !detail ||
        !rcCreateHeightfield(&context, *solid, config.width, config.height, config.bmin,
                             config.bmax, config.cs, config.ch))
    {
        error = "Cannot allocate Recast tile working data";
        return false;
    }
    const int vertex_count = static_cast<int>(input.vertices.size() / 3);
    const int triangle_count = static_cast<int>(triangles.size() / 3);
    std::vector<unsigned char> areas(static_cast<std::size_t>(triangle_count), RC_NULL_AREA);
    rcMarkWalkableTriangles(&context, config.walkableSlopeAngle, input.vertices.data(),
                            vertex_count, triangles.data(), triangle_count, areas.data());
    if (!rcRasterizeTriangles(&context, input.vertices.data(), vertex_count, triangles.data(),
                              areas.data(), triangle_count, *solid, config.walkableClimb))
    {
        error = "Recast tile rasterization failed";
        return false;
    }
    rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *solid);
    rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *solid);
    rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *solid);
    if (!rcBuildCompactHeightfield(&context, config.walkableHeight, config.walkableClimb, *solid,
                                   *compact))
    {
        error = "Recast compact heightfield failed";
        return false;
    }
    solid.reset();
    if (compact->spanCount == 0)
    {
        return true;
    }
    // Clamp the navigable surface to the caller's bounds; border geometry still participates
    // in rasterization/clearance, but never becomes an escape route outside the declared grid.
    for (int z = 0; z < compact->height; ++z)
    {
        for (int x = 0; x < compact->width; ++x)
        {
            const double px = compact->bmin[0] + (x + 0.5) * compact->cs;
            const double pz = compact->bmin[2] + (z + 0.5) * compact->cs;
            if (px >= data.bounds.minimum.x() && px <= data.bounds.maximum.x() &&
                pz >= data.bounds.minimum.z() && pz <= data.bounds.maximum.z())
            {
                continue;
            }
            const auto& cell = compact->cells[x + z * compact->width];
            for (unsigned int index = cell.index; index < cell.index + cell.count; ++index)
            {
                compact->areas[index] = RC_NULL_AREA;
            }
        }
    }
    if (!rcErodeWalkableArea(&context, config.walkableRadius, *compact) ||
        !rcBuildDistanceField(&context, *compact) ||
        !rcBuildRegions(&context, *compact, config.borderSize, config.minRegionArea,
                        config.mergeRegionArea) ||
        !rcBuildContours(&context, *compact, config.maxSimplificationError, config.maxEdgeLen,
                         *contours))
    {
        error = "Recast erosion, region or contour construction failed";
        return false;
    }
    if (contours->nconts == 0)
    {
        return true;
    }
    if (!rcBuildPolyMesh(&context, *contours, config.maxVertsPerPoly, *polygons) ||
        polygons->nverts >= 0xffff || polygons->npolys > maximum_polygons)
    {
        error = "Recast polygon construction failed or exceeded the tile polygon limit";
        return false;
    }
    if (polygons->npolys == 0)
    {
        return true;
    }
    if (!rcBuildPolyMeshDetail(&context, *polygons, *compact, config.detailSampleDist,
                               config.detailSampleMaxError, *detail))
    {
        error = "Recast detail mesh construction failed";
        return false;
    }
    for (int index = 0; index < polygons->npolys; ++index)
    {
        polygons->areas[index] = 0;
        polygons->flags[index] = 1;
    }
    dtNavMeshCreateParams params{};
    params.verts = polygons->verts;
    params.vertCount = polygons->nverts;
    params.polys = polygons->polys;
    params.polyAreas = polygons->areas;
    params.polyFlags = polygons->flags;
    params.polyCount = polygons->npolys;
    params.nvp = polygons->nvp;
    params.detailMeshes = detail->meshes;
    params.detailVerts = detail->verts;
    params.detailVertsCount = detail->nverts;
    params.detailTris = detail->tris;
    params.detailTriCount = detail->ntris;
    params.walkableHeight = data.settings.agent_height;
    params.walkableRadius = data.settings.agent_radius;
    params.walkableClimb = data.settings.agent_max_climb;
    params.tileX = tile.x;
    params.tileY = tile.z;
    rcVcopy(params.bmin, polygons->bmin);
    rcVcopy(params.bmax, polygons->bmax);
    params.cs = config.cs;
    params.ch = config.ch;
    params.buildBvTree = true;
    unsigned char* bytes = nullptr;
    if (!dtCreateNavMeshData(&params, &bytes, &tile.size))
    {
        dtFree(bytes);
        error = "Detour tile encoding failed";
        return false;
    }
    tile.data.reset(bytes);
    return true;
}

bool initialize_grid(NavigationData& data)
{
    data.mesh.reset(dtAllocNavMesh());
    data.query.reset(dtAllocNavMeshQuery());
    if (!data.mesh || !data.query)
    {
        return false;
    }
    dtNavMeshParams params{};
    params.orig[0] = static_cast<float>(data.bounds.minimum.x());
    params.orig[1] = static_cast<float>(data.bounds.minimum.y());
    params.orig[2] = static_cast<float>(data.bounds.minimum.z());
    params.tileWidth = params.tileHeight = data.settings.tile_size * data.settings.cell_size;
    params.maxTiles = 1;
    while (params.maxTiles < data.columns * data.rows)
    {
        params.maxTiles *= 2;
    }
    params.maxPolys = maximum_polygons;
    return dtStatusSucceed(data.mesh->init(&params)) &&
           dtStatusSucceed(data.query->init(data.mesh.get(), maximum_query_nodes));
}

bool add_tile(NavigationData& data, TileData& tile)
{
    if (!tile.data)
    {
        return true;
    }
    if (dtStatusFailed(
            data.mesh->addTile(tile.data.get(), tile.size, DT_TILE_FREE_DATA, 0, nullptr)))
    {
        return false;
    }
    (void)tile.data.release();
    return true;
}

bool complete(dtStatus status)
{
    return dtStatusSucceed(status) &&
           (status & (DT_PARTIAL_RESULT | DT_BUFFER_TOO_SMALL | DT_OUT_OF_NODES)) == 0;
}

} // namespace

struct NavigationSystem::State
{
    std::unique_ptr<NavigationData> data;
    std::uint64_t revision = 0;
    std::size_t rebuilt = 0;
    std::string error;

    void changed() noexcept
    {
        if (revision < std::numeric_limits<std::uint64_t>::max())
        {
            ++revision;
        }
    }

    void invalidate(const std::vector<TileData>& tiles)
    {
        rebuilt = 0;
        if (!data)
        {
            return;
        }
        for (const auto& tile : tiles)
        {
            const auto reference = data->mesh->getTileRefAt(tile.x, tile.z, 0);
            if (reference && dtStatusFailed(data->mesh->removeTile(reference, nullptr, nullptr)))
            {
                data.reset();
                break;
            }
        }
        changed();
    }
};

NavigationSystem::NavigationSystem() noexcept = default;

NavigationSystem::~NavigationSystem() noexcept
{
    shutdown();
}

bool NavigationSystem::on_initialize(const EngineConfig&)
{
    state_ = std::make_unique<State>();
    return true;
}

void NavigationSystem::clear() noexcept
{
    if (state_)
    {
        if (state_->data)
        {
            state_->data.reset();
            state_->changed();
        }
        state_->rebuilt = 0;
        state_->error.clear();
    }
}

bool NavigationSystem::ready() const noexcept
{
    return state_ && state_->data;
}

std::uint64_t NavigationSystem::revision() const noexcept
{
    return state_ ? state_->revision : 0;
}

std::size_t NavigationSystem::rebuilt_tiles() const noexcept
{
    return state_ ? state_->rebuilt : 0;
}

std::string_view NavigationSystem::last_error() const noexcept
{
    return state_ ? std::string_view(state_->error) : std::string_view{};
}

bool NavigationSystem::build(std::span<const NavigationGeometry> geometry,
                             const NavigationBounds& bounds, const NavigationSettings& settings)
{
    if (!state_ || !is_initialized())
    {
        return false;
    }
    state_->rebuilt = 0;
    auto candidate = std::make_unique<NavigationData>();
    candidate->bounds = bounds;
    candidate->settings = settings;
    const auto fail = [&](std::string error)
    {
        clear();
        state_->error = std::move(error);
        return false;
    };
    if (state_->revision == std::numeric_limits<std::uint64_t>::max() ||
        !valid_settings(settings, bounds, candidate->columns, candidate->rows))
    {
        return fail("Invalid navigation configuration, bounds or tile grid limit");
    }
    InputGeometry input;
    if (!prepare_geometry(geometry, input, state_->error))
    {
        return fail(state_->error);
    }
    if (!initialize_grid(*candidate))
    {
        return fail("Cannot initialize Detour navigation grid");
    }
    for (int z = 0; z < candidate->rows; ++z)
    {
        for (int x = 0; x < candidate->columns; ++x)
        {
            TileData tile;
            tile.x = x;
            tile.z = z;
            if (!build_tile(*candidate, input, tile, state_->error))
            {
                return fail(state_->error);
            }
            if (!add_tile(*candidate, tile))
            {
                return fail("Cannot publish Detour navigation tile");
            }
        }
    }
    state_->rebuilt = static_cast<std::size_t>(candidate->columns * candidate->rows);
    state_->data = std::move(candidate);
    state_->changed();
    state_->error.clear();
    return true;
}

bool NavigationSystem::replace(std::span<const NavigationGeometry> geometry,
                               const NavigationBounds& dirty)
{
    if (!ready())
    {
        if (state_)
        {
            state_->error = "Navigation requires a complete build before local replacement";
            state_->rebuilt = 0;
        }
        return false;
    }
    state_->rebuilt = 0;
    if (!valid_bounds(dirty, false) ||
        state_->revision == std::numeric_limits<std::uint64_t>::max())
    {
        clear();
        state_->error = "Invalid navigation dirty bounds; all previous navigation was invalidated";
        return false;
    }
    auto& data = *state_->data;
    std::vector<TileData> tiles;
    for (int z = 0; z < data.rows; ++z)
    {
        for (int x = 0; x < data.columns; ++x)
        {
            const auto config = tile_config(data, x, z);
            if (dirty.maximum.x() < config.bmin[0] || dirty.minimum.x() > config.bmax[0] ||
                dirty.maximum.z() < config.bmin[2] || dirty.minimum.z() > config.bmax[2] ||
                dirty.maximum.y() < config.bmin[1] || dirty.minimum.y() > config.bmax[1])
            {
                continue;
            }
            tiles.emplace_back();
            tiles.back().x = x;
            tiles.back().z = z;
        }
    }
    if (tiles.empty())
    {
        state_->error.clear();
        return true;
    }
    const auto fail = [&](std::string error)
    {
        state_->invalidate(tiles);
        state_->error = std::move(error) + "; affected navigation tiles were invalidated";
        return false;
    };
    InputGeometry input;
    if (!prepare_geometry(geometry, input, state_->error))
    {
        return fail(state_->error);
    }
    for (auto& tile : tiles)
    {
        if (!build_tile(data, input, tile, state_->error))
        {
            return fail(state_->error);
        }
    }
    // All expensive preparation completed. Publish only at this caller-owned safe point.
    for (auto& tile : tiles)
    {
        const auto reference = data.mesh->getTileRefAt(tile.x, tile.z, 0);
        if ((reference && dtStatusFailed(data.mesh->removeTile(reference, nullptr, nullptr))) ||
            !add_tile(data, tile))
        {
            return fail("Cannot publish rebuilt Detour navigation tiles");
        }
    }
    state_->rebuilt = tiles.size();
    state_->changed();
    state_->error.clear();
    return true;
}

bool NavigationSystem::find_path(const Vec3& start, const Vec3& end,
                                 std::vector<Vec3>& output) const
{
    output.clear();
    if (!ready())
    {
        if (state_)
        {
            state_->error = "Navigation is not built";
        }
        return false;
    }
    auto& data = *state_->data;
    const auto inside = [&](const Vec3& value)
    {
        return valid_vector(value) && value.x() >= data.bounds.minimum.x() &&
               value.x() <= data.bounds.maximum.x() && value.y() >= data.bounds.minimum.y() &&
               value.y() <= data.bounds.maximum.y() && value.z() >= data.bounds.minimum.z() &&
               value.z() <= data.bounds.maximum.z();
    };
    if (!inside(start) || !inside(end))
    {
        state_->error = "Navigation path endpoints must be finite and inside the build bounds";
        return false;
    }
    const float begin[3]{static_cast<float>(start.x()), static_cast<float>(start.y()),
                         static_cast<float>(start.z())};
    const float target[3]{static_cast<float>(end.x()), static_cast<float>(end.y()),
                          static_cast<float>(end.z())};
    const auto& range = data.settings.query_half_extents;
    const float extent[3]{static_cast<float>(range.x()), static_cast<float>(range.y()),
                          static_cast<float>(range.z())};
    dtQueryFilter filter;
    filter.setIncludeFlags(1);
    dtPolyRef begin_ref = 0;
    dtPolyRef end_ref = 0;
    float projected_begin[3]{};
    float projected_end[3]{};
    if (!complete(
            data.query->findNearestPoly(begin, extent, &filter, &begin_ref, projected_begin)) ||
        !complete(data.query->findNearestPoly(target, extent, &filter, &end_ref, projected_end)) ||
        !begin_ref || !end_ref)
    {
        state_->error = "No navigable polygon near a path endpoint";
        return false;
    }
    std::array<dtPolyRef, maximum_path> corridor{};
    int count = 0;
    const auto status = data.query->findPath(begin_ref, end_ref, projected_begin, projected_end,
                                             &filter, corridor.data(), &count, maximum_path);
    if (!complete(status) || count == 0 || corridor[static_cast<std::size_t>(count - 1)] != end_ref)
    {
        state_->error = "Navigation destination is unreachable or exceeds the query budget";
        return false;
    }
    std::array<float, maximum_path * 3> points{};
    int point_count = 0;
    if (!complete(data.query->findStraightPath(projected_begin, projected_end, corridor.data(),
                                               count, points.data(), nullptr, nullptr, &point_count,
                                               maximum_path)) ||
        point_count == 0)
    {
        state_->error = "Navigation path exceeds the output point budget";
        return false;
    }
    output.reserve(static_cast<std::size_t>(point_count));
    for (int index = 0; index < point_count; ++index)
    {
        output.emplace_back(points[static_cast<std::size_t>(index) * 3],
                            points[static_cast<std::size_t>(index) * 3 + 1],
                            points[static_cast<std::size_t>(index) * 3 + 2]);
    }
    state_->error.clear();
    return true;
}

void NavigationSystem::on_shutdown() noexcept
{
    state_.reset();
}

} // namespace engine
