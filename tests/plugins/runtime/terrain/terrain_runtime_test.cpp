#include "plugins/runtime/terrain/codecs/terrain_resource_codec.hpp"
#include "resource_services.hpp"
#include "scene_snapshot.hpp"

#include "plugins/runtime/terrain/terrain_component.hpp"
#include "plugins/runtime/terrain/terrain_system.hpp"
#include "plugins/runtime/terrain/terrain_volume.hpp"
#include "resource_fixture.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/rigid_body.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/schema/terrain_state_generated.h"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"
#include "runtime/utils/mesh_utils.hpp"
#include "runtime/world/world_system.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <semaphore>
#include <string>
#include <vector>

namespace
{
using namespace engine;
using plugins::terrain::TerrainComponent;
using plugins::terrain::TerrainSystem;
using plugins::terrain::TerrainVolume;

bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "terrain runtime: %s\n", message);
    }
    return condition;
}

schema::TerrainGeneratorDataT generator()
{
    schema::TerrainGeneratorDataT result;
    result.cells_x = result.cells_y = result.cells_z = 32;
    result.chunk_cells = 16;
    result.origin = {-16, -16, -16};
    result.amplitude = 0;
    result.height = 0;
    return result;
}

using Triangle = std::array<float, 18>;
std::vector<Triangle> triangles(const schema::MeshDataT& mesh)
{
    std::vector<Triangle> result;
    for (std::size_t index = 0; index < mesh.indices.size(); index += 3)
    {
        Triangle triangle{};
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const auto& vertex = mesh.vertices[mesh.indices[index + corner]];
            const std::array values = {vertex.x(),  vertex.y(),  vertex.z(),
                                       vertex.nx(), vertex.ny(), vertex.nz()};
            std::copy(values.begin(), values.end(), triangle.begin() + corner * values.size());
        }
        result.push_back(triangle);
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool volume_contract()
{
    auto settings = generator();
    schema::TerrainAssetDataT asset;
    schema::TerrainAssetDataT alternate;
    schema::MeshDataT first;
    schema::MeshDataT second;
    TerrainVolume fine;
    TerrainVolume coarse;
    std::string error;
    if (!expect(TerrainVolume::generate(settings, asset, error) && fine.initialize(asset, error) &&
                    fine.build_mesh(first, error) && MeshUtils::validate(first) &&
                    fine.chunk_count() == 8 && fine.rebuilt_chunks() == 8,
                "generate a valid closed mesh in eight chunks"))
    {
        return false;
    }
    const auto baseline = triangles(first);
    if (!expect(fine.build_mesh(second, error) && fine.rebuilt_chunks() == 0 &&
                    triangles(second) == baseline,
                "unchanged rebuild reuses all chunk geometry"))
    {
        return false;
    }
    settings.chunk_cells = 32;
    if (!expect(TerrainVolume::generate(settings, alternate, error) &&
                    alternate.density == asset.density && coarse.initialize(alternate, error) &&
                    coarse.build_mesh(second, error) && coarse.chunk_count() == 1 &&
                    triangles(second) == baseline,
                "16 and 32 cell partitions produce identical triangles and boundary normals"))
    {
        return false;
    }
    const Vec3 center{0, -6, -8};
    if (!expect(fine.apply_sphere(center, 2.5F, false, error) && fine.build_mesh(first, error) &&
                    fine.rebuilt_chunks() >= 2 && fine.rebuilt_chunks() < fine.chunk_count() &&
                    coarse.apply_sphere(center, 2.5F, false, error) &&
                    coarse.build_mesh(second, error) && triangles(first) == triangles(second),
                "cross-boundary edit rebuilds affected neighbors and matches full regeneration"))
    {
        return false;
    }
    const auto previous = fine.data();
    if (!expect(!fine.apply_sphere(center, -1.0F, false, error) && !error.empty() &&
                    fine.data() == previous &&
                    !fine.apply_sphere({std::numeric_limits<double>::quiet_NaN(), 0, 0}, 2, false,
                                       error) &&
                    fine.data() == previous,
                "invalid edits preserve sampled volume"))
    {
        return false;
    }
    alternate = asset;
    alternate.density.pop_back();
    if (!expect(!fine.initialize(alternate, error) && fine.data() == previous,
                "invalid resource cannot replace a valid volume"))
    {
        return false;
    }
    settings.chunk_cells = 8;
    return expect(!TerrainVolume::generate(settings, alternate, error) && alternate.density.empty(),
                  "unsupported partition rejects generation with empty output");
}

bool add_terrain(test_support::ResourceFixture& fixture, const schema::TerrainAssetDataT& asset)
{
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishTerrainAssetDataBuffer(builder, schema::TerrainAssetData::Pack(builder, &asset));
    auto entry = std::make_unique<schema::AssetEntryT>();
    entry->guid = "81000000-0000-4000-8000-000000000001";
    entry->path = "test.terrain";
    entry->kind = schema::AssetKind::terrain;
    ByteBuffer bytes;
    const ByteView payload(reinterpret_cast<const std::byte*>(builder.GetBufferPointer()),
                           builder.GetSize());
    if (fixture.root.empty() || !ResourceSystem::encode_resource(entry->guid, entry->kind, payload,
                                                                 bytes, entry->header_size))
    {
        return false;
    }
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto value : bytes)
    {
        hash = (hash ^ std::to_integer<std::uint8_t>(value)) * 1099511628211ULL;
    }
    char digest[17]{};
    std::snprintf(digest, sizeof(digest), "%016llx", static_cast<unsigned long long>(hash));
    entry->content_hash = digest;
    entry->content_size = bytes.size();
    entry->location = std::make_unique<schema::FileLocationT>();
    entry->location->mount = "content";
    entry->location->path = "gen/" + entry->guid;
    entry->location->length = bytes.size();
    std::ofstream stream(fixture.root / entry->location->path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    stream.close();
    if (!stream)
    {
        return false;
    }
    fixture.catalog.entries.push_back(std::move(entry));
    return true;
}

bool runtime_contract()
{
    const EngineConfig config;
    test_support::ResourceServices services{};
    auto& resources = services.core;
    WorldSystem world{&services.loader};
    PhysicsSystem physics(&world);
    TerrainSystem terrain(world, physics);
    test_support::ResourceFixture fixture;
    schema::TerrainAssetDataT asset;
    std::string error;
    if (!expect((resources.add<engine::plugins::terrain::TerrainResourceCodec>() &&
                 services.initialize(config)) &&
                    world.initialize(config) && physics.initialize(config) &&
                    terrain.initialize(config) &&
                    TerrainVolume::generate(generator(), asset, error) &&
                    add_terrain(fixture, asset) &&
                    services.publish_and_load(fixture.catalog, {{"content", fixture.root}}),
                "initialize terrain plugin with real GUID resource loading"))
    {
        return false;
    }
    auto* scene = world.create_scene("Terrain contracts");
    auto node = scene ? scene->create_node("Volume") : engine::Node{};
    auto component = node ? node.add_component<TerrainComponent>() : TerrainComponent{};
    if (!expect(component.valid() && !component.ready(), "empty component has no generated output"))
    {
        return false;
    }
    auto settings = test_support::read_settings(component);
    settings.terrain_guid = fixture.catalog.entries.front()->guid;
    if (!expect(component.set_settings(settings) && terrain.tick(0) && component.ready() &&
                    physics.tick(0) && physics.body_count() == 1,
                "configured component creates rendering and collision through the plugin"))
    {
        std::fprintf(stderr, "terrain diagnostic: %s\n", component.last_error().c_str());
        return false;
    }
    auto cast = [&](double x, bool expected)
    {
        float fraction = 0;
        return physics.cast_sphere({x, 4, 0.25}, {0, -12, 0}, 0.1F, fraction) &&
               (expected ? fraction < 1.0F : fraction == 1.0F);
    };
    auto renderer = node.get_component<MeshRenderer>();
    auto collider = node.get_component<Collider>();
    schema::SceneDataT scene_data;
    scene_data.schema_version = 2;
    if (!expect(renderer && collider && renderer.transient() && collider.transient() &&
                    collider.mesh_data() == nullptr &&
                    test_support::scene_nodes(*scene).size() == 1 &&
                    scene->build_data(scene_data) && scene_data.nodes.size() == 1 &&
                    scene_data.nodes.front()->components.size() == 2 && cast(0.25, true),
                "scene snapshot retains only Transform and Terrain, excluding generated outputs"))
    {
        return false;
    }
    float original_fraction = 0;
    float edited_fraction = 0;
    if (!expect(physics.cast_ray(collider, {-8, 4, 0.25}, {0, -8, 0}, original_fraction) &&
                    original_fraction < 1 && component.apply_sphere({-8, -1, 0.25}, 2, false) &&
                    component.rebuilt_chunks() > 0 &&
                    component.rebuilt_chunks() < component.chunk_count() &&
                    physics.cast_ray(collider, {-8, 4, 0.25}, {0, -8, 0}, edited_fraction) &&
                    edited_fraction > original_fraction + 0.1F && edited_fraction < 1 &&
                    cast(12, true) && physics.body_count() == 1 &&
                    test_support::scene_nodes(*scene).size() == 1 &&
                    node.get_component<Collider>() == collider && component.reset() &&
                    physics.cast_ray(collider, {-8, 4, 0.25}, {0, -8, 0}, edited_fraction) &&
                    std::abs(edited_fraction - original_fraction) < 0.001F,
                "a local brush replaces only affected chunks on the same collider and restores "
                "the original surface"))
    {
        return false;
    }
    physics.clear();
    if (!expect(physics.tick(0) && physics.body_count() == 1 && cast(12, true) &&
                    node.get_component<Collider>() == collider,
                "clearing physics rebuilds all terrain chunks from the retained collider"))
    {
        return false;
    }
    const auto initial_revision = component.revision();
    ByteBuffer saved;
    const auto state_path = fixture.root / "state" / "dig.bin";
    if (!expect(component.apply_sphere({0, -3, 0}, 8, false) &&
                    component.revision() > initial_revision && cast(0.25, false) &&
                    cast(12, true) && component.snapshot(saved) && component.save_state(state_path),
                "dig opens collision immediately while retaining the surrounding surface") ||
        !expect(component.reset() && cast(0.25, true) && component.load_state(state_path) &&
                    cast(0.25, false),
                "file save and restore recover modified volume after reset"))
    {
        return false;
    }
    const auto revision = component.revision();
    ByteBuffer after;
    auto corrupted = saved;
    corrupted[4] = std::byte{0};
    if (!expect(!component.restore(corrupted) && component.revision() == revision &&
                    component.snapshot(after) && after == saved && cast(0.25, false),
                "corrupt state preserves volume and collision"))
    {
        return false;
    }
    auto state =
        schema::UnPackTerrainStateData(reinterpret_cast<const std::uint8_t*>(saved.data()));
    state->base_hash = "different-base-content";
    flatbuffers::FlatBufferBuilder mismatch;
    schema::FinishTerrainStateDataBuffer(mismatch,
                                         schema::TerrainStateData::Pack(mismatch, state.get()));
    const ByteView wrong_base(reinterpret_cast<const std::byte*>(mismatch.GetBufferPointer()),
                              mismatch.GetSize());
    if (!expect(!component.restore(wrong_base) && component.revision() == revision &&
                    component.snapshot(after) && after == saved,
                "state from another base resource is rejected without mutation"))
    {
        return false;
    }
    auto transform = node.get_component<Transform>();
    const auto original_pose = test_support::read_settings(transform);
    auto invalid_pose = original_pose;
    invalid_pose.scale = {-1, 1, 1};
    const auto mesh_before = triangles(*renderer.mesh_data());
    if (!expect(
            transform.set_settings(invalid_pose) && !component.apply_sphere({0, -3, 0}, 3, true) &&
                component.revision() == revision && component.snapshot(after) && after == saved &&
                triangles(*renderer.mesh_data()) == mesh_before &&
                transform.set_settings(original_pose) && cast(0.25, false) && cast(12, true),
            "failed physics commit rolls back volume and rendering and preserves old collision"))
    {
        return false;
    }
    auto no_collision = settings;
    no_collision.collision = false;
    if (!expect(
            component.set_settings(no_collision) && !collider.enabled() &&
                physics.body_count() == 0 && cast(12, false) &&
                transform.set_settings(invalid_pose) && !component.set_settings(settings) &&
                test_support::read_settings(component) == no_collision && !collider.enabled() &&
                physics.body_count() == 0 && transform.set_settings(original_pose) &&
                component.set_settings(settings) && collider.enabled() && cast(12, true) &&
                component.snapshot(after) && after == saved,
            "collision changes publish immediately and failed reenabling restores disabled state"))
    {
        return false;
    }
    if (!expect(component.apply_sphere({0, 0, 0}, 100, false) && component.triangle_count() == 0 &&
                    !renderer.visible() && !collider.enabled() && physics.body_count() == 0 &&
                    cast(0.25, false) && terrain.synchronize() && component.triangle_count() == 0 &&
                    !renderer.visible() && !collider.enabled() &&
                    component.apply_sphere({0, -4, 0}, 4, true) && component.triangle_count() > 0 &&
                    renderer.visible() && collider.enabled() && physics.body_count() == 1 &&
                    node.get_component<Collider>() == collider &&
                    test_support::scene_nodes(*scene).size() == 1 && cast(0.25, true) &&
                    component.restore(saved) && cast(0.25, false) && cast(12, true),
                "empty terrain stays hidden after synchronization and filling restores collision"))
    {
        return false;
    }
    if (!component.set_enabled(false))
    {
        std::abort();
    }
    if (!expect(terrain.tick(0) && physics.tick(0) && !renderer.enabled() && !collider.enabled() &&
                    physics.body_count() == 0 && cast(12, false),
                "disabled terrain removes both render output and collision"))
    {
        return false;
    }
    if (!component.set_enabled(true))
    {
        std::abort();
    }
    if (!expect(terrain.tick(0) && physics.tick(0) && cast(12, true) && physics.body_count() == 1 &&
                    node.get_component<Collider>() == collider,
                "reenabling terrain restores its modified surface"))
    {
        return false;
    }
    auto cleared = settings;
    cleared.terrain_guid.clear();
    if (!expect(
            component.set_settings(cleared) && !component.ready() && component.chunk_count() == 0 &&
                component.triangle_count() == 0 && terrain.tick(0) && physics.tick(0) &&
                physics.body_count() == 0 && node.get_component<MeshRenderer>().valid() == false &&
                node.get_component<Collider>().valid() == false &&
                component.triangle_count() == 0 && component.set_settings(settings) &&
                component.restore(saved) && cast(12, true),
            "clearing the resource resets statistics and the component can be configured again") ||
        !expect(node.remove_component(TerrainComponent::component_type) && terrain.tick(0) &&
                    physics.tick(0) && node.get_component<MeshRenderer>().valid() == false &&
                    node.get_component<Collider>().valid() == false && physics.body_count() == 0 &&
                    scene->build_data(scene_data) &&
                    scene_data.nodes.front()->components.size() == 1,
                "removing terrain removes only generated components and their physical bodies"))
    {
        return false;
    }
    world.destroy_scene();
    terrain.shutdown();
    physics.shutdown();
    world.shutdown();
    services.shutdown();
    return true;
}

bool subsystem_contract()
{
    const EngineConfig config;
    test_support::ResourceServices services{};
    auto& resources = services.core;
    WorldSystem world{&services.loader};
    PhysicsSystem physics(&world);
    auto owned = std::make_unique<TerrainSystem>(world, physics);
    auto* terrain = owned.get();
    std::unique_ptr<System> child = std::move(owned);
    test_support::ResourceFixture fixture;
    schema::TerrainAssetDataT asset;
    std::string error;
    if (!expect(!terrain->synchronize() && world.add_subsystem(child) &&
                    (resources.add<engine::plugins::terrain::TerrainResourceCodec>() &&
                     services.initialize(config)) &&
                    world.initialize(config) && physics.initialize(config) &&
                    TerrainVolume::generate(generator(), asset, error) &&
                    add_terrain(fixture, asset) &&
                    services.publish_and_load(fixture.catalog, {{"content", fixture.root}}),
                "host owns the terrain subsystem and initializes its shared dependencies"))
    {
        return false;
    }
    auto* scene = world.create_scene("Preview terrain");
    auto node = scene ? scene->create_node("Volume") : engine::Node{};
    auto component = node ? node.add_component<TerrainComponent>() : TerrainComponent{};
    if (!expect(component.valid(), "create a pending terrain in the preview scene"))
    {
        return false;
    }
    auto settings = test_support::read_settings(component);
    settings.terrain_guid = fixture.catalog.entries.front()->guid;
    float fraction = 1;
    if (!expect(component.set_settings(settings) && !terrain->tick(0) && !component.ready() &&
                    terrain->synchronize() && component.ready() && physics.body_count() == 1 &&
                    physics.cast_sphere({0.25, 4, 0.25}, {0, -12, 0}, 0.1F, fraction) &&
                    fraction < 1,
                "preview explicitly synchronizes terrain while direct subsystem ticking stays "
                "forbidden"))
    {
        return false;
    }
    const auto revision = component.revision();
    if (!expect(world.tick(0) && component.revision() == revision,
                "normal parent ticking reuses the synchronized terrain without rebuilding"))
    {
        return false;
    }
    world.shutdown();
    const bool stopped = !terrain->synchronize();
    physics.shutdown();
    services.shutdown();
    return expect(stopped, "explicit synchronization cannot run after subsystem shutdown");
}

schema::TerrainAssetDataT collapse_asset(bool bridge)
{
    schema::TerrainAssetDataT asset;
    asset.cells_x = asset.cells_y = asset.cells_z = 16;
    asset.chunk_cells = 16;
    asset.voxel_size = 1;
    asset.density.assign(17 * 17 * 17, 1);
    for (unsigned z = 2; z <= 13; ++z)
    {
        for (unsigned y = 1; y <= 9; ++y)
        {
            for (unsigned x = 2; x <= 13; ++x)
            {
                const bool ground = y <= 2;
                const bool island = x >= 6 && x <= 9 && y >= 6 && z >= 6 && z <= 9;
                const bool pillar = bridge && x == 7 && z == 7 && y >= 3 && y <= 5;
                if (ground || island || pillar)
                {
                    asset.density[(z * 17 + y) * 17 + x] = -1;
                }
            }
        }
    }
    return asset;
}

bool island_contract()
{
    TerrainVolume volume;
    const auto asset = collapse_asset(false);
    std::string error;
    std::vector<plugins::terrain::TerrainFragment> fragments;
    if (!expect(volume.initialize(asset, error) &&
                    !volume.extract_detached(8, 7, 2, fragments, error) && !error.empty() &&
                    fragments.empty() && volume.data() == asset,
                "fragment-budget failure leaves every detached density sample intact") ||
        !expect(!volume.extract_detached(0, 8, 2, fragments, error) && volume.data() == asset &&
                    !volume.extract_detached(8, 8, 9, fragments, error) && volume.data() == asset,
                "invalid collapse parameters do not mutate the sampled volume") ||
        !expect(volume.extract_detached(65, 8, 2, fragments, error) && fragments.empty() &&
                    volume.data() == asset,
                "islands below the explicit sample threshold remain static") ||
        !expect(volume.extract_detached(8, 8, 2, fragments, error) && fragments.size() == 8,
                "one detached island splits into eight requested cell groups"))
    {
        return false;
    }
    double total_volume = 0;
    for (const auto& fragment : fragments)
    {
        total_volume += fragment.volume;
        if (!expect(MeshUtils::validate(fragment.mesh) &&
                        Collider::valid_settings(fragment.collision) &&
                        fragment.collision.hulls.size() == 27,
                    "each two-sample cube retains its interior cell and all 26 boundary hulls"))
        {
            return false;
        }
        std::array<double, 3> low{};
        std::array<double, 3> high{};
        low.fill(std::numeric_limits<double>::max());
        high.fill(std::numeric_limits<double>::lowest());
        for (const auto& hull : fragment.collision.hulls)
        {
            for (const auto& vertex : hull->vertices)
            {
                const std::array<double, 3> point{vertex.x(), vertex.y(), vertex.z()};
                for (std::size_t axis = 0; axis < point.size(); ++axis)
                {
                    low[axis] = std::min(low[axis], point[axis]);
                    high[axis] = std::max(high[axis], point[axis]);
                }
            }
        }
        for (std::size_t axis = 0; axis < low.size(); ++axis)
        {
            if (!expect(high[axis] - low[axis] == 2.0 && (low[axis] == 5.5 || low[axis] == 7.5),
                        "collision keeps both half-cell surfaces and the original world bounds"))
            {
                return false;
            }
        }
    }
    std::size_t removed = 0;
    for (std::size_t index = 0; index < asset.density.size(); ++index)
    {
        if (asset.density[index] < 0 && volume.data().density[index] >= 0)
        {
            ++removed;
        }
    }
    if (!expect(removed == 64 && total_volume == 64 &&
                    volume.extract_detached(8, 8, 2, fragments, error) && fragments.empty(),
                "only converted samples leave the source, without duplicate extraction"))
    {
        return false;
    }
    const auto connected = collapse_asset(true);
    return expect(volume.initialize(connected, error) &&
                      volume.extract_detached(8, 8, 2, fragments, error) && fragments.empty() &&
                      volume.data() == connected,
                  "a thin six-connected support preserves the island until it is severed");
}

bool incremental_collapse_contract()
{
    schema::TerrainAssetDataT asset;
    asset.cells_x = asset.cells_y = asset.cells_z = 32;
    asset.chunk_cells = 16;
    asset.density.assign(33 * 33 * 33, 1);
    for (unsigned z = 1; z < 32; ++z)
    {
        for (unsigned y = 1; y <= 10; ++y)
        {
            for (unsigned x = 1; x < 32; ++x)
            {
                // Place the sample beside the top edge: the neighboring normal has x/y
                // components, so changing its x gradient changes the normalized direction.
                // A flat wall would change only gradient magnitude, not its final normal.
                const bool ground = y <= 2;
                const bool wall = x >= 17 && y <= 8 && z >= 6 && z <= 10;
                const bool island = x == 15 && y == 8 && z == 8;
                if (ground || wall || island)
                {
                    asset.density[(z * 33 + y) * 33 + x] = -1;
                }
            }
        }
    }
    TerrainVolume volume;
    TerrainVolume rebuilt;
    schema::MeshDataT before;
    schema::MeshDataT after;
    schema::MeshDataT full;
    std::string error;
    std::vector<plugins::terrain::TerrainFragment> fragments;
    if (!expect(volume.initialize(asset, error) && volume.build_mesh(before, error) &&
                    volume.chunk_count() == 8 &&
                    volume.extract_detached(1, 1, 2, fragments, error) && fragments.size() == 1 &&
                    volume.build_mesh(after, error) && volume.rebuilt_chunks() == 2 &&
                    rebuilt.initialize(volume.data(), error) && rebuilt.build_mesh(full, error) &&
                    triangles(after) == triangles(full),
                "collapse rebuilds only geometry/normal neighbors and matches full regeneration"))
    {
        return false;
    }
    const auto right_side = [](const schema::MeshDataT& mesh)
    {
        std::vector<std::array<float, 6>> result;
        for (const auto& vertex : mesh.vertices)
        {
            if (vertex.x() >= 16)
            {
                result.push_back(
                    {vertex.x(), vertex.y(), vertex.z(), vertex.nx(), vertex.ny(), vertex.nz()});
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    };
    if (!expect(right_side(before) != right_side(after),
                "removing a boundary sample changes the neighboring chunk's normalized normals"))
    {
        return false;
    }
    const auto collision = volume.collision_data();
    return expect(volume.extract_detached(1, 1, 2, fragments, error) && fragments.empty() &&
                      volume.build_mesh(full, error) && volume.rebuilt_chunks() == 0 &&
                      volume.collision_data().parts == collision.parts &&
                      triangles(full) == triangles(after),
                  "empty extraction retains all geometry and collision caches");
}

bool collapse_runtime_contract()
{
    const EngineConfig config;
    test_support::ResourceServices services{};
    auto& resources = services.core;
    WorldSystem world{&services.loader};
    PhysicsSystem physics(&world);
    TerrainSystem terrain(world, physics);
    test_support::ResourceFixture fixture;
    const auto asset = collapse_asset(true);
    if (!expect((resources.add<engine::plugins::terrain::TerrainResourceCodec>() &&
                 services.initialize(config)) &&
                    world.initialize(config) && physics.initialize(config) &&
                    terrain.initialize(config) && add_terrain(fixture, asset) &&
                    services.publish_and_load(fixture.catalog, {{"content", fixture.root}}),
                "initialize the collapse laboratory with a real terrain resource"))
    {
        return false;
    }
    auto* scene = world.create_scene("Terrain collapse");
    auto node = scene ? scene->create_node("Supported island") : engine::Node{};
    auto component = node ? node.add_component<TerrainComponent>() : TerrainComponent{};
    if (!expect(component.valid(), "create terrain collapse component"))
    {
        return false;
    }
    auto settings = test_support::read_settings(component);
    settings.terrain_guid = fixture.catalog.entries.front()->guid;
    if (!expect(component.set_settings(settings) && terrain.tick(0) &&
                    component.collapse_detached(8, 16, 2, 100) && component.fragment_count() == 0 &&
                    component.apply_sphere({7, 4, 7}, 1.8F, false),
                "an intact support does not collapse; digging removes the support"))
    {
        return false;
    }
    ByteBuffer before;
    ByteBuffer after;
    const auto revision = component.revision();
    if (!component.set_visible(false))
    {
        std::abort();
    }
    if (!expect(component.snapshot(before) && !component.collapse_detached(8, 7, 2, 100) &&
                    component.snapshot(after) && before == after &&
                    component.revision() == revision && component.fragment_count() == 0 &&
                    physics.body_count() == 1 && test_support::scene_nodes(*scene).size() == 1,
                "rejected collapse publishes neither holes nor candidate rigid bodies") ||
        !expect(component.collapse_detached(8, 16, 2, 100) && component.fragment_count() == 8 &&
                    physics.body_count() == 9 && !component.snapshot(after) && after.empty() &&
                    !component.restore(before),
                "eight dynamic fragments replace the island and unsupported saves fail explicitly"))
    {
        std::fprintf(stderr, "terrain collapse diagnostic: %s\n", component.last_error().c_str());
        return false;
    }
    schema::SceneDataT saved;
    if (!expect(scene->build_data(saved) && saved.nodes.size() == 1,
                "normal scene saving excludes all transient soil bodies"))
    {
        return false;
    }
    std::uint64_t fragment_id = 0;
    for (const auto& item : test_support::scene_nodes(*scene))
    {
        if (item.transient())
        {
            fragment_id = item.id();
            auto body = item.get_component<RigidBody>();
            if (!expect(body &&
                            test_support::read_settings(body).motion ==
                                schema::BodyMotion::dynamic &&
                            item.get_component<MeshRenderer>() && item.get_component<Collider>() &&
                            !item.get_component<MeshRenderer>().visible_in_hierarchy() &&
                            item.get_component<Collider>().active(),
                        "new soil fragments immediately inherit hidden source state without "
                        "suppressing collision"))
            {
                return false;
            }
        }
    }
    const auto fragments_visible = [&](bool expected)
    {
        const auto nodes = test_support::scene_nodes(*scene);
        return std::all_of(nodes.begin(), nodes.end(),
                           [&](const auto& item)
                           {
                               auto mesh = item.template get_component<MeshRenderer>();
                               return !item.transient() ||
                                      (mesh && mesh.visible_in_hierarchy() == expected);
                           });
    };
    if (!component.set_visible(true))
    {
        std::abort();
    }
    if (!expect(world.set_paused(true) && terrain.tick(2) && fragments_visible(true) &&
                    node.get_component<MeshRenderer>().visible(),
                "paused terrain synchronizes visibility without advancing simulation"))
    {
        return false;
    }
    if (!node.set_visible(false))
    {
        std::abort();
    }
    if (!expect(terrain.tick(2) && fragments_visible(false) && physics.body_count() == 9,
                "hiding the source node while paused hides all independent fragments"))
    {
        return false;
    }
    if (!node.set_visible(true))
    {
        std::abort();
    }
    if (!node.set_enabled(false))
    {
        std::abort();
    }
    if (!expect(terrain.tick(2) && physics.tick(0) &&
                    !scene->find_node(fragment_id).get_component<MeshRenderer>().active() &&
                    !scene->find_node(fragment_id).get_component<Collider>().active(),
                "disabling the source while paused synchronizes fragment activation"))
    {
        return false;
    }
    if (!node.set_enabled(true))
    {
        std::abort();
    }
    if (!expect(terrain.tick(2) && physics.tick(0) && fragments_visible(true) &&
                    physics.body_count() == 9 && world.set_paused(false),
                "reenabling the paused source restores the retained fragments"))
    {
        return false;
    }
    const double initial_y =
        test_support::read_settings(scene->find_node(fragment_id).get_component<Transform>())
            .position.y();
    for (int step = 0; step < 20; ++step)
    {
        if (!physics.tick(1.0 / 60.0))
        {
            return false;
        }
    }
    if (!expect(
            test_support::read_settings(scene->find_node(fragment_id).get_component<Transform>())
                    .position.y() < initial_y - 0.1,
            "detached soil actually falls under Jolt gravity") ||
        !expect(component.apply_sphere({3, 2, 3}, 1, false) &&
                    component.apply_sphere({3, 2, 3}, 1, true) && component.fragment_count() == 8 &&
                    !component.collapse_detached(8, 8, 2, 100) && component.fragment_count() == 8,
                "further brushes preserve moving fragments and respect the total active budget"))
    {
        return false;
    }
    if (!component.set_enabled(false))
    {
        std::abort();
    }
    if (!expect(terrain.tick(0) && physics.tick(0) && physics.body_count() == 0 &&
                    component.fragment_count() == 8 &&
                    !scene->find_node(fragment_id).get_component<MeshRenderer>().enabled(),
                "disabling the terrain disables both source and moving soil"))
    {
        return false;
    }
    if (!component.set_enabled(true))
    {
        std::abort();
    }
    if (!expect(terrain.tick(0) && physics.tick(0) && physics.body_count() == 9 &&
                    component.reset() && component.fragment_count() == 0 &&
                    physics.body_count() == 1 && test_support::scene_nodes(*scene).size() == 1 &&
                    component.snapshot(after),
                "reenabling restores fragments and reset cleans them while restoring source"))
    {
        return false;
    }
    if (!node.set_visible(false))
    {
        std::abort();
    }
    if (!expect(component.apply_sphere({7, 4, 7}, 1.8F, false) &&
                    component.collapse_detached(8, 16, 2, 100) && fragments_visible(false) &&
                    node.remove_component(TerrainComponent::component_type) && terrain.tick(0) &&
                    physics.tick(0) && physics.body_count() == 0 &&
                    test_support::scene_nodes(*scene).size() == 1,
                "removing the source component cleans all fragment nodes and bodies"))
    {
        return false;
    }
    world.destroy_scene();
    terrain.shutdown();
    physics.shutdown();
    world.shutdown();
    services.shutdown();
    return true;
}
bool fragment_retirement_contract()
{
    test_support::ResourceServices services{};
    auto& resources = services.core;
    WorldSystem world{&services.loader};
    PhysicsSystem physics(&world);
    TerrainSystem terrain(world, physics);
    test_support::ResourceFixture fixture;
    if (!expect((resources.add<engine::plugins::terrain::TerrainResourceCodec>() &&
                 services.initialize({})) &&
                    world.initialize({}) && physics.initialize({}) && terrain.initialize({}) &&
                    add_terrain(fixture, collapse_asset(true)) &&
                    services.publish_and_load(fixture.catalog, {{"content", fixture.root}}),
                "initialize fragment retirement with a real terrain resource"))
    {
        return false;
    }
    auto* scene = world.create_scene("Terrain retirement");
    auto first_node = scene ? scene->create_node("First owner") : engine::Node{};
    auto second_node = scene ? scene->create_node("Second owner") : engine::Node{};
    auto first = first_node ? first_node.add_component<TerrainComponent>() : TerrainComponent{};
    auto second = second_node ? second_node.add_component<TerrainComponent>() : TerrainComponent{};
    std::uint32_t retired = 99;
    if (!expect(first && second && !first.retire_fragments(0, 0, retired) && retired == 0,
                "unready retirement fails with a cleared output"))
    {
        return false;
    }
    auto pose = test_support::read_settings(second_node.get_component<Transform>());
    pose.position = {30, 0, 0};
    schema::TerrainDataT description;
    description.terrain_guid = fixture.catalog.entries.front()->guid;
    ByteBuffer before_collapse;
    if (!second_node.get_component<Transform>().set_settings(pose) ||
        !first.set_settings(description) || !second.set_settings(description) ||
        !terrain.synchronize() || !first.apply_sphere({7, 4, 7}, 1.8F, false) ||
        !first.snapshot(before_collapse) || !first.collapse_detached(8, 16, 2, 100))
    {
        return false;
    }
    std::vector<std::uint64_t> first_ids;
    for (const auto& node : test_support::scene_nodes(*scene))
    {
        if (node.transient())
        {
            first_ids.push_back(node.id());
        }
    }
    std::sort(first_ids.begin(), first_ids.end());
    if (!terrain.tick(0.5) || !second.apply_sphere({7, 4, 7}, 1.8F, false) ||
        !second.collapse_detached(8, 16, 2, 100) || !terrain.tick(0.5) ||
        !expect(first_ids.size() == 8 && second.fragment_count() == 8 && physics.body_count() == 18,
                "two independent owners publish eight soil fragments at distinct ages"))
    {
        return false;
    }
    const auto revision = first.revision();
    const auto source_triangles = first.triangle_count();
    const auto rebuilt_chunks = first.rebuilt_chunks();
    auto source_mesh = first_node.get_component<MeshRenderer>().mesh_data();
    auto retained = scene->find_node(first_ids.back());
    auto retained_body = retained.get_component<RigidBody>();
    const auto retained_pose = test_support::read_settings(retained.get_component<Transform>());
    if (!retained_body.set_linear_velocity({1, 2, 3}))
    {
        return false;
    }
    std::vector<PhysicsObject> hits;
    if (!physics.overlap_box({8, 8, 8}, {16, 16, 16}, {}, hits))
    {
        return false;
    }
    const auto found = std::find_if(hits.begin(), hits.end(), [&](const auto& hit)
                                    { return hit.node_id == first_ids.front(); });
    if (!expect(found != hits.end(), "fragment collision is present before retirement"))
    {
        return false;
    }
    const auto old_handle = *found;
    if (!expect(
            !first.retire_fragments(0, -1, retired) && retired == 0 &&
                !first.retire_fragments(0, std::numeric_limits<double>::infinity(), retired) &&
                retired == 0 && first.revision() == revision && first.fragment_count() == 8 &&
                first.retire_fragments(6, 0, retired) && retired == 2 &&
                !scene->find_node(first_ids[0]) && !scene->find_node(first_ids[1]) &&
                scene->find_node(first_ids.back()) == retained &&
                test_support::read_settings(retained.get_component<Transform>()) == retained_pose &&
                retained_body.linear_velocity() == Vec3(1, 2, 3) && first.fragment_count() == 6 &&
                second.fragment_count() == 8 && physics.body_count() == 16 &&
                !physics.is_object_valid(old_handle) && first.revision() == revision + 1 &&
                first.triangle_count() == source_triangles &&
                first.rebuilt_chunks() == rebuilt_chunks &&
                first_node.get_component<MeshRenderer>().mesh_data() == source_mesh,
            "count retirement removes stable oldest IDs without rebuilding source or survivors"))
    {
        return false;
    }
    if (!expect(physics.overlap_box({8, 8, 8}, {16, 16, 16}, {}, hits) &&
                    std::none_of(hits.begin(), hits.end(), [&](const auto& hit)
                                 { return hit.node_id == old_handle.node_id; }),
                "retired soil disappears from collision queries immediately"))
    {
        return false;
    }
    if (!expect(
            world.set_paused(true) && terrain.tick(100) && terrain.synchronize() &&
                first.retire_fragments(128, 1.5, retired) && retired == 0 &&
                first.revision() == revision + 1 && world.set_paused(false) && terrain.tick(0.5) &&
                first.retire_fragments(128, 1.5, retired) && retired == 6 &&
                second.retire_fragments(128, 1.5, retired) && retired == 0 &&
                second.fragment_count() == 8 && physics.body_count() == 10,
            "simulation age excludes pause and editor synchronization, and remains owner-local"))
    {
        return false;
    }
    ByteBuffer saved;
    const auto empty_revision = first.revision();
    if (!expect(
            first.retire_fragments(0, 0, retired) && retired == 0 &&
                first.revision() == empty_revision && !first.snapshot(saved) && saved.empty() &&
                !first.restore(before_collapse) && first.apply_sphere({3, 2, 3}, 1, false) &&
                first.collapse_detached(8, 16, 2, 100) && first.fragment_count() == 0 &&
                !first.snapshot(saved),
            "empty retirement preserves detached-state restriction and never refills source soil"))
    {
        return false;
    }
    Node second_fragment = {};
    for (const auto& node : test_support::scene_nodes(*scene))
    {
        if (node.transient())
        {
            second_fragment = node;
            break;
        }
    }
    auto child = scene->create_node("Project attachment");
    const auto second_revision = second.revision();
    if (!expect(second_fragment && child && child.set_parent(second_fragment) &&
                    !second.retire_fragments(0, 0, retired) && retired == 0 &&
                    second.revision() == second_revision && second.fragment_count() == 8 &&
                    scene->destroy_node(child.id()),
                "foreign child nodes reject the entire retirement before mutation"))
    {
        return false;
    }
    if (!expect(
            second_fragment.remove_component(Collider::component_type) &&
                second_fragment.add_component<Collider>() &&
                !second.retire_fragments(0, 0, retired) && retired == 0 &&
                second.revision() == second_revision && second.fragment_count() == 8 &&
                physics.body_count() == 10 && second.reset() && second.fragment_count() == 0 &&
                first.reset() && first.snapshot(saved) &&
                first.apply_sphere({7, 4, 7}, 1.8F, false) &&
                first.collapse_detached(8, 16, 2, 100) && first.fragment_count() == 8 &&
                first.retire_fragments(0, 0, retired) && retired == 8 &&
                first.fragment_count() == 0 && physics.body_count() == 2,
            "replaced generated components reject atomically; reset explicitly restores source"))
    {
        return false;
    }
    return true;
}

bool asynchronous_brush_contract()
{
    ThreadSystem threads({.worker_limit = 1});
    test_support::ResourceServices services{&threads};
    auto& resources = services.core;
    WorldSystem world{&services.loader};
    PhysicsSystem physics(&world);

    TaskSystem tasks(threads, {.name = "Terrain asynchronous tests", .max_concurrency = 1});
    TerrainSystem terrain(world, physics, &tasks, 2, 1);
    test_support::ResourceFixture fixture;
    schema::TerrainAssetDataT asset;
    auto settings = generator();
    settings.cells_x = settings.cells_y = settings.cells_z = 16;
    settings.origin = {-8, -8, -8};
    std::string error;
    if (!expect((threads.initialize({}) &&
                 (resources.add<engine::plugins::terrain::TerrainResourceCodec>() &&
                  services.initialize({}))) &&
                    world.initialize({}) && physics.initialize({}) && tasks.initialize({}) &&
                    terrain.initialize({}) && TerrainVolume::generate(settings, asset, error) &&
                    add_terrain(fixture, asset) &&
                    services.publish_and_load(fixture.catalog, {{"content", fixture.root}}),
                "initialize asynchronous terrain with a bounded shared worker"))
    {
        return false;
    }
    auto* scene = world.create_scene("Async terrain");
    auto node = scene ? scene->create_node("Volume") : engine::Node{};
    auto component = node ? node.add_component<TerrainComponent>() : TerrainComponent{};
    schema::TerrainDataT description;
    description.terrain_guid = fixture.catalog.entries.front()->guid;
    if (!component || !component.set_settings(description) || !terrain.synchronize())
    {
        return false;
    }

    // Holding the sole worker proves request + synchronize do not wait on preparation.
    std::binary_semaphore gate(0);
    auto occupied = tasks.submit("Hold terrain worker",
                                 [&]
                                 {
                                     gate.acquire();
                                     return true;
                                 });
    if (!occupied.valid())
    {
        return false;
    }
    std::vector<bool> results;
    std::vector<bool> retirement_allowed;
    bool observe_retirement = true;
    const auto completed = [&](bool success)
    {
        results.push_back(success);
        if (observe_retirement)
        {
            std::uint32_t retired = 99;
            const bool allowed = component.retire_fragments(128, 0, retired);
            retirement_allowed.push_back(allowed && retired == 0);
        }
    };
    const auto before = component.revision();
    std::uint32_t retired = 99;
    const bool queued = terrain.request_apply_sphere(component, {-2, -1, 0}, 2, false, completed) &&
                        terrain.request_apply_sphere(component, {2, -1, 0}, 2, false, completed) &&
                        !terrain.request_apply_sphere(component, {}, 1, true, completed) &&
                        !component.retire_fragments(0, 0, retired) && retired == 0 &&
                        terrain.synchronize() && results.empty() &&
                        component.revision() == before &&
                        !component.retire_fragments(0, 0, retired) && retired == 0;
    gate.release();
    if (!expect(occupied.get() && queued && tasks.wait() && world.set_paused(true) &&
                    terrain.tick(1) && terrain.synchronize() && results.empty() &&
                    component.revision() == before && world.set_paused(false) &&
                    terrain.synchronize() && results == std::vector<bool>{true} &&
                    component.revision() == before + 1 && tasks.wait() && terrain.synchronize() &&
                    results == std::vector<bool>({true, true}) &&
                    component.revision() == before + 2 &&
                    retirement_allowed == std::vector<bool>({false, true}),
                "queued edits stay ordered; retirement waits through the last owner completion"))
    {
        return false;
    }
    observe_retirement = false;
    TerrainVolume reference;
    ByteBuffer bytes;
    if (!reference.initialize(asset, error) ||
        !reference.apply_sphere({-2, -1, 0}, 2, false, error) ||
        !reference.apply_sphere({2, -1, 0}, 2, false, error) || !component.snapshot(bytes))
    {
        return false;
    }
    auto saved =
        schema::UnPackTerrainStateData(reinterpret_cast<const std::uint8_t*>(bytes.data()));
    if (!expect(saved->volume->density == reference.data().density,
                "successive candidates use the preceding committed terrain"))
    {
        return false;
    }

    results.clear();
    if (!expect(terrain.request_apply_sphere(component, {}, 3, false, completed) &&
                    terrain.synchronize() && tasks.wait() && component.reset() &&
                    terrain.synchronize() && results == std::vector<bool>{false} &&
                    component.snapshot(bytes),
                "synchronous reset invalidates an already prepared asynchronous candidate"))
    {
        return false;
    }
    saved = schema::UnPackTerrainStateData(reinterpret_cast<const std::uint8_t*>(bytes.data()));
    if (!expect(saved->volume->density == asset.density, "stale edit cannot overwrite reset"))
    {
        return false;
    }

    auto replacement_node = scene->create_node("Replacement identity");
    auto original = replacement_node.add_component<TerrainComponent>();
    results.clear();
    if (!original || !original.set_settings(description) || !terrain.synchronize())
    {
        return false;
    }
    const auto original_revision = original.revision();
    if (!terrain.request_apply_sphere(original, {}, 2, false, completed) ||
        !terrain.synchronize() || !tasks.wait() ||
        !replacement_node.remove_component(TerrainComponent::component_type))
    {
        return false;
    }
    auto replacement = replacement_node.add_component<TerrainComponent>();
    if (!expect(replacement && replacement.set_settings(description) && terrain.synchronize() &&
                    replacement.revision() == original_revision &&
                    results == std::vector<bool>{false},
                "same node and revision cannot revive a removed component request"))
    {
        return false;
    }

    results.clear();
    if (!terrain.request_apply_sphere(replacement, {}, 2, false, completed) ||
        !terrain.synchronize())
    {
        return false;
    }
    world.destroy_scene();
    if (!expect(world.create_scene("Other scene") && tasks.wait() && terrain.synchronize() &&
                    results == std::vector<bool>{false},
                "scene replacement discards the old candidate and completes cancellation"))
    {
        return false;
    }
    node = world.scene()->create_node("Shutdown");
    component = node.add_component<TerrainComponent>();
    results.clear();
    if (!component || !component.set_settings(description) || !terrain.synchronize() ||
        !terrain.request_apply_sphere(component, {}, 2, false, completed) || !terrain.synchronize())
    {
        return false;
    }
    terrain.shutdown();
    return expect(tasks.wait() && results.empty(),
                  "shutdown drops callbacks while worker-owned candidates finish safely");
}

bool asynchronous_brush_fairness(std::size_t concurrency)
{
    ThreadSystem threads({.worker_limit = concurrency});
    test_support::ResourceServices services{&threads};
    auto& resources = services.core;
    WorldSystem world{&services.loader};
    PhysicsSystem physics(&world);

    TaskSystem tasks(threads, {.name = "Terrain fairness", .max_concurrency = concurrency});
    TerrainSystem terrain(world, physics, &tasks, 4, 1);
    test_support::ResourceFixture files;
    schema::TerrainAssetDataT asset;
    auto settings = generator();
    settings.cells_x = settings.cells_y = settings.cells_z = 16;
    settings.origin = {-8, -8, -8};
    std::string error;
    if (!(threads.initialize({}) &&
          (resources.add<engine::plugins::terrain::TerrainResourceCodec>() &&
           services.initialize({}))) ||
        !world.initialize({}) || !physics.initialize({}) || !tasks.initialize({}) ||
        !terrain.initialize({}) || !TerrainVolume::generate(settings, asset, error) ||
        !add_terrain(files, asset) ||
        !services.publish_and_load(files.catalog, {{"content", files.root}}))
    {
        return false;
    }
    auto* scene = world.create_scene("Fair terrain edits");
    auto low_node = scene ? scene->create_node("Frequent edits") : engine::Node{};
    auto high_node = scene ? scene->create_node("Occasional edit") : engine::Node{};
    auto low = low_node ? low_node.add_component<TerrainComponent>() : TerrainComponent{};
    auto high = high_node ? high_node.add_component<TerrainComponent>() : TerrainComponent{};
    schema::TerrainDataT description;
    description.terrain_guid = files.catalog.entries.front()->guid;
    if (!low || !high || !low.set_settings(description) || !high.set_settings(description) ||
        !terrain.synchronize())
    {
        return false;
    }
    bool high_finished = false;
    bool succeeded = true;
    unsigned low_completions = 0;
    std::function<void(bool)> refill;
    refill = [&](bool success)
    {
        succeeded = success && succeeded;
        ++low_completions;
        if (!high_finished)
        {
            succeeded = terrain.request_apply_sphere(low, {}, 1, false, refill) && succeeded;
        }
    };
    if (!terrain.request_apply_sphere(low, {}, 1, false, refill) ||
        !terrain.request_apply_sphere(low, {}, 1, false, refill) ||
        !terrain.request_apply_sphere(high, {}, 1, false,
                                      [&](bool success)
                                      {
                                          succeeded = success && succeeded;
                                          high_finished = true;
                                      }) ||
        !terrain.synchronize())
    {
        return false;
    }
    // One worker exercises dispatch fairness; two ready candidates exercise publish fairness.
    for (unsigned frame = 0; frame < 4 && !high_finished; ++frame)
    {
        if (!tasks.wait() || !terrain.synchronize())
        {
            return false;
        }
    }
    const bool passed = expect(high_finished && succeeded && low_completions > 0,
                               "higher node ID finishes despite continuous lower-ID refill");
    terrain.shutdown();
    return tasks.wait() && passed;
}

bool persistent_terrain_contract()
{
    ThreadSystem threads({.worker_limit = 1});
    test_support::ResourceServices services{&threads};
    auto& resources = services.core;
    WorldSystem world{&services.loader};
    PhysicsSystem physics(&world);

    TaskSystem tasks(threads, {.name = "Persistent terrain", .max_concurrency = 1});
    TerrainSystem terrain(world, physics, &tasks);
    test_support::ResourceFixture fixture;
    if (!(threads.initialize({}) &&
          (resources.add<engine::plugins::terrain::TerrainResourceCodec>() &&
           services.initialize({}))) ||
        !world.initialize({}) || !physics.initialize({}) || !tasks.initialize({}) ||
        !terrain.initialize({}) || !add_terrain(fixture, collapse_asset(true)) ||
        !services.publish_and_load(fixture.catalog, {{"content", fixture.root}}))
    {
        return false;
    }
    auto* scene = world.create_scene("Retained terrain");
    auto node = scene ? scene->create_node("Source") : engine::Node{};
    auto component = node ? node.add_component<TerrainComponent>() : TerrainComponent{};
    schema::TerrainDataT description;
    description.terrain_guid = fixture.catalog.entries.front()->guid;
    if (!component || !node.set_persistent(true) || !component.set_settings(description) ||
        !terrain.synchronize() || !component.apply_sphere({7, 4, 7}, 1.8F, false) ||
        !component.collapse_detached(8, 16, 2, 100) || component.fragment_count() != 8)
    {
        return false;
    }
    const auto revision = component.revision();
    auto renderer = node.get_component<MeshRenderer>();
    auto collider = node.get_component<Collider>();
    world.unload_scene();
    if (!expect(terrain.synchronize() && physics.tick(0) && component.ready() &&
                    component.revision() == revision && component.fragment_count() == 0 &&
                    node.get_component<MeshRenderer>() == renderer &&
                    node.get_component<Collider>() == collider && physics.body_count() == 1,
                "ordinary soil roots unload while the retained source keeps mesh, volume and "
                "collision"))
    {
        return false;
    }
    bool completed = false;
    bool success = false;
    if (!terrain.request_apply_sphere(component, {3, 2, 3}, 1, false,
                                      [&](bool result)
                                      {
                                          completed = true;
                                          success = result;
                                      }) ||
        !terrain.synchronize())
    {
        return false;
    }
    world.unload_scene();
    if (!world.create_scene("Next terrain level") || !tasks.wait() || !terrain.synchronize())
    {
        return false;
    }
    return expect(
        completed && success && component.revision() > revision &&
            world.find_node(node.id()) == node,
        "queued terrain candidate publishes to the retained source across scene replacement");
}

} // namespace

int main()
{
    return persistent_terrain_contract() && volume_contract() && runtime_contract() &&
                   subsystem_contract() && island_contract() && incremental_collapse_contract() &&
                   collapse_runtime_contract() && fragment_retirement_contract() &&
                   asynchronous_brush_contract() && asynchronous_brush_fairness(1) &&
                   asynchronous_brush_fairness(2)
               ? 0
               : 1;
}
