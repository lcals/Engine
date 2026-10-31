#include "plugins/runtime/terrain/terrain_component.hpp"
#include "plugins/runtime/terrain/terrain_system.hpp"
#include "plugins/runtime/terrain/terrain_volume.hpp"
#include "resource_fixture.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/schema/terrain_state_generated.h"
#include "runtime/utils/mesh_utils.hpp"
#include "runtime/world/world_system.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <memory>
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
    entry->path = "project/test.terrain";
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
    ResourceSystem resources;
    WorldSystem world;
    PhysicsSystem physics(&world);
    TerrainSystem terrain(world, physics);
    test_support::ResourceFixture fixture;
    schema::TerrainAssetDataT asset;
    std::string error;
    if (!expect(resources.initialize(config) && world.initialize(config) &&
                    physics.initialize(config) && terrain.initialize(config) &&
                    TerrainVolume::generate(generator(), asset, error) &&
                    add_terrain(fixture, asset) && fixture.publish(resources),
                "initialize terrain plugin with real GUID resource loading"))
    {
        return false;
    }
    auto* scene = world.create_scene("Terrain contracts");
    auto* node = scene ? scene->create_node("Volume") : nullptr;
    auto* component = node ? node->add_component<TerrainComponent>() : nullptr;
    if (!expect(component != nullptr && !component->ready(),
                "empty component has no generated output"))
    {
        return false;
    }
    auto settings = component->settings();
    settings.terrain_guid = fixture.catalog.entries.front()->guid;
    if (!expect(component->set_settings(settings) && terrain.tick(0) && component->ready() &&
                    physics.tick(0) && physics.body_count() == 1,
                "configured component creates rendering and collision through the plugin"))
    {
        std::fprintf(stderr, "terrain diagnostic: %s\n", component->last_error().c_str());
        return false;
    }
    auto cast = [&](double x, bool expected)
    {
        float fraction = 0;
        return physics.cast_sphere({x, 4, 0.25}, {0, -12, 0}, 0.1F, fraction) &&
               (expected ? fraction < 1.0F : fraction == 1.0F);
    };
    auto* renderer = node->get_component<MeshRenderer>();
    auto* collider = node->get_component<Collider>();
    schema::SceneDataT scene_data;
    if (!expect(renderer && collider && renderer->transient() && collider->transient() &&
                    scene->build_data(scene_data) && scene_data.nodes.size() == 1 &&
                    scene_data.nodes.front()->components.size() == 2 && cast(0.25, true),
                "scene snapshot retains only Transform and Terrain, excluding generated outputs"))
    {
        return false;
    }
    const auto initial_revision = component->revision();
    ByteBuffer saved;
    const auto state_path = fixture.root / "state" / "dig.bin";
    if (!expect(component->apply_sphere({0, -3, 0}, 8, false) &&
                    component->revision() > initial_revision && cast(0.25, false) &&
                    cast(12, true) && component->snapshot(saved) &&
                    component->save_state(state_path),
                "dig opens collision immediately while retaining the surrounding surface") ||
        !expect(component->reset() && cast(0.25, true) && component->load_state(state_path) &&
                    cast(0.25, false),
                "file save and restore recover modified volume after reset"))
    {
        return false;
    }
    const auto revision = component->revision();
    ByteBuffer after;
    auto corrupted = saved;
    corrupted[4] = std::byte{0};
    if (!expect(!component->restore(corrupted) && component->revision() == revision &&
                    component->snapshot(after) && after == saved && cast(0.25, false),
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
    if (!expect(!component->restore(wrong_base) && component->revision() == revision &&
                    component->snapshot(after) && after == saved,
                "state from another base resource is rejected without mutation"))
    {
        return false;
    }
    auto* transform = node->get_component<Transform>();
    const auto original_pose = transform->settings();
    auto invalid_pose = original_pose;
    invalid_pose.scale = {-1, 1, 1};
    const auto mesh_before = triangles(*renderer->mesh_data());
    if (!expect(
            transform->set_settings(invalid_pose) &&
                !component->apply_sphere({0, -3, 0}, 3, true) &&
                component->revision() == revision && component->snapshot(after) && after == saved &&
                triangles(*renderer->mesh_data()) == mesh_before &&
                transform->set_settings(original_pose) && cast(0.25, false) && cast(12, true),
            "failed physics commit rolls back volume and rendering and preserves old collision"))
    {
        return false;
    }
    auto no_collision = settings;
    no_collision.collision = false;
    if (!expect(
            component->set_settings(no_collision) && !collider->enabled() &&
                physics.body_count() == 0 && cast(12, false) &&
                transform->set_settings(invalid_pose) && !component->set_settings(settings) &&
                component->settings() == no_collision && !collider->enabled() &&
                physics.body_count() == 0 && transform->set_settings(original_pose) &&
                component->set_settings(settings) && collider->enabled() && cast(12, true) &&
                component->snapshot(after) && after == saved,
            "collision changes publish immediately and failed reenabling restores disabled state"))
    {
        return false;
    }
    if (!expect(component->apply_sphere({0, 0, 0}, 100, false) &&
                    component->triangle_count() == 0 && !renderer->visible() &&
                    !collider->enabled() && physics.body_count() == 0 && cast(0.25, false) &&
                    component->apply_sphere({0, -4, 0}, 4, true) &&
                    component->triangle_count() > 0 && renderer->visible() && collider->enabled() &&
                    physics.body_count() == 1 && cast(0.25, true) && component->restore(saved) &&
                    cast(0.25, false) && cast(12, true),
                "filling an entirely empty terrain restores collision without a physics tick"))
    {
        return false;
    }
    component->set_enabled(false);
    if (!expect(terrain.tick(0) && physics.tick(0) && !renderer->enabled() &&
                    !collider->enabled() && physics.body_count() == 0 && cast(12, false),
                "disabled terrain removes both render output and collision"))
    {
        return false;
    }
    component->set_enabled(true);
    if (!expect(terrain.tick(0) && physics.tick(0) && cast(12, true),
                "reenabling terrain restores its modified surface"))
    {
        return false;
    }
    auto cleared = settings;
    cleared.terrain_guid.clear();
    if (!expect(
            component->set_settings(cleared) && !component->ready() &&
                component->chunk_count() == 0 && component->triangle_count() == 0 &&
                terrain.tick(0) && physics.tick(0) && physics.body_count() == 0 &&
                node->get_component<MeshRenderer>() == nullptr &&
                node->get_component<Collider>() == nullptr && component->triangle_count() == 0 &&
                component->set_settings(settings) && component->restore(saved) && cast(12, true),
            "clearing the resource resets statistics and the component can be configured again") ||
        !expect(node->remove_component(TerrainComponent::component_type) && terrain.tick(0) &&
                    physics.tick(0) && node->get_component<MeshRenderer>() == nullptr &&
                    node->get_component<Collider>() == nullptr && physics.body_count() == 0 &&
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
    resources.shutdown();
    return true;
}

bool subsystem_contract()
{
    const EngineConfig config;
    ResourceSystem resources;
    WorldSystem world;
    PhysicsSystem physics(&world);
    auto owned = std::make_unique<TerrainSystem>(world, physics);
    auto* terrain = owned.get();
    std::unique_ptr<System> child = std::move(owned);
    test_support::ResourceFixture fixture;
    schema::TerrainAssetDataT asset;
    std::string error;
    if (!expect(!terrain->synchronize() && world.add_subsystem(child) &&
                    resources.initialize(config) && world.initialize(config) &&
                    physics.initialize(config) &&
                    TerrainVolume::generate(generator(), asset, error) &&
                    add_terrain(fixture, asset) && fixture.publish(resources),
                "host owns the terrain subsystem and initializes its shared dependencies"))
    {
        return false;
    }
    auto* scene = world.create_scene("Preview terrain");
    auto* node = scene ? scene->create_node("Volume") : nullptr;
    auto* component = node ? node->add_component<TerrainComponent>() : nullptr;
    if (!expect(component != nullptr, "create a pending terrain in the preview scene"))
    {
        return false;
    }
    auto settings = component->settings();
    settings.terrain_guid = fixture.catalog.entries.front()->guid;
    float fraction = 1;
    if (!expect(component->set_settings(settings) && !terrain->tick(0) && !component->ready() &&
                    terrain->synchronize() && component->ready() && physics.body_count() == 1 &&
                    physics.cast_sphere({0.25, 4, 0.25}, {0, -12, 0}, 0.1F, fraction) &&
                    fraction < 1,
                "preview explicitly synchronizes terrain while direct subsystem ticking stays "
                "forbidden"))
    {
        return false;
    }
    const auto revision = component->revision();
    if (!expect(world.tick(0) && component->revision() == revision,
                "normal parent ticking reuses the synchronized terrain without rebuilding"))
    {
        return false;
    }
    world.shutdown();
    const bool stopped = !terrain->synchronize();
    physics.shutdown();
    resources.shutdown();
    return expect(stopped, "explicit synchronization cannot run after subsystem shutdown");
}
} // namespace

int main()
{
    return volume_contract() && runtime_contract() && subsystem_contract() ? 0 : 1;
}
