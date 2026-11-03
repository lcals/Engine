#include "plugins/runtime/terrain/terrain_volume.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/physics/mesh_collision_data.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using Clock = std::chrono::steady_clock;
using engine::plugins::terrain::TerrainVolume;

struct Options
{
    std::uint32_t chunk_cells = 16;
    std::uint32_t repeats = 4;
    bool legacy = false;
    bool tick = false;
    bool help = false;
};

struct Sample
{
    std::uint32_t round = 0;
    std::size_t site = 0;
    const char* action = "initial";
    std::size_t rebuilt_chunks = 0;
    std::size_t geometry_changed_parts = 0;
    std::size_t changed_old_triangles = 0;
    std::size_t changed_new_triangles = 0;
    std::size_t total_triangles = 0;
    double submit_ms = 0;
    double tick_ms = 0;
};

void usage()
{
    std::fputs("Usage: engine_terrain_collision_benchmark [--chunk-cells N] [--repeats N]\n"
               "       [--mode parts|legacy] [--tick] [--help]\n"
               "Defaults: chunk-cells=16, repeats=4 (1..10000), mode=parts.\n"
               "JSON reports synchronous CPU submission only; this is not real shelling "
               "acceptance.\n"
               "--tick separately measures the following physics tick at 1/60 s.\n",
               stdout);
}

bool number(std::string_view text, std::uint32_t& output)
{
    const auto result = std::from_chars(text.data(), text.data() + text.size(), output);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && output > 0;
}

bool parse(int argc, char** argv, Options& options)
{
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument = argv[index];
        if (argument == "--help")
        {
            options.help = true;
            return true;
        }
        if (argument == "--tick")
        {
            options.tick = true;
            continue;
        }
        if (++index >= argc)
        {
            return false;
        }
        const std::string_view value = argv[index];
        if (argument == "--chunk-cells")
        {
            if (!number(value, options.chunk_cells))
            {
                return false;
            }
        }
        else if (argument == "--repeats")
        {
            if (!number(value, options.repeats) || options.repeats > 10000)
            {
                return false;
            }
        }
        else if (argument == "--mode" && (value == "parts" || value == "legacy"))
        {
            options.legacy = value == "legacy";
        }
        else
        {
            return false;
        }
    }
    return true;
}

