#include "plugins/runtime/destruction/destruction_component.hpp"
#include "plugins/runtime/destruction/destruction_system.hpp"
#include "plugins/runtime/script/script_system.hpp"
#include "plugins/runtime/terrain/terrain_component.hpp"
#include "plugins/runtime/terrain/terrain_system.hpp"
#include "resource_fixture.hpp"
#include "runtime/app/app.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"
#include "runtime/world/world_system.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace
{
using namespace engine;
using plugins::destruction::DestructionComponent;
using plugins::destruction::DestructionSystem;
using plugins::terrain::TerrainComponent;
using plugins::terrain::TerrainSystem;

constexpr std::string_view terrain_guid = "50000000-0000-4000-8000-000000000021";
constexpr std::string_view destruction_guid = "50000000-0000-4000-8000-000000000022";
constexpr std::string_view source = R"js(
import { Gameplay, Scene, TerrainComponent, DestructionComponent } from 'engine';
function check(value, message) { if (!value) throw new Error(message); }
export default class extends Gameplay {
    phase = 0;
    callbacks = 0;
    cancelled = 0;
    OnInitialize() {
        this.scene = Scene.Create('Async edits');
        this.terrain = this.scene.CreateNode('Terrain').AddComponent(TerrainComponent);
        this.building = this.scene.CreateNode('Building').AddComponent(DestructionComponent);
        check(this.terrain && this.building, 'create native components');
        const unexpected = () => { throw new Error('rejected request delivered a callback'); };
        check(!this.terrain.DigSphereAsync(0,0,0,1,unexpected) &&
              !this.building.DamageSphereAsync(0,0,0,2,.25,0,unexpected),
              'not-ready components accepted asynchronous work');
        return true;
    }
    Update() {
        if (this.phase === 0) {
            check(this.terrain.ready && this.building.ready, 'native fixtures not ready');
            const unexpected = () => { throw new Error('invalid request delivered callback'); };
            check(!this.terrain.DigSphereAsync(0,0,0,0,unexpected) &&
                  !this.terrain.FillSphereAsync(NaN,0,0,1,unexpected) &&
                  !this.terrain.DigSphereAsync(0,0,0,1,null) &&
                  !this.building.DamageSphereAsync(0,0,0,2,-1,0,unexpected) &&
                  !this.building.DamageSphereAsync(0,0,0,2,1,0,null),
                  'invalid parameters or callback accepted');
            this.revision = this.terrain.revision;
            const done = success => {
                check(success && this.phase === 1, 'callback failed or ran during submission');
                ++this.callbacks;
            };
            check(this.terrain.DigSphereAsync(0,-1,0,2,done) &&
                  this.terrain.FillSphereAsync(0,-1,0,1,done) &&
                  this.building.DamageSphereAsync(.5,0,0,2,.25,0,done),
                  'valid request rejected');
            check(this.callbacks === 0 && this.terrain.revision === this.revision,
                  'submission synchronously published data or called a delegate');
            this.phase = 1;
            return;
        }
        if (this.phase === 1 && this.callbacks === 3) {
            check(this.terrain.revision > this.revision, 'successful callback preceded publication');
            check(this.terrain.DigSphereAsync(0,-1,0,2,success => {
                check(!success && this.phase === 2, 'reset did not cancel the queued brush');
                ++this.cancelled;
            }) && this.building.DamageSphereAsync(.5,0,0,2,.25,0,success => {
                check(!success && this.phase === 2, 'reset did not cancel queued damage');
                ++this.cancelled;
            }) && this.terrain.Reset() && this.building.Reset(), 'queue and reset');
            this.phase = 2;
            return;
        }
        if (this.phase === 2 && this.cancelled === 2) {
            const stale = () => { throw new Error('old scene callback escaped into replacement'); };
            check(this.terrain.DigSphereAsync(0,-1,0,2,stale) &&
                  this.building.DamageSphereAsync(.5,0,0,2,.25,0,stale), 'queue before scene change');
            check(this.scene.Destroy(), 'destroy previous scene');
            this.scene = Scene.Create('Complete');
            check(this.scene && !this.terrain.DigSphereAsync(0,0,0,1,stale) &&
                  !this.building.DamageSphereAsync(0,0,0,2,.25,0,stale),
                  'old scene handles remained usable');
            this.phase = 3;
        }
        if (this.phase === 3)
            check(this.callbacks === 3 && this.cancelled === 2, 'duplicate delegate');
    }
}
)js";

