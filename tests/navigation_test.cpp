#include "runtime/engine_config.hpp"
#include "runtime/navigation/navigation_system.hpp"
#include "runtime/schema/mesh_generated.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace
{

bool expect(bool condition, const char* message, const engine::NavigationSystem& navigation)
{
    if (!condition)
    {
        const auto error = navigation.last_error();
        std::fprintf(stderr, "navigation test: %s (%.*s)\n", message,
                     static_cast<int>(error.size()), error.empty() ? "" : error.data());
    }
    return condition;
}

engine::schema::MeshDataT grid(int width, int depth, int hole_x0 = 0, int hole_x1 = 0,
                               int hole_z0 = 0, int hole_z1 = 0)
{
    engine::schema::MeshDataT mesh;
    for (int z = 0; z <= depth; ++z)
    {
        for (int x = 0; x <= width; ++x)
        {
            mesh.vertices.emplace_back(static_cast<float>(x), 0, static_cast<float>(z), 0, 1, 0);
        }
    }
    for (int z = 0; z < depth; ++z)
    {
        for (int x = 0; x < width; ++x)
        {
            if (x >= hole_x0 && x < hole_x1 && z >= hole_z0 && z < hole_z1)
            {
                continue;
            }
            const auto a = static_cast<std::uint32_t>(x + z * (width + 1));
            const auto b = a + 1;
            const auto c = a + static_cast<std::uint32_t>(width + 1);
            const auto d = c + 1;
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    }
    return mesh;
}

bool local_rebuild()
{
    engine::NavigationSystem navigation;
    auto mesh = grid(24, 16);
    std::array sources{engine::NavigationGeometry{&mesh}};
    const engine::NavigationBounds bounds{{0, -2, 0}, {24, 4, 16}};
    engine::NavigationSettings settings;
    settings.tile_size = 16;
    std::vector<engine::Vec3> path;
    if (!expect(!navigation.build(sources, bounds, settings) &&
                    !navigation.find_path({2, 0, 8}, {22, 0, 8}, path) && path.empty() &&
                    navigation.initialize({}) && navigation.build(sources, bounds, settings) &&
                    navigation.ready() && navigation.rebuilt_tiles() == 24 &&
                    navigation.find_path({2, 0, 8}, {22, 0, 8}, path) && path.size() >= 2 &&
                    std::abs(path.front().x() - 2) < 0.1 && std::abs(path.back().x() - 22) < 0.1,
                "Recast tiles connect and Detour returns a complete path", navigation))
    {
        return false;
    }
    const auto initial_revision = navigation.revision();
    mesh = grid(24, 16, 10, 14, 4, 12);
    const engine::NavigationBounds hole{{10, -1, 4}, {14, 1, 12}};
    if (!expect(navigation.find_path({2, 0, 8}, {22, 0, 8}, path) &&
                    navigation.revision() == initial_revision &&
                    navigation.replace(sources, hole) && navigation.revision() > initial_revision &&
                    navigation.rebuilt_tiles() > 0 && navigation.rebuilt_tiles() < 24 &&
                    navigation.find_path({2, 0, 8}, {22, 0, 8}, path) &&
                    std::any_of(path.begin(), path.end(),
                                [](const auto& point) { return std::abs(point.z() - 8) > 4; }),
                "explicit local replacement reroutes around a hole without rebuilding all tiles",
                navigation))
    {
        return false;
    }
    mesh = grid(24, 16, 10, 14, 0, 16);
    const engine::NavigationBounds cut{{10, -1, 0}, {14, 1, 16}};
    if (!expect(
            navigation.replace(sources, cut) &&
                !navigation.find_path({2, 0, 8}, {22, 0, 8}, path) && path.empty() &&
                navigation.find_path({2, 0, 8}, {6, 0, 8}, path),
            "cutting the full connection rejects Detour partial paths and preserves other tiles",
            navigation))
    {
        return false;
    }
    mesh = grid(24, 16);
    if (!expect(navigation.replace(sources, cut) &&
                    navigation.find_path({2, 0, 8}, {22, 0, 8}, path),
                "filling the cut reconnects existing neighboring tiles", navigation))
    {
        return false;
    }
    const auto before_failure = navigation.revision();
    mesh.indices[0] = std::numeric_limits<std::uint32_t>::max();
    if (!expect(!navigation.replace(sources, cut) && navigation.ready() &&
                    navigation.revision() > before_failure && navigation.rebuilt_tiles() == 0 &&
                    !navigation.last_error().empty() &&
                    !navigation.find_path({2, 0, 8}, {22, 0, 8}, path) && path.empty() &&
                    navigation.find_path({2, 0, 8}, {6, 0, 8}, path),
                "failed recook invalidates the dirty tiles instead of retaining stale routes",
                navigation))
    {
        return false;
    }
    mesh = grid(24, 16);
    if (!expect(navigation.replace(sources, cut) &&
                    navigation.find_path({2, 0, 8}, {22, 0, 8}, path),
                "valid data recovers an invalidated region", navigation))
    {
        return false;
    }
    const auto before_outside = navigation.revision();
    if (!expect(navigation.replace(sources, {{100, -1, 100}, {101, 1, 101}}) &&
                    navigation.rebuilt_tiles() == 0 && navigation.revision() == before_outside &&
                    navigation.replace({}, cut) &&
                    !navigation.find_path({2, 0, 8}, {22, 0, 8}, path) && path.empty() &&
                    navigation.find_path({2, 0, 8}, {6, 0, 8}, path),
                "outside dirty bounds do nothing and empty submitted geometry removes old tiles",
                navigation))
    {
        return false;
    }
    auto invalid = settings;
    invalid.tile_size = 129;
    const auto before_build = navigation.revision();
    if (!expect(!navigation.build(sources, bounds, invalid) && !navigation.ready() &&
                    navigation.revision() > before_build &&
                    !navigation.find_path({2, 0, 8}, {6, 0, 8}, path) && path.empty() &&
                    navigation.build({}, bounds, settings) && navigation.ready() &&
                    !navigation.find_path({2, 0, 8}, {6, 0, 8}, path) && path.empty(),
                "failed full build invalidates old data and an empty grid is a valid empty result",
                navigation))
    {
        return false;
    }
    const auto before_clear = navigation.revision();
    navigation.clear();
    const auto cleared = navigation.revision();
    navigation.clear();
    if (!expect(!navigation.ready() && cleared > before_clear && navigation.revision() == cleared &&
                    navigation.rebuilt_tiles() == 0,
                "clear is idempotent and never reuses a navigation revision", navigation))
    {
        return false;
    }
    navigation.shutdown();
    navigation.shutdown();
    return expect(!navigation.ready() && !navigation.replace(sources, cut),
                  "shutdown is repeatable and rejects later rebuilding", navigation);
}

bool geometry_and_boundaries()
{
    engine::NavigationSystem navigation;
    auto mesh = grid(8, 8);
    std::array sources{engine::NavigationGeometry{&mesh}};
    sources[0].transform[0] = -1;
    sources[0].transform[12] = 18;
    sources[0].transform[14] = 20;
    const engine::NavigationBounds bounds{{10, -1, 20}, {18, 4, 28}};
    const engine::NavigationSettings settings;
    std::vector<engine::Vec3> path;
    if (!expect(navigation.initialize({}) && navigation.build(sources, bounds, settings) &&
                    navigation.find_path({11, 0, 24}, {17, 0, 24}, path),
                "caller transforms and reflected winding preserve walkable geometry", navigation))
    {
        return false;
    }
    mesh.indices.clear();
    mesh.vertices.clear();
    if (!expect(navigation.find_path({11, 0, 24}, {17, 0, 24}, path),
                "completed navigation does not retain borrowed source mesh storage", navigation))
    {
        return false;
    }
    path.push_back({1, 2, 3});
    if (!expect(!navigation.find_path({std::numeric_limits<double>::infinity(), 0, 24}, {17, 0, 24},
                                      path) &&
                    path.empty(),
                "invalid query endpoints clear caller output", navigation))
    {
        return false;
    }
    const auto previous = navigation.revision();
    if (!expect(!navigation.replace(sources, {{11, 1, 22}, {10, 0, 21}}) && !navigation.ready() &&
                    navigation.revision() > previous,
                "unlocatable dirty bounds explicitly invalidate all old navigation", navigation))
    {
        return false;
    }
    mesh = grid(8, 8);
    sources[0].transform[3] = 1;
    if (!expect(!navigation.build(sources, bounds, settings) && !navigation.ready(),
                "perspective transforms are rejected", navigation))
    {
        return false;
    }
    sources[0].transform[3] = 0;
    sources[0].transform[0] = 0;
    if (!expect(!navigation.build(sources, bounds, settings), "singular transforms are rejected",
                navigation))
    {
        return false;
    }
    sources[0].transform[0] = -1;
    auto invalid = settings;
    invalid.cell_size = 0;
    if (!expect(!navigation.build(sources, bounds, invalid), "zero cell size is rejected",
                navigation))
    {
        return false;
    }
    return expect(!navigation.build(sources, {{0, -1, 0}, {100000, 4, 100000}}, settings) &&
                      !navigation.ready(),
                  "oversized tile grids are rejected before allocating voxel data", navigation);
}

} // namespace

int main()
{
    return local_rebuild() && geometry_and_boundaries() ? 0 : 1;
}