double milliseconds(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

bool submit(engine::PhysicsSystem& physics, engine::Collider& collider, const TerrainVolume& volume,
            const engine::schema::MeshDataT& mesh, engine::MeshCollisionData& previous,
            const Options& options, Sample& sample)
{
    const auto& current = volume.collision_data();
    sample.rebuilt_chunks = volume.rebuilt_chunks();
    sample.total_triangles = mesh.indices.size() / 3;
    const auto slots = std::max(previous.parts.size(), current.parts.size());
    for (std::size_t slot = 0; slot < slots; ++slot)
    {
        const auto* before = slot < previous.parts.size() ? previous.parts[slot].get() : nullptr;
        const auto* after = slot < current.parts.size() ? current.parts[slot].get() : nullptr;
        if (before != after)
        {
            ++sample.geometry_changed_parts;
            sample.changed_old_triangles += before != nullptr ? before->indices.size() / 3 : 0;
            sample.changed_new_triangles += after != nullptr ? after->indices.size() / 3 : 0;
        }
    }

    // Do not extend the old geometry's lifetime across the timed publication.
    previous.parts.clear();
    const auto start = Clock::now();
    const bool submitted = options.legacy ? physics.set_mesh_data(collider, mesh)
                                         : physics.set_mesh_parts(collider, current);
    sample.submit_ms = milliseconds(start);
    if (!submitted)
    {
        std::fputs("terrain collision benchmark: collision submission failed\n", stderr);
        return false;
    }
    if (options.tick)
    {
        const auto tick_start = Clock::now();
        const bool ticked = physics.tick(1.0 / 60.0);
        sample.tick_ms = milliseconds(tick_start);
        if (!ticked)
        {
            std::fputs("terrain collision benchmark: physics tick failed\n", stderr);
            return false;
        }
    }
    previous = current;
    return true;
}

void print_sample(const Sample& sample, bool tick)
{
    std::printf("{\"round\":%u,\"site\":%zu,\"action\":\"%s\",\"rebuilt_chunks\":%zu,"
                "\"geometry_changed_parts\":%zu,\"changed_old_triangles\":%zu,"
                "\"changed_new_triangles\":%zu,\"total_triangles\":%zu,\"submit_ms\":%.6f",
                sample.round, sample.site, sample.action, sample.rebuilt_chunks,
                sample.geometry_changed_parts, sample.changed_old_triangles,
                sample.changed_new_triangles, sample.total_triangles, sample.submit_ms);
    if (tick)
    {
        std::printf(",\"tick_ms\":%.6f", sample.tick_ms);
    }
    std::putchar('}');
}

void print_statistics(const std::vector<Sample>& samples, bool changed, bool tick)
{
    std::vector<double> times;
    double sum = 0;
    std::size_t over_budget = 0;
    for (const auto& sample : samples)
    {
        if ((sample.geometry_changed_parts != 0) == changed)
        {
            const double time = tick ? sample.tick_ms : sample.submit_ms;
            times.push_back(time);
            sum += time;
            over_budget += time > 0.5 ? 1 : 0;
        }
    }
    std::sort(times.begin(), times.end());
    if (times.empty())
    {
        std::fputs("{\"count\":0,\"mean_ms\":null,\"p95_ms\":null,\"max_ms\":null,"
                   "\"over_0_5_ms\":0}",
                   stdout);
        return;
    }
    const auto percentile = (times.size() * 95 + 99) / 100 - 1;
    std::printf("{\"count\":%zu,\"mean_ms\":%.6f,\"p95_ms\":%.6f,\"max_ms\":%.6f,"
                "\"over_0_5_ms\":%zu}",
                times.size(), sum / static_cast<double>(times.size()), times[percentile],
                times.back(), over_budget);
}

void print_summary(const std::vector<Sample>& samples, bool tick)
{
    std::fputs("{\"changed\":", stdout);
    print_statistics(samples, true, tick);
    std::fputs(",\"no_change\":", stdout);
    print_statistics(samples, false, tick);
    std::putchar('}');
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!parse(argc, argv, options))
    {
        std::fputs("terrain collision benchmark: invalid arguments\n", stderr);
        usage();
        return 2;
    }
    if (options.help)
    {
        usage();
        return 0;
    }

    engine::schema::TerrainGeneratorDataT settings;
    settings.cells_x = 64;
    settings.cells_y = 32;
    settings.cells_z = 64;
    settings.chunk_cells = options.chunk_cells;
    settings.origin = {-32, -16, -32};
    settings.height = 0;
    settings.amplitude = 0;
    settings.voxel_size = 1;
    engine::schema::TerrainAssetDataT asset;
    engine::schema::MeshDataT mesh;
    TerrainVolume volume;
    std::string error;
    if (!TerrainVolume::generate(settings, asset, error) || !volume.initialize(asset, error) ||
        !volume.build_mesh(mesh, error))
    {
        std::fprintf(stderr, "terrain collision benchmark: %s\n", error.c_str());
        return 1;
    }

    const engine::EngineConfig config;
    engine::WorldSystem world;
    engine::PhysicsSystem physics(&world);
    if (!world.initialize(config) || !physics.initialize(config))
    {
        std::fputs("terrain collision benchmark: system initialization failed\n", stderr);
        return 1;
    }
    auto* scene = world.create_scene("Terrain collision benchmark");
    auto* node = scene != nullptr ? scene->create_node("Foundation") : nullptr;
    auto* collider = node != nullptr ? node->add_component<engine::Collider>() : nullptr;
    if (collider == nullptr)
    {
        std::fputs("terrain collision benchmark: collider creation failed\n", stderr);
        return 1;
    }
    engine::MeshCollisionData previous;
    Sample initial;
    if (!submit(physics, *collider, volume, mesh, previous, options, initial))
    {
        return 1;
    }
    if (physics.body_count() != 1)
    {
        std::fputs("terrain collision benchmark: expected one static terrain body\n", stderr);
        return 1;
    }

    // Interior and boundary sites are fixed so separate process runs use identical edits.
    const std::array<engine::Vec3, 4> centers = {
        engine::Vec3{-24, -1, -24}, engine::Vec3{-16, -1, -16}, engine::Vec3{0, -1, 0},
        engine::Vec3{11, -1, 9}};
    const std::array<const char*, 4> actions = {"dig", "repeat_dig", "fill", "repeat_fill"};
    std::vector<Sample> samples;
    samples.reserve(static_cast<std::size_t>(options.repeats) * centers.size() * actions.size());
    for (std::uint32_t round = 0; round < options.repeats; ++round)
    {
        for (std::size_t site = 0; site < centers.size(); ++site)
        {
            for (std::size_t action = 0; action < actions.size(); ++action)
            {
                if (!volume.apply_sphere(centers[site], 3.8F, action >= 2, error) ||
                    !volume.build_mesh(mesh, error))
                {
                    std::fprintf(stderr, "terrain collision benchmark: %s\n", error.c_str());
                    return 1;
                }
                Sample sample;
                sample.round = round;
                sample.site = site;
                sample.action = actions[action];
                if (!submit(physics, *collider, volume, mesh, previous, options, sample))
                {
                    return 1;
                }
                samples.push_back(sample);
            }
        }
    }

#ifdef NDEBUG
    constexpr const char* build = "release";
#else
    constexpr const char* build = "debug";
#endif
    std::printf("{\n\"scope\":\"cpu_local_collision_not_real_shelling_acceptance\","
                "\"mode\":\"%s\",\"build\":\"%s\",\"threading\":\"single\","
                "\"cells\":[64,32,64],\"origin\":[-32,-16,-32],\"height\":0,\"amplitude\":0,"
                "\"voxel_size\":1,\"chunk_cells\":%u,\"part_slots\":%zu,\"radius\":3.8,"
                "\"repeats\":%u,\"budget_ms\":0.5,\"p95_method\":\"nearest_rank\","
                "\"sites\":[[-24,-1,-24],[-16,-1,-16],[0,-1,0],[11,-1,9]],\n\"initial\":",
                options.legacy ? "legacy" : "parts", build, options.chunk_cells,
                volume.collision_data().parts.size(), options.repeats);
    print_sample(initial, options.tick);
    std::fputs(",\n\"samples\":[\n", stdout);
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        print_sample(samples[index], options.tick);
        std::fputs(index + 1 == samples.size() ? "\n" : ",\n", stdout);
    }
    std::fputs("],\n\"submit_summary\":", stdout);
    print_summary(samples, false);
    if (options.tick)
    {
        std::fputs(",\n\"tick_summary\":", stdout);
        print_summary(samples, true);
    }
    std::fputs("\n}\n", stdout);
    return 0;
}