constexpr std::string_view native_replacement_source = R"js(
import { Gameplay, Scene, TerrainComponent, DestructionComponent } from 'engine';
export default class extends Gameplay {
    submitted = false;
    OnInitialize() {
        this.scene = Scene.Create('Native replacement source');
        this.terrain = this.scene.CreateNode('Terrain').AddComponent(TerrainComponent);
        this.building = this.scene.CreateNode('Building').AddComponent(DestructionComponent);
        return !!this.terrain && !!this.building;
    }
    Update() {
        if (!this.submitted) {
            const stale = () => { throw new Error('native scene replacement delivered an old delegate'); };
            if (!this.terrain.DigSphereAsync(0,-1,0,2,stale) ||
                !this.building.DamageSphereAsync(.5,0,0,2,.25,0,stale))
                throw new Error('native replacement requests were rejected');
            this.submitted = true;
        } else if (this.scene.IsValid()) {
            throw new Error('native replacement left the old scene active');
        }
    }
}
)js";

bool add_resource(test_support::ResourceFixture& files, std::string_view guid,
                  std::string_view path, schema::AssetKind kind, ByteView payload)
{
    auto entry = std::make_unique<schema::AssetEntryT>();
    entry->guid = guid;
    entry->path = path;
    entry->kind = kind;
    if (kind == schema::AssetKind::script)
    {
        entry->module_path = path;
    }
    ByteBuffer bytes;
    if (!ResourceSystem::encode_resource(entry->guid, kind, payload, bytes, entry->header_size))
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
    std::ofstream stream(files.root / entry->location->path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    stream.close();
    if (!stream)
    {
        return false;
    }
    files.catalog.entries.push_back(std::move(entry));
    return true;
}

bool add_fixtures(test_support::ResourceFixture& files)
{
    schema::TerrainGeneratorDataT generator;
    generator.cells_x = generator.cells_y = generator.cells_z = 16;
    generator.chunk_cells = 16;
    generator.origin = {-8, -8, -8};
    generator.amplitude = 0;
    generator.height = 0;
    schema::TerrainAssetDataT volume;
    std::string error;
    if (!plugins::terrain::TerrainVolume::generate(generator, volume, error))
    {
        return false;
    }
    flatbuffers::FlatBufferBuilder terrain;
    schema::FinishTerrainAssetDataBuffer(terrain, schema::TerrainAssetData::Pack(terrain, &volume));

    schema::DestructionAssetDataT structure;
    for (std::uint32_t index = 0; index < 2; ++index)
    {
        auto chunk = std::make_unique<schema::DestructionChunkDataT>();
        chunk->centroid = {static_cast<double>(index), 0, 0};
        chunk->anchored = true;
        chunk->mesh = std::make_unique<schema::MeshDataT>();
        for (const float z : {-0.5F, 0.5F})
        {
            for (const float y : {-0.5F, 0.5F})
            {
                for (const float x : {-0.5F, 0.5F})
                {
                    chunk->mesh->vertices.emplace_back(static_cast<float>(index) + x, y, z, 0, 1,
                                                       0);
                }
            }
        }
        chunk->mesh->indices = {0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4,
                                2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5};
        structure.chunks.push_back(std::move(chunk));
    }
    auto bond = std::make_unique<schema::DestructionBondDataT>();
    bond->chunk_a = 0;
    bond->chunk_b = 1;
    bond->centroid = {.5, 0, 0};
    bond->normal = {1, 0, 0};
    structure.bonds.push_back(std::move(bond));
    flatbuffers::FlatBufferBuilder destruction;
    schema::FinishDestructionAssetDataBuffer(
        destruction, schema::DestructionAssetData::Pack(destruction, &structure));
    return add_resource(files, terrain_guid, "test.terrain", schema::AssetKind::terrain,
                        {reinterpret_cast<const std::byte*>(terrain.GetBufferPointer()),
                         terrain.GetSize()}) &&
           add_resource(files, destruction_guid, "test.destruction", schema::AssetKind::destruction,
                        {reinterpret_cast<const std::byte*>(destruction.GetBufferPointer()),
                         destruction.GetSize()}) &&
           add_resource(files, "50000000-0000-4000-8000-000000000023", "scripts/game.js",
                        schema::AssetKind::script,
                        {reinterpret_cast<const std::byte*>(source.data()), source.size()}) &&
           add_resource(files, "50000000-0000-4000-8000-000000000024", "scripts/native-replace.js",
                        schema::AssetKind::script,
                        {reinterpret_cast<const std::byte*>(native_replacement_source.data()),
                         native_replacement_source.size()});
}

bool prepare_components(WorldSystem& world, TerrainSystem& terrain, DestructionSystem& destruction)
{
    auto* scene = world.scene();
    Node* ground = nullptr;
    Node* building = nullptr;
    if (scene)
    {
        for (const auto& node : scene->nodes())
        {
            if (node->name() == "Terrain")
            {
                ground = node.get();
            }
            if (node->name() == "Building")
            {
                building = node.get();
            }
        }
    }
    auto* terrain_component = ground ? ground->get_component<TerrainComponent>() : nullptr;
    auto* destruction_component =
        building ? building->get_component<DestructionComponent>() : nullptr;
    schema::TerrainDataT terrain_settings;
    terrain_settings.terrain_guid = terrain_guid;
    schema::DestructionDataT destruction_settings;
    destruction_settings.destruction_guid = destruction_guid;
    return terrain_component && destruction_component &&
           terrain_component->set_settings(terrain_settings) &&
           destruction_component->set_settings(destruction_settings) && terrain.synchronize() &&
           destruction.synchronize();
}

bool run(test_support::ResourceFixture& files)
{
    ResourceSystem resources;
    WorldSystem world;
    PhysicsSystem physics(&world);
    ThreadSystem threads({.worker_limit = 2});
    TaskSystem tasks(threads, {.name = "Async script tests", .max_concurrency = 2});
    TerrainSystem terrain(world, physics, &tasks);
    DestructionSystem destruction(world, physics, &tasks);
    plugins::script::ScriptSystem script(&world, nullptr, nullptr, &physics);
    const auto expect = [&](bool condition, const char* message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "terrain/destruction script: %s (%s)\n", message,
                         script.status().last_error.c_str());
        }
        return condition;
    };
    if (!expect(resources.initialize({}) && world.initialize({}) && physics.initialize({}) &&
                    threads.initialize({}) && tasks.initialize({}) && terrain.initialize({}) &&
                    destruction.initialize({}) && script.initialize({}) && add_fixtures(files) &&
                    files.publish(resources) && script.start_gameplay("scripts/game.js"),
                "initialize the real script backend and native edit systems"))
    {
        return false;
    }
    if (!expect(prepare_components(world, terrain, destruction),
                "prepare native assets"))
    {
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (world.scene() && world.scene()->name() != "Complete" &&
           std::chrono::steady_clock::now() < deadline)
    {
        if (!expect(terrain.synchronize() && destruction.synchronize() && script.tick(.016) &&
                        script.status().running,
                    "publish candidates and deliver one-shot delegates"))
        {
            return false;
        }
        std::this_thread::yield();
    }
    if (!expect(world.scene() && world.scene()->name() == "Complete",
                "success, reset cancellation and scene replacement finished"))
    {
        return false;
    }
    if (!expect(tasks.wait(), "finish independently owned worker candidates"))
    {
        return false;
    }
    for (unsigned index = 0; index < 3; ++index)
    {
        if (!expect(terrain.synchronize() && destruction.synchronize() && script.tick(.016) &&
                        script.status().running,
                    "old-scene delegates stay discarded"))
        {
            return false;
        }
    }
    script.stop_gameplay();
    for (const bool replace : {false, true})
    {
        if (!expect(script.start_gameplay("scripts/native-replace.js") &&
                        prepare_components(world, terrain, destruction) && script.tick(.016) &&
                        script.status().running,
                    "queue real script delegates before a native scene change"))
        {
            return false;
        }
        if (replace &&
            !expect(terrain.synchronize() && destruction.synchronize() && tasks.wait() &&
                        terrain.synchronize() && destruction.synchronize(),
                    "prepare completed results without dispatching script delegates"))
        {
            return false;
        }
        world.destroy_scene();
        Scene* replacement = replace ? world.create_scene("Native replacement") : nullptr;
        if (!expect((!replace || replacement != nullptr) && terrain.synchronize() &&
                        destruction.synchronize() && script.tick(.016) && script.status().running &&
                        script.tick(.016) && script.status().running,
                    "native replacement discards pending and completed old-scene delegates"))
        {
            return false;
        }
        script.stop_gameplay();
        if (!expect(world.scene() == replacement,
                    "stopping the old Run preserves a native-owned replacement scene"))
        {
            return false;
        }
        world.destroy_scene();
    }
    return expect(!script.status().running && script.status().last_error.empty(),
                  "script stops without retaining asynchronous delegates");
}
} // namespace

int main()
{
    test_support::ResourceFixture files;
    AppConfig app;
    app.application_name = "engine-terrain-destruction-script-test";
    app.assets_path = files.root / "builtin";
    app.persistent_data_path = files.root / "persistent";
    app.cache_path = files.root / "cache";
    if (files.root.empty() || !App::initialize(app) || !LogSystem::instance().initialize({}))
    {
        App::shutdown();
        return 1;
    }
    const bool passed = run(files);
    LogSystem::instance().shutdown();
    App::shutdown();
    return passed ? 0 : 1;
}
