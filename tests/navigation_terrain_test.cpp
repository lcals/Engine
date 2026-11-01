#include "plugins/runtime/terrain/terrain_volume.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/navigation/navigation_system.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace
{

bool expect(bool condition, const char* message, const engine::NavigationSystem& navigation,
            std::string_view terrain_error)
{
    if (!condition)
    {
        const auto error = navigation.last_error();
        std::fprintf(stderr, "navigation terrain: %s (navigation: %.*s; terrain: %.*s)\n", message,
                     static_cast<int>(error.size()), error.empty() ? "" : error.data(),
                     static_cast<int>(terrain_error.size()),
                     terrain_error.empty() ? "" : terrain_error.data());
    }
    return condition;
}

double distance(const engine::Vec3& first, const engine::Vec3& second)
{
    return std::hypot(first.x() - second.x(), first.y() - second.y(), first.z() - second.z());
}

bool terrain_destruction_and_recovery()
{
    using engine::NavigationAgentStatus;
    using engine::plugins::terrain::TerrainVolume;

    engine::schema::TerrainGeneratorDataT generator;
    generator.cells_x = 48;
    generator.cells_y = generator.cells_z = 16;
    generator.voxel_size = 0.5F;
    generator.chunk_cells = 16;
    generator.origin = {-12, -4, -4};
    generator.height = generator.amplitude = 0;

    TerrainVolume terrain;
    engine::schema::TerrainAssetDataT baseline;
    engine::schema::MeshDataT mesh;
    std::string terrain_error;
    engine::NavigationSystem navigation;
    const std::array geometry{engine::NavigationGeometry{&mesh}};
    const engine::NavigationBounds bounds{{-12, -4, -4}, {12, 4, 4}};
    engine::NavigationSettings settings;
    settings.tile_size = 16;
    if (!expect(TerrainVolume::generate(generator, baseline, terrain_error) &&
                    terrain.initialize(baseline, terrain_error) &&
                    terrain.build_mesh(mesh, terrain_error) && !mesh.indices.empty() &&
                    navigation.initialize({}) && navigation.build(geometry, bounds, settings) &&
                    navigation.configure_crowd(5),
                "generated sampled terrain feeds its actual mesh into navigation", navigation,
                terrain_error))
    {
        return false;
    }
    const auto full_tiles = navigation.rebuilt_tiles();
    const std::array lanes{-2.4, -0.8, 0.8, 2.4};
    std::array<engine::NavigationAgentHandle, 4> handles{};
    std::array<engine::NavigationAgentState, 4> states{};
    engine::NavigationAgentHandle center_handle;
    engine::NavigationAgentState center_state;
    const engine::Vec3 center_target{0, 0, 0};
    std::vector<engine::Vec3> path;
    engine::NavigationAgentSettings agent_settings;
    // Neighbor separation can settle slightly off four closely spaced destinations.
    agent_settings.arrival_distance = 0.4F;
    for (std::size_t index = 0; index < handles.size(); ++index)
    {
        const engine::Vec3 start{-9, 0, lanes[index]};
        const engine::Vec3 target{9, 0, lanes[index]};
        if (!expect(navigation.find_path(start, target, path) &&
                        navigation.create_agent(start, agent_settings, handles[index]) &&
                        navigation.set_agent_target(handles[index], target),
                    "four agents accept distinct goals across the original terrain", navigation,
                    terrain_error))
        {
            return false;
        }
    }
    if (!expect(navigation.create_agent(center_target, {}, center_handle) &&
                    navigation.set_agent_target(center_handle, center_target),
                "a fifth agent holds its own distinct goal at the future trench center", navigation,
                terrain_error))
    {
        return false;
    }
    for (int step = 0; step < 30; ++step)
    {
        if (!expect(navigation.step_crowd(1.0 / 60.0), "advance before changing terrain",
                    navigation, terrain_error))
        {
            return false;
        }
    }
    for (std::size_t index = 0; index < handles.size(); ++index)
    {
        if (!expect(navigation.agent_state(handles[index], states[index]) &&
                        states[index].status == NavigationAgentStatus::moving &&
                        states[index].position.x() > -9 && states[index].position.x() < -6,
                    "all agents begin moving while still clear of the future trench", navigation,
                    terrain_error))
        {
            return false;
        }
    }
    if (!expect(navigation.agent_state(center_handle, center_state) &&
                    center_state.status == NavigationAgentStatus::arrived &&
                    center_state.has_target &&
                    std::hypot(center_state.position.x(), center_state.position.z()) < 0.001 &&
                    distance(center_state.velocity, {}) < 0.00001,
                "the center agent reaches its current-position goal before any terrain removal",
                navigation, terrain_error))
    {
        return false;
    }
    const auto center_position = center_state.position;

    // The overlapping spheres remove the complete solid thickness across the strip.
    // Dirty also includes the brush's neighboring samples and the full terrain height.
    const engine::NavigationBounds dirty{{-4.5, -4, -4}, {4.5, 4, 4}};
    for (const double z : std::array{-4.0, 0.0, 4.0})
    {
        if (!expect(terrain.apply_sphere({0, -1.5, z}, 3.5F, false, terrain_error),
                    "real density brushes carve a continuous trench", navigation, terrain_error))
        {
            return false;
        }
    }
    const auto before_cut = navigation.revision();
    if (!expect(terrain.build_mesh(mesh, terrain_error) && navigation.replace(geometry, dirty) &&
                    navigation.revision() > before_cut && navigation.rebuilt_tiles() > 0 &&
                    navigation.rebuilt_tiles() < full_tiles &&
                    !navigation.find_path({-9, 0, 0}, {9, 0, 0}, path) && path.empty() &&
                    navigation.find_path({-9, 0, 0}, {-6, 0, 0}, path),
                "local replacement cuts the route while preserving unaffected terrain tiles",
                navigation, terrain_error))
    {
        return false;
    }
    if (!expect(!navigation.find_path(center_position, center_position, path) && path.empty() &&
                    navigation.agent_state(center_handle, center_state) &&
                    center_state.status == NavigationAgentStatus::invalid &&
                    center_state.has_target && distance(center_state.target, center_target) == 0 &&
                    distance(center_state.position, center_position) < 0.00001 &&
                    distance(center_state.velocity, {}) < 0.00001,
                "the missing center surface leaves the old handle invalid, stationary and "
                "unprojectable",
                navigation, terrain_error))
    {
        return false;
    }
    for (std::size_t index = 0; index < handles.size(); ++index)
    {
        if (!expect(navigation.agent_state(handles[index], states[index]) &&
                        states[index].status == NavigationAgentStatus::pending &&
                        states[index].has_target && states[index].target.x() == 9 &&
                        states[index].target.z() == lanes[index],
                    "local terrain changes keep agent generations and requeue their old goals",
                    navigation, terrain_error))
        {
            return false;
        }
    }
    for (int step = 0; step < 180; ++step)
    {
        if (!expect(navigation.step_crowd(1.0 / 60.0), "advance disconnected terrain crowd",
                    navigation, terrain_error))
        {
            return false;
        }
        for (std::size_t index = 0; index < handles.size(); ++index)
        {
            if (!expect(navigation.agent_state(handles[index], states[index]) &&
                            states[index].status != NavigationAgentStatus::invalid &&
                            states[index].status != NavigationAgentStatus::arrived &&
                            states[index].position.x() < -1,
                        "no old corridor carries an agent across the removed terrain", navigation,
                        terrain_error))
            {
                return false;
            }
        }
        if (!expect(navigation.agent_state(center_handle, center_state) &&
                        center_state.status == NavigationAgentStatus::invalid &&
                        center_state.has_target &&
                        distance(center_state.target, center_target) == 0 &&
                        distance(center_state.position, center_position) < 0.00001 &&
                        distance(center_state.velocity, {}) < 0.00001,
                    "the center handle stays invalid and motionless while its surface is absent",
                    navigation, terrain_error))
        {
            return false;
        }
    }
    if (!expect(std::all_of(states.begin(), states.end(),
                            [](const auto& state)
                            {
                                return state.status == NavigationAgentStatus::unreachable &&
                                       distance(state.velocity, {}) < 0.00001;
                            }),
                "all original goals become unreachable and motion stops", navigation,
                terrain_error))
    {
        return false;
    }

    const auto blocked = states;
    const auto before_fill = navigation.revision();
    if (!expect(terrain.initialize(baseline, terrain_error) &&
                    terrain.build_mesh(mesh, terrain_error) &&
                    navigation.replace(geometry, dirty) && navigation.revision() > before_fill &&
                    navigation.rebuilt_tiles() > 0 && navigation.rebuilt_tiles() < full_tiles &&
                    navigation.find_path({-9, 0, 0}, {9, 0, 0}, path),
                "restoring sampled terrain reconnects only the declared navigation region",
                navigation, terrain_error))
    {
        return false;
    }
    for (std::size_t index = 0; index < handles.size(); ++index)
    {
        if (!expect(navigation.agent_state(handles[index], states[index]) &&
                        states[index].status == NavigationAgentStatus::pending &&
                        states[index].has_target && states[index].target.x() == 9 &&
                        states[index].target.z() == lanes[index] &&
                        distance(states[index].position, blocked[index].position) < 0.01,
                    "terrain recovery retains each existing handle, pose and original goal",
                    navigation, terrain_error))
        {
            return false;
        }
    }
    if (!expect(navigation.agent_state(center_handle, center_state) &&
                    center_state.status == NavigationAgentStatus::pending &&
                    center_state.has_target && distance(center_state.target, center_target) == 0 &&
                    distance(center_state.position, center_position) < 0.01 &&
                    navigation.step_crowd(1.0 / 60.0) &&
                    navigation.agent_state(center_handle, center_state) &&
                    center_state.status == NavigationAgentStatus::arrived &&
                    distance(center_state.position, center_position) < 0.01 &&
                    distance(center_state.velocity, {}) < 0.00001,
                "surface recovery reactivates the original center handle and retained goal",
                navigation, terrain_error))
    {
        return false;
    }
    // No target is resent after either edit: replace must restore the caller's original request.
    for (int step = 0; step < 1200; ++step)
    {
        if (!expect(navigation.step_crowd(1.0 / 60.0), "advance across restored terrain",
                    navigation, terrain_error))
        {
            return false;
        }
        for (std::size_t index = 0; index < handles.size(); ++index)
        {
            if (!expect(navigation.agent_state(handles[index], states[index]) &&
                            states[index].status != NavigationAgentStatus::invalid &&
                            states[index].status != NavigationAgentStatus::unreachable,
                        "restored terrain produces valid paths for the retained agents", navigation,
                        terrain_error))
            {
                return false;
            }
        }
        if (std::all_of(states.begin(), states.end(), [](const auto& state)
                        { return state.status == NavigationAgentStatus::arrived; }))
        {
            break;
        }
    }
    return expect(std::all_of(states.begin(), states.end(),
                              [](const auto& state)
                              {
                                  return state.status == NavigationAgentStatus::arrived &&
                                         state.position.x() > 8.5 &&
                                         distance(state.position, state.target) < 0.5;
                              }) &&
                      navigation.agent_state(center_handle, center_state) &&
                      center_state.status == NavigationAgentStatus::arrived &&
                      center_state.has_target && distance(center_state.target, center_target) == 0,
                  "all five agents recover their original goals after the terrain is restored",
                  navigation, terrain_error);
}

} // namespace

int main()
{
    return terrain_destruction_and_recovery() ? 0 : 1;
}
