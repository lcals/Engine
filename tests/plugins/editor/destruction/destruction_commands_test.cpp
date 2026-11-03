#include "editor/commands/editor_commands.hpp"
#include "editor/commands/scene_commands.hpp"
#include "plugins/editor/destruction/destruction_authoring.hpp"
#include "plugins/editor/destruction/destruction_commands.hpp"
#include "plugins/runtime/destruction/destruction_component.hpp"
#include "plugins/runtime/destruction/destruction_system.hpp"
#include "resource_fixture.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/world/world_system.hpp"

#include <flatbuffers/flatbuffers.h>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <utility>

namespace
{
using namespace engine;
using Json = nlohmann::json;
using plugins::destruction::DestructionComponent;
using plugins::destruction::DestructionSystem;
constexpr const char* asset_guid = "83000000-0000-4000-8000-000000000001";

bool expect(bool condition, const char* message, const std::string& error = {})
{
    if (!condition)
    {
        std::fprintf(stderr, "destruction commands: %s (%s)\n", message, error.c_str());
    }
    return condition;
}

bool add_asset(test_support::ResourceFixture& fixture)
{
    schema::DestructionGeneratorDataT settings;
    settings.size = {2, 3, 1};
    settings.slices_x = 1;
    settings.slices_y = 3;
    settings.slices_z = 1;
    schema::DestructionAssetDataT asset;
    std::string error;
    if (!plugins::destruction::DestructionAuthoring::generate(settings, asset, error))
    {
        return false;
    }
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishDestructionAssetDataBuffer(builder,
                                             schema::DestructionAssetData::Pack(builder, &asset));
    auto entry = std::make_unique<schema::AssetEntryT>();
    entry->guid = asset_guid;
    entry->path = "test.destruction";
    entry->kind = schema::AssetKind::destruction;
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

struct Fixture
{
    ResourceSystem resources;
    WorldSystem world;
    PhysicsSystem physics{&world};
    DestructionSystem destruction{world, physics};
    test_support::ResourceFixture files;

    ~Fixture()
    {
        destruction.shutdown();
        world.shutdown();
        physics.shutdown();
        resources.shutdown();
    }

    bool initialize()
    {
        return resources.initialize({}) && physics.initialize({}) && world.initialize({}) &&
               destruction.initialize({}) && add_asset(files) && files.publish(resources);
    }
};

bool run(Fixture& fixture)
{
    auto* scene = fixture.world.create_scene("Destruction command test");
    auto* node = scene ? scene->create_node("Specimen") : nullptr;
    auto* transform = node ? node->get_component<Transform>() : nullptr;
    TransformSettings pose;
    pose.position = {0, 4, 0};
    if (!expect(transform && transform->set_settings(pose), "create elevated specimen"))
    {
        return false;
    }
    editor::SceneCommands scenes(fixture.world);
    editor::EditorCommands commands(
        [&fixture]()
        {
            const auto* current = fixture.world.scene();
            return current ? current->id() : 0;
        });
    std::string error;
    Json result;
    const Json identity = {{"sceneId", std::to_string(scene->id())},
                           {"nodeId", std::to_string(node->id())}};
    const Json scene_identity = {{"sceneId", std::to_string(scene->id())}};
    const auto call = [&](std::string_view name, const Json& args)
    {
        error.clear();
        return commands.execute(name, args, result, error);
    };
    if (!expect(scenes.register_commands(commands) &&
                    plugins::destruction::register_destruction_commands(
                        fixture.world, scenes, commands, fixture.files.root),
                "register destruction commands"))
    {
        return false;
    }
    auto settings = identity;
    settings["destruction"] = "test.destruction";
    if (!expect(call("set_destruction", settings), "create component through command", error))
    {
        return false;
    }
    auto* component = node->get_component<DestructionComponent>();
    if (!expect(component && component->ready() && component->group_count() == 1,
                "command publishes initialized support graph"))
    {
        return false;
    }
    auto* support_node = scene->create_node("Foundation");
    auto* support = support_node ? support_node->add_component<Collider>() : nullptr;
    auto* support_transform = support_node ? support_node->get_component<Transform>() : nullptr;
    TransformSettings ground_pose;
    ground_pose.position = {0, 3.5, 0};
    schema::ColliderDataT ground;
    ground.half_extents = {3, .5, 3};
    if (!expect(support && support_transform && support_transform->set_settings(ground_pose) &&
                    fixture.physics.set_collider_data(*support, ground),
                "create explicit support"))
    {
        return false;
    }
    auto refresh = identity;
    refresh["supportNodeId"] = std::to_string(support_node->id());
    refresh["maxGap"] = 0.2;
    refresh["minCoverage"] = 0.7;
    const auto before_support = component->revision();
    if (!expect(call("destruction_refresh_support", refresh) &&
                    component->revision() == before_support && component->dynamic_groups() == 0 &&
                    call("undo", scene_identity) && component->revision() == before_support &&
                    call("redo", scene_identity) && component->revision() == before_support,
                "supported refresh and its history leave graph and revision unchanged", error))
    {
        return false;
    }
    support->set_enabled(false);
    if (!expect(fixture.physics.set_collider_data(*support, ground) &&
                    call("destruction_refresh_support", refresh) &&
                    component->dynamic_groups() == 1 && call("undo", scene_identity) &&
                    component->dynamic_groups() == 0 && call("redo", scene_identity) &&
                    component->dynamic_groups() == 1 && call("destruction_reset", identity) &&
                    component->dynamic_groups() == 0,
                "support release, undo, redo and reset preserve complete graph state", error))
    {
        return false;
    }
    auto stress = identity;
    stress["gravity"] = {0, -9.81, 0};
    stress["elasticLimit"] = 1e8;
    stress["fatalLimit"] = 2e8;
    stress["iterations"] = 500;
    const auto before_stress = component->revision();
    ByteBuffer intact_stress;
    ByteBuffer checked_stress;
    if (!expect(component->snapshot(intact_stress) && call("destruction_apply_stress", stress) &&
                    component->revision() == before_stress && call("undo", scene_identity) &&
                    call("redo", scene_identity) && component->revision() == before_stress &&
                    component->snapshot(checked_stress) && checked_stress == intact_stress,
                "safe stress and its undo/redo preserve physical state and node revision", error))
    {
        return false;
    }
    auto invalid_stress = stress;
    invalid_stress["iterations"] = 1.5;
    if (!expect(!call("destruction_apply_stress", invalid_stress) &&
                    component->snapshot(checked_stress) && checked_stress == intact_stress,
                "stress command rejects fractional iteration counts before publication"))
    {
        return false;
    }
    stress["elasticLimit"] = 10;
    stress["fatalLimit"] = 20;
    ByteBuffer stressed;
    if (!expect(call("destruction_apply_stress", stress) && component->group_count() > 1 &&
                    component->snapshot(stressed) && stressed != intact_stress &&
                    call("undo", scene_identity) && component->snapshot(checked_stress) &&
                    checked_stress == intact_stress && call("redo", scene_identity) &&
                    component->snapshot(checked_stress) && checked_stress == stressed &&
                    call("destruction_reset", identity),
                "stress command, undo and redo restore exact Blast and physical snapshots", error))
    {
        return false;
    }
    const auto scene_revision = commands.mutation_revision();
    auto damage = identity;
    damage["center"] = {0, 5, 0};
    damage["radius"] = 0.8;
    damage["damage"] = 10;
    damage["impulse"] = 20;
    if (!expect(call("destruction_damage", damage) && component->group_count() >= 2 &&
                    component->dynamic_groups() >= 1,
                "damage splits connected physical groups", error) ||
        !expect(call("destruction_reset", identity) && component->group_count() == 1,
                "reset starts a second undo record", error) ||
        !expect(call("undo", scene_identity) && component->group_count() >= 2 &&
                    call("undo", scene_identity) && component->group_count() == 1 &&
                    call("redo", scene_identity) && component->group_count() >= 2,
                "shared revision supports sequential undo and redo", error) ||
        !expect(commands.mutation_revision() == scene_revision,
                "runtime damage and restoration do not dirty scene settings"))
    {
        return false;
    }
    const auto damage_revision = component->revision();
    for (unsigned frame = 0; frame < 8; ++frame)
    {
        if (!expect(fixture.physics.tick(1.0 / 60.0) && fixture.destruction.synchronize(),
                    "advance physical fragments"))
        {
            return false;
        }
    }
    if (!expect(component->revision() == damage_revision && call("undo", scene_identity) &&
                    component->group_count() == 1 && call("redo", scene_identity),
                "ordinary physical movement does not invalidate undo", error))
    {
        return false;
    }
    const auto damaged_groups = component->group_count();
    auto density = identity;
    density["density"] = 200;
    if (!expect(call("set_destruction", density) && component->settings().density == 200 &&
                    component->group_count() == damaged_groups && call("undo", scene_identity) &&
                    component->settings().density == 100 &&
                    component->group_count() == damaged_groups && call("undo", scene_identity) &&
                    component->group_count() == 1 && call("redo", scene_identity),
                "settings edits preserve damage and remain in the same undo sequence", error) ||
        !expect(component->damage_sphere({0, 6, 0}, 0.8F, 10, 0) && !call("undo", scene_identity),
                "external gameplay mutation invalidates prior history", error))
    {
        return false;
    }
    commands.clear_history();
    auto* added = scene->create_node("New editable specimen");
    auto* pending = scene->create_node("Unrelated pending specimen");
    auto* pending_component = pending ? pending->add_component<DestructionComponent>() : nullptr;
    schema::DestructionDataT pending_settings;
    pending_settings.destruction_guid = asset_guid;
    TransformSettings invalid_pose;
    invalid_pose.scale = {2, 1, 1};
    if (!expect(added && pending_component &&
                    pending->get_component<Transform>()->set_settings(invalid_pose) &&
                    pending_component->set_settings(pending_settings),
                "prepare unrelated pending object with an unsupported transform"))
    {
        return false;
    }
    const auto before_nodes = scene->nodes().size();
    Json added_identity = {{"sceneId", std::to_string(scene->id())},
                           {"nodeId", std::to_string(added->id())},
                           {"destruction", "test.destruction"}};
    if (!expect(call("set_destruction", added_identity) && !pending_component->ready() &&
                    scene->nodes().size() == before_nodes + 1 && call("undo", scene_identity) &&
                    !added->get_component<DestructionComponent>() &&
                    scene->nodes().size() == before_nodes && call("redo", scene_identity) &&
                    added->get_component<DestructionComponent>() &&
                    scene->nodes().size() == before_nodes + 1,
                "single-object edit ignores unrelated pending work and undo removes generated "
                "nodes immediately",
                error))
    {
        return false;
    }
    commands.clear_history();
    return true;
}
} // namespace

int main()
{
    Fixture fixture;
    return expect(fixture.initialize(), "initialize destruction fixture") && run(fixture) ? 0 : 1;
}
