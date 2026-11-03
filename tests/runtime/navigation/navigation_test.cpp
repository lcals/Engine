#include "runtime/engine_config.hpp"
#include "runtime/navigation/navigation_system.hpp"
#include "runtime/schema/mesh_generated.h"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <semaphore>
#include <vector>

namespace
{

engine::TaskSystem* navigation_tasks = nullptr;

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
    engine::NavigationSystem navigation(navigation_tasks);
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
    engine::NavigationSystem navigation(navigation_tasks);
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

double distance(const engine::Vec3& a, const engine::Vec3& b)
{
    const double x = a.x() - b.x();
    const double y = a.y() - b.y();
    const double z = a.z() - b.z();
    return std::sqrt(x * x + y * y + z * z);
}

bool advance(engine::NavigationSystem& navigation, int steps)
{
    for (int step = 0; step < steps; ++step)
    {
        if (!navigation.step_crowd(1.0 / 60.0))
        {
            return expect(false, "crowd accepts each finite fixed step", navigation);
        }
    }
    return true;
}

bool crowd_lifecycle()
{
    using Status = engine::NavigationAgentStatus;
    engine::NavigationSystem navigation(navigation_tasks);
    auto mesh = grid(24, 16);
    std::array sources{engine::NavigationGeometry{&mesh}};
    const engine::NavigationBounds bounds{{0, -2, 0}, {24, 4, 16}};
    const engine::NavigationAgentSettings settings;
    engine::NavigationAgentHandle first{99, 99};
    engine::NavigationAgentHandle second;
    engine::NavigationAgentState state;
    if (!expect(
            !navigation.configure_crowd(2) &&
                !navigation.create_agent({2, 0, 8}, settings, first) && first.generation == 0 &&
                navigation.initialize({}) && !navigation.configure_crowd(2) &&
                navigation.build(sources, bounds, {}) && !navigation.configure_crowd(0) &&
                !navigation.configure_crowd(4097) && navigation.configure_crowd(2) &&
                !navigation.create_agent({100, 0, 8}, settings, first) && first.generation == 0,
            "crowd rejects missing mesh, invalid capacity and positions without leaving handles",
            navigation))
    {
        return false;
    }
    auto invalid = settings;
    invalid.radius = 0;
    if (!expect(!navigation.create_agent({2, 0, 8}, invalid, first),
                "zero crowd radius is rejected before Detour division by radius", navigation))
    {
        return false;
    }
    invalid.radius = 0.4F;
    if (!expect(!navigation.create_agent({2, 0, 8}, invalid, first),
                "agent radius cannot exceed baked clearance", navigation))
    {
        return false;
    }
    invalid = settings;
    invalid.height = 2;
    if (!expect(!navigation.create_agent({2, 0, 8}, invalid, first),
                "agent height cannot exceed baked clearance", navigation))
    {
        return false;
    }
    invalid = settings;
    invalid.max_acceleration = std::numeric_limits<float>::quiet_NaN();
    if (!expect(!navigation.create_agent({2, 0, 8}, invalid, first) &&
                    navigation.create_agent({2, 0, 8}, settings, first) &&
                    navigation.create_agent({2, 0, 2}, settings, second) &&
                    navigation.set_agent_target(first, {22, 0, 8}) &&
                    navigation.configure_crowd(2) && !navigation.configure_crowd(3) &&
                    navigation.agent_state(first, state) && state.status == Status::pending &&
                    state.has_target,
                "finite settings, capacity and repeated configuration preserve active targets",
                navigation))
    {
        return false;
    }
    engine::NavigationAgentHandle overflow{99, 99};
    const auto initial = state.position;
    if (!expect(!navigation.create_agent({2, 0, 12}, settings, overflow) &&
                    overflow.generation == 0 && navigation.step_crowd(0) &&
                    navigation.step_crowd(1.0 / 120.0) && navigation.agent_state(first, state) &&
                    distance(initial, state.position) < 1.0e-8 &&
                    navigation.step_crowd(1.0 / 120.0) && navigation.agent_state(first, state) &&
                    state.position.x() > initial.x() && state.status == Status::moving,
                "zero and half steps do not integrate; two half steps advance one complete step",
                navigation))
    {
        return false;
    }
    if (!expect(advance(navigation, 30) && navigation.stop_agent(first) &&
                    navigation.agent_state(first, state) && !state.has_target &&
                    state.status == Status::idle && distance(state.velocity, {}) == 0,
                "stop immediately clears the queued target and all instantaneous velocity",
                navigation))
    {
        return false;
    }
    const auto stopped = state.position;
    if (!expect(advance(navigation, 60) && navigation.agent_state(first, state) &&
                    distance(stopped, state.position) < 0.001 && !navigation.step_crowd(-1) &&
                    !navigation.step_crowd(std::numeric_limits<double>::infinity()) &&
                    navigation.set_agent_target(first, {22, 0, 8}) &&
                    !navigation.set_agent_target(first, {100, 0, 8}) &&
                    navigation.agent_state(first, state) && state.status == Status::unreachable &&
                    state.has_target && distance(state.velocity, {}) == 0 &&
                    advance(navigation, 20) && navigation.agent_state(first, state) &&
                    distance(stopped, state.position) < 0.001,
                "invalid time and failed new targets never continue the old motion", navigation))
    {
        return false;
    }
    auto stationary = settings;
    stationary.max_speed = 0.03F;
    if (!expect(
            navigation.set_agent_settings(first, stationary) &&
                navigation.set_agent_target(first, {22, 0, 8}) && advance(navigation, 20) &&
                navigation.agent_state(first, state) && distance(stopped, state.position) < 0.02 &&
                navigation.set_agent_settings(first, settings) && advance(navigation, 20) &&
                navigation.agent_state(first, state) && state.position.x() > stopped.x(),
            "parameter updates change live crowd motion without replacing identity", navigation))
    {
        return false;
    }
    const auto old = first;
    if (!expect(navigation.remove_agent(first) && !navigation.remove_agent(first) &&
                    navigation.create_agent({4, 0, 8}, settings, first) && first.slot == old.slot &&
                    first.generation != old.generation && !navigation.stop_agent(old) &&
                    !navigation.agent_state(old, state) && state.status == Status::invalid &&
                    !state.has_target && distance(state.position, {}) == 0 &&
                    navigation.agent_state(first, state) && state.status == Status::idle,
                "reused slots reject stale generations and failed queries clear output",
                navigation))
    {
        return false;
    }
    if (!expect(navigation.build(sources, bounds, {}) && !navigation.agent_state(first, state) &&
                    navigation.configure_crowd(1) &&
                    navigation.create_agent({4, 0, 8}, settings, first),
                "complete builds invalidate old handles and allow explicit re-registration",
                navigation))
    {
        return false;
    }
    const auto before_clear = first;
    navigation.clear();
    if (!expect(!navigation.agent_state(first, state) && !navigation.step_crowd(0) &&
                    navigation.build(sources, bounds, {}) && navigation.configure_crowd(1) &&
                    navigation.create_agent({4, 0, 8}, settings, first) &&
                    first.generation != before_clear.generation &&
                    !navigation.set_agent_target(before_clear, {6, 0, 8}),
                "clear releases crowds before their mesh and old handles cannot revive",
                navigation))
    {
        return false;
    }
    navigation.shutdown();
    return expect(
        navigation.initialize({}) && navigation.build(sources, bounds, {}) &&
            navigation.configure_crowd(1) && navigation.create_agent({4, 0, 8}, settings, second) &&
            second.generation != first.generation && !navigation.agent_state(first, state),
        "shutdown and reinitialize also invalidate all previous identities", navigation);
}

bool crowd_crossing()
{
    using Status = engine::NavigationAgentStatus;
    engine::NavigationSystem navigation(navigation_tasks);
    auto mesh = grid(24, 24);
    std::array sources{engine::NavigationGeometry{&mesh}};
    if (!expect(navigation.initialize({}) &&
                    navigation.build(sources, {{0, -2, 0}, {24, 4, 24}}, {}) &&
                    navigation.configure_crowd(4),
                "crossing crowd is configured on a real tiled navmesh", navigation))
    {
        return false;
    }
    const std::array starts{engine::Vec3{3, 0, 10}, engine::Vec3{21, 0, 11}, engine::Vec3{11, 0, 3},
                            engine::Vec3{10, 0, 21}};
    const std::array targets{engine::Vec3{21, 0, 10}, engine::Vec3{3, 0, 11},
                             engine::Vec3{11, 0, 21}, engine::Vec3{10, 0, 3}};
    std::array<engine::NavigationAgentHandle, 4> handles{};
    std::array<engine::NavigationAgentState, 4> states{};
    for (std::size_t index = 0; index < handles.size(); ++index)
    {
        if (!expect(navigation.create_agent(starts[index], {}, handles[index]) &&
                        navigation.set_agent_target(handles[index], targets[index]),
                    "each crossing agent receives an independent target", navigation))
        {
            return false;
        }
    }
    double minimum_distance = 100;
    double maximum_deviation = 0;
    for (int step = 0; step < 1800; ++step)
    {
        if (!navigation.step_crowd(1.0 / 60.0))
        {
            return false;
        }
        for (std::size_t index = 0; index < handles.size(); ++index)
        {
            if (!expect(navigation.agent_state(handles[index], states[index]) &&
                            states[index].status != Status::invalid &&
                            states[index].status != Status::unreachable &&
                            std::isfinite(states[index].position.x()),
                        "crossing agents retain valid finite motion", navigation))
            {
                return false;
            }
            const double deviation = index < 2
                                         ? std::abs(states[index].position.z() - starts[index].z())
                                         : std::abs(states[index].position.x() - starts[index].x());
            maximum_deviation = std::max(maximum_deviation, deviation);
            for (std::size_t other = 0; other < index; ++other)
            {
                minimum_distance = std::min(
                    minimum_distance, distance(states[index].position, states[other].position));
            }
        }
        if (std::all_of(states.begin(), states.end(),
                        [](const auto& state) { return state.status == Status::arrived; }))
        {
            break;
        }
    }
    return expect(minimum_distance > 0.4 && maximum_deviation > 0.05 &&
                      std::all_of(states.begin(), states.end(), [](const auto& state)
                                  { return state.status == Status::arrived; }),
                  "opposing and crossing agents avoid overlap, steer off direct lines and arrive",
                  navigation);
}

bool crowd_moving_target()
{
    using Status = engine::NavigationAgentStatus;
    engine::NavigationSystem navigation(navigation_tasks);
    auto mesh = grid(24, 16);
    const std::array sources{engine::NavigationGeometry{&mesh}};
    engine::NavigationAgentHandle handle;
    engine::NavigationAgentState state;
    engine::NavigationAgentSettings settings;
    settings.max_acceleration = 12;
    if (!expect(navigation.initialize({}) &&
                    navigation.build(sources, {{0, -2, 0}, {24, 4, 16}}, {}) &&
                    navigation.configure_crowd(1) &&
                    navigation.create_agent({2, 0, 8}, settings, handle) &&
                    navigation.set_agent_target(handle, {22, 0, 8}) && advance(navigation, 60) &&
                    navigation.agent_state(handle, state) && state.velocity.x() > 2.5,
                "pursuer accelerates before its target begins moving", navigation))
    {
        return false;
    }
    const auto start = state.position;
    for (int frame = 0; frame < 120; ++frame)
    {
        const auto velocity = state.velocity;
        const engine::Vec3 target{22, 0, 8 + 0.5 * std::sin(frame / 60.0)};
        if (!expect(navigation.set_agent_target(handle, target) &&
                        navigation.agent_state(handle, state) && state.status == Status::pending &&
                        distance(state.velocity, velocity) < 1.0e-8 && advance(navigation, 1) &&
                        navigation.agent_state(handle, state) && state.status == Status::moving &&
                        state.velocity.x() > 2.5,
                    "continuous target updates preserve motion instead of restarting acceleration",
                    navigation))
        {
            return false;
        }
    }
    if (!expect(state.position.x() - start.x() > 5 &&
                    !navigation.set_agent_target(handle, {100, 0, 8}) &&
                    navigation.agent_state(handle, state) && state.status == Status::unreachable &&
                    distance(state.velocity, {}) == 0,
                "moving pursuer makes progress but a failed target immediately stops it",
                navigation))
    {
        return false;
    }
    const auto stopped = state.position;
    return expect(advance(navigation, 30) && navigation.agent_state(handle, state) &&
                      distance(state.position, stopped) < 0.001 &&
                      navigation.set_agent_target(handle, {22, 0, 8}) && advance(navigation, 60) &&
                      navigation.agent_state(handle, state) && state.position.x() > stopped.x() + 2,
                  "failed retarget discards old motion and a later valid target resumes pursuit",
                  navigation);
}

bool crowd_rebuild()
{
    using Status = engine::NavigationAgentStatus;
    engine::NavigationSystem navigation(navigation_tasks);
    auto mesh = grid(24, 16, 10, 14, 4, 12);
    std::array sources{engine::NavigationGeometry{&mesh}};
    const engine::NavigationBounds bounds{{0, -2, 0}, {24, 4, 16}};
    const engine::NavigationBounds cut{{10, -1, 0}, {14, 1, 16}};
    engine::NavigationSettings settings;
    settings.tile_size = 16;
    engine::NavigationAgentHandle handle;
    engine::NavigationAgentState state;
    if (!expect(navigation.initialize({}) && navigation.build(sources, bounds, settings) &&
                    navigation.configure_crowd(1) &&
                    navigation.create_agent({2, 0, 8}, {}, handle) &&
                    navigation.set_agent_target(handle, {22, 0, 8}),
                "agent accepts the complete route around a multi-tile obstacle", navigation))
    {
        return false;
    }
    double deviation = 0;
    for (int step = 0; step < 1800; ++step)
    {
        if (!expect(navigation.step_crowd(1.0 / 60.0) && navigation.agent_state(handle, state) &&
                        state.status != Status::invalid && state.status != Status::unreachable,
                    "obstacle-following remains on a complete valid corridor", navigation))
        {
            return false;
        }
        deviation = std::max(deviation, std::abs(state.position.z() - 8));
        if (state.status == Status::arrived)
        {
            break;
        }
    }
    if (!expect(state.status == Status::arrived && deviation > 4 &&
                    distance(state.position, {22, 0.1, 8}) < 0.25 &&
                    navigation.set_agent_target(handle, {2, 0, 8}) && advance(navigation, 20),
                "agent reaches the actual target after routing around the obstacle", navigation))
    {
        return false;
    }
    mesh = grid(24, 16, 10, 14, 0, 16);
    if (!expect(navigation.replace(sources, cut) && navigation.agent_state(handle, state) &&
                    state.status == Status::pending && advance(navigation, 60) &&
                    navigation.agent_state(handle, state) && state.status == Status::unreachable &&
                    state.has_target && state.target.x() == 2 && distance(state.velocity, {}) == 0,
                "local disconnection preserves identity and requested target but stops old motion",
                navigation))
    {
        return false;
    }
    const auto blocked = state.position;
    if (!expect(advance(navigation, 60) && navigation.agent_state(handle, state) &&
                    distance(blocked, state.position) < 0.001,
                "discarded crowd corridors cannot continue across removed tiles", navigation))
    {
        return false;
    }
    mesh = grid(24, 16);
    if (!expect(navigation.replace(sources, cut) && navigation.agent_state(handle, state) &&
                    state.status == Status::pending && advance(navigation, 30) &&
                    navigation.agent_state(handle, state) && state.position.x() < blocked.x(),
                "restored tiles reissue the original target through a new crowd query", navigation))
    {
        return false;
    }
    mesh.indices[0] = std::numeric_limits<std::uint32_t>::max();
    if (!expect(!navigation.replace(sources, cut) && advance(navigation, 60) &&
                    navigation.agent_state(handle, state) && state.status == Status::unreachable &&
                    distance(state.velocity, {}) == 0,
                "failed recook also discards queued movement through invalidated tiles",
                navigation))
    {
        return false;
    }
    if (!expect(navigation.replace({}, bounds) && navigation.agent_state(handle, state) &&
                    state.status == Status::invalid && distance(state.velocity, {}) == 0 &&
                    advance(navigation, 60) && navigation.agent_state(handle, state) &&
                    state.status == Status::invalid,
                "removing the agent surface retains a queryable invalid identity without motion",
                navigation))
    {
        return false;
    }
    mesh = grid(24, 16);
    return expect(navigation.replace(sources, bounds) && navigation.agent_state(handle, state) &&
                      state.status == Status::pending && advance(navigation, 900) &&
                      navigation.agent_state(handle, state) && state.status == Status::arrived,
                  "surface recovery preserves handles and restores explicit outstanding targets",
                  navigation);
}

bool run_suite()
{
    return local_rebuild() && geometry_and_boundaries() && crowd_lifecycle() && crowd_crossing() &&
           crowd_moving_target() && crowd_rebuild();
}

} // namespace

int main()
{
    if (!run_suite())
    {
        return 1;
    }
    engine::ThreadSystem threads({.worker_limit = 2});
    engine::TaskSystem tasks(threads, {.name = "Navigation tests", .max_concurrency = 2});
    engine::NavigationSystem invalid_domain(&tasks);
    engine::NavigationSystem invalid_threshold(nullptr, 0);
    if (invalid_domain.initialize({}) || invalid_threshold.initialize({}) ||
        !threads.initialize({}) || !tasks.initialize({}))
    {
        std::fputs("navigation test: invalid configuration or task lifecycle\n", stderr);
        return 1;
    }
    navigation_tasks = &tasks;
    const bool parallel_ok = run_suite();
    navigation_tasks = nullptr;
    if (!parallel_ok || !tasks.wait())
    {
        return 1;
    }
    tasks.shutdown();

    engine::TaskConfig limited_config;
    limited_config.name = "Navigation limited";
    limited_config.max_concurrency = 1;
    limited_config.max_pending = 1;
    engine::TaskSystem limited(threads, limited_config);
    if (!limited.initialize({}))
    {
        return 1;
    }
    std::binary_semaphore gate(0);
    auto occupied = limited.submit("Hold sole pending slot",
                                   [&]
                                   {
                                       gate.acquire();
                                       return true;
                                   });
    if (!occupied.valid())
    {
        return 1;
    }
    navigation_tasks = &limited;
    const bool fallback_ok = local_rebuild();
    navigation_tasks = nullptr;
    gate.release();
    return occupied.get() && fallback_ok ? 0 : 1;
}
