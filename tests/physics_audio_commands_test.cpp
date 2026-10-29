#include "resource_fixture.hpp"

#include "editor/commands/editor_commands.hpp"
#include "editor/commands/scene_commands.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/scene/audio_listener.hpp"
#include "runtime/scene/audio_source.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/rigid_body.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/schema/audio_generated.h"
#include "runtime/world/world_system.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdio>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using Json = nlohmann::json;

bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "physics/audio commands: %s\n", message);
    }
    return condition;
}

Json target(const engine::Node& node)
{
    return {{"sceneId", std::to_string(node.scene().id())}, {"nodeId", std::to_string(node.id())}};
}

bool add_clip(test_support::ResourceFixture& fixture)
{
    engine::schema::AudioClipDataT clip;
    clip.sample_rate = 48000;
    clip.channels = 1;
    clip.frames = 480;
    clip.samples.resize(480, 0.125F);
    flatbuffers::FlatBufferBuilder builder;
    engine::schema::FinishAudioClipDataBuffer(builder,
                                              engine::schema::AudioClipData::Pack(builder, &clip));
    auto entry = std::make_unique<engine::schema::AssetEntryT>();
    entry->guid = "30000000-0000-4000-8000-000000000001";
    entry->path = "project/audio/commands.wav";
    entry->kind = engine::schema::AssetKind::audio;
    engine::ByteBuffer entity;
    const engine::ByteView payload(reinterpret_cast<const std::byte*>(builder.GetBufferPointer()),
                                   builder.GetSize());
    if (!engine::ResourceSystem::encode_resource(entry->guid, entry->kind, payload, entity,
                                                 entry->header_size))
    {
        return false;
    }
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto byte : entity)
    {
        hash = (hash ^ std::to_integer<std::uint8_t>(byte)) * 1099511628211ULL;
    }
    char digest[17]{};
    std::snprintf(digest, sizeof(digest), "%016llx", static_cast<unsigned long long>(hash));
    entry->content_hash = digest;
    entry->content_size = entity.size();
    entry->location = std::make_unique<engine::schema::FileLocationT>();
    entry->location->mount = "content";
    entry->location->path = "gen/" + entry->guid;
    entry->location->length = entity.size();
    std::ofstream output(fixture.root / entry->location->path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(entity.data()),
                 static_cast<std::streamsize>(entity.size()));
    output.close();
    if (!output)
    {
        return false;
    }
    fixture.catalog.entries.push_back(std::move(entry));
    return true;
}

struct Fixture
{
    test_support::ResourceFixture files;
    engine::ResourceSystem resources;
    engine::WorldSystem world;
    engine::editor::SceneCommands module{world};
    engine::editor::EditorCommands commands{[this]()
                                            { return world.scene() ? world.scene()->id() : 0; }};
    Json result;
    std::string error;

    bool initialize()
    {
        const engine::EngineConfig config;
        return !files.root.empty() && resources.initialize(config) && world.initialize(config) &&
               add_clip(files) && files.publish(resources) &&
               world.create_scene("Physics and audio commands") &&
               module.register_commands(commands);
    }

    bool execute(std::string_view name, const Json& arguments)
    {
        return commands.execute(name, arguments, result, error);
    }

    Json scene_identity() const
    {
        return {{"sceneId", std::to_string(world.scene()->id())}};
    }
};

template <typename T>
bool component_roundtrip(Fixture& fixture, engine::Node& node, std::string_view name,
                         const Json& settings, const std::vector<Json>& invalid)
{
    const std::string set_name = "set_" + std::string(name);
    const std::string get_name = "get_" + std::string(name);
    const auto identity = target(node);
    auto arguments = identity;
    arguments.update(settings);
    if (!expect(!fixture.execute(get_name, identity), "missing component query fails") ||
        !expect(fixture.execute(set_name, arguments), "setter creates a missing component"))
    {
        return false;
    }
    auto* component = node.get_component<T>();
    if (!expect(component && fixture.execute(get_name, identity), "getter reads created component"))
    {
        return false;
    }
    for (const auto& field : settings.items())
    {
        if (!expect(fixture.result.contains(field.key()) &&
                        fixture.result[field.key()] == field.value(),
                    "get returns the requested field value"))
        {
            return false;
        }
    }
    engine::schema::ComponentDataT saved;
    if (!expect(component->build_data(saved), "capture persisted component") ||
        !expect(fixture.execute("undo", fixture.scene_identity()) && !node.get_component<T>(),
                "undo removes only the newly added component") ||
        !expect(fixture.execute("redo", fixture.scene_identity()), "redo recreates component"))
    {
        return false;
    }
    component = node.get_component<T>();
    if (!expect(component && component->data() == saved, "redo restores complete generated data"))
    {
        return false;
    }
    const auto history = fixture.commands.undo_count();
    for (const auto& fields : invalid)
    {
        arguments = identity;
        arguments.update(fields);
        if (!expect(!fixture.execute(set_name, arguments) && fixture.result.is_null() &&
                        !fixture.error.empty() && node.get_component<T>() == component &&
                        component->data() == saved && fixture.commands.undo_count() == history,
                    "invalid edit preserves component identity, data and history"))
        {
            return false;
        }
    }
    arguments = identity;
    arguments["enabled"] = false;
    if (!expect(fixture.execute(set_name, arguments) && !component->enabled(),
                "enabled flag is editable") ||
        !expect(fixture.execute("undo", fixture.scene_identity()) &&
                    node.get_component<T>() == component && component->data() == saved,
                "undo updates existing component without replacing it") ||
        !expect(fixture.execute("redo", fixture.scene_identity()) && !component->enabled(),
                "redo applies enabled flag") ||
        !expect(fixture.execute("undo", fixture.scene_identity()), "restore enabled component"))
    {
        return false;
    }
    component->set_enabled(false);
    if (!expect(!fixture.execute("undo", fixture.scene_identity()) &&
                    node.get_component<T>() == component && !component->enabled() &&
                    fixture.commands.undo_count() == history,
                "stale undo never overwrites changes made outside the command"))
    {
        return false;
    }
    component->set_enabled(saved.enabled);
    fixture.commands.clear_history();
    return true;
}

bool settings_and_history()
{
    Fixture fixture;
    if (!expect(fixture.initialize(), "initialize command fixture"))
    {
        return false;
    }
    auto* node = fixture.world.scene()->create_node("Components");
    if (!expect(node != nullptr, "create component owner"))
    {
        return false;
    }
    const double infinity = std::numeric_limits<double>::infinity();
    if (!component_roundtrip<engine::Collider>(fixture, *node, "collider",
                                               {{"shape", "capsule"},
                                                {"halfExtents", {1, 2, 3}},
                                                {"radius", 0.75},
                                                {"halfHeight", 1.5},
                                                {"center", {0.5, 1, -0.5}},
                                                {"friction", 0.25},
                                                {"restitution", 0.5},
                                                {"isTrigger", true},
                                                {"enabled", true}},
                                               {{{"shape", "triangle"}},
                                                {{"halfExtents", {1, 0, 1}}},
                                                {{"center", {1, 2}}},
                                                {{"radius", -1}, {"friction", 0.75}},
                                                {{"halfHeight", -1}},
                                                {{"restitution", 2}},
                                                {{"friction", infinity}},
                                                {{"isTrigger", 1}},
                                                {{"enabled", "false"}},
                                                {{"unknown", 1}}}) ||
        !component_roundtrip<engine::RigidBody>(fixture, *node, "rigid_body",
                                                {{"motion", "dynamic"},
                                                 {"mass", 2},
                                                 {"gravityFactor", 0.5},
                                                 {"linearDamping", 0.25},
                                                 {"angularDamping", 0.5},
                                                 {"linearVelocity", {1, 2, 3}},
                                                 {"angularVelocity", {0.5, -0.5, 1}}},
                                                {{{"motion", "unknown"}},
                                                 {{"mass", 0}, {"gravityFactor", 2}},
                                                 {{"gravityFactor", -1}},
                                                 {{"linearDamping", 2}},
                                                 {{"angularDamping", -1}},
                                                 {{"linearVelocity", {1, 2, "3"}}},
                                                 {{"angularVelocity", {0, infinity, 0}}},
                                                 {{"motion", "static_body"}},
                                                 {{"enabled", nullptr}}}) ||
        !component_roundtrip<engine::AudioSource>(
            fixture, *node, "audio_source",
            {{"clip", "project/audio/commands.wav"},
             {"volume", 0.5},
             {"pitch", 1.5},
             {"loop", true},
             {"playOnAwake", false},
             {"spatial", false},
             {"minDistance", 2},
             {"maxDistance", 20}},
            {{{"volume", -1}},
             {{"pitch", 0}, {"volume", 0.25}},
             {{"minDistance", 30}},
             {{"maxDistance", 2}},
             {{"loop", "true"}},
             {{"playOnAwake", 1}},
             {{"spatial", nullptr}},
             {{"clip", 7}},
             {{"clip", "project/missing.wav"}, {"volume", 0.25}},
             {{"clip", "builtin/textures/white-srgb"}, {"volume", 0.25}},
             {{"pitch", infinity}}}) ||
        !component_roundtrip<engine::AudioListener>(
            fixture, *node, "audio_listener", {{"volume", 0.75}},
            {{{"volume", -1}}, {{"volume", 2}}, {{"volume", infinity}}, {{"enabled", 1}}}))
    {
        return false;
    }
    auto invalid = target(*node);
    invalid["nodeId"] = node->id();
    if (!expect(!fixture.execute("get_collider", invalid), "numeric node identity is rejected"))
    {
        return false;
    }
    invalid["nodeId"] = "18446744073709551616";
    if (!expect(!fixture.execute("get_collider", invalid), "overflow node identity is rejected"))
    {
        return false;
    }
    invalid = target(*node);
    invalid["mass"] = 1;
    return expect(!fixture.execute("get_rigid_body", invalid), "getter rejects setter arguments");
}

bool created_node_aliases()
{
    Fixture fixture;
    if (!expect(fixture.initialize(), "initialize identity fixture"))
    {
        return false;
    }
    auto arguments = fixture.scene_identity();
    arguments.update({{"name", "Created by command"}, {"component", "empty"}});
    if (!expect(fixture.execute("create_node", arguments), "create node through editor command"))
    {
        return false;
    }
    auto* scene = fixture.world.scene();
    auto* node = scene->nodes().front().get();
    const auto old_id = node->id();
    const auto old_target = target(*node);
    constexpr std::array<std::string_view, 4> names{"collider", "rigid_body", "audio_source",
                                                    "audio_listener"};
    for (const auto name : names)
    {
        if (!expect(fixture.execute("set_" + std::string(name), old_target),
                    "attach component to command-created node"))
        {
            return false;
        }
    }
    for (std::size_t index = 0; index < 5; ++index)
    {
        if (!expect(fixture.execute("undo", fixture.scene_identity()),
                    "undo components then their owning node"))
        {
            return false;
        }
    }
    if (!expect(scene->find_node(old_id) == nullptr && scene->nodes().empty(),
                "owner and attached components are removed") ||
        !expect(scene->create_node("Unrelated") != nullptr, "create intervening node"))
    {
        return false;
    }
    for (std::size_t index = 0; index < 5; ++index)
    {
        if (!expect(fixture.execute("redo", fixture.scene_identity()),
                    "redo components follows recreated owner identity"))
        {
            return false;
        }
    }
    node = scene->nodes().back().get();
    if (!expect(node->id() != old_id && node->name() == "Created by command" &&
                    node->get_component<engine::Collider>() &&
                    node->get_component<engine::RigidBody>() &&
                    node->get_component<engine::AudioSource>() &&
                    node->get_component<engine::AudioListener>(),
                "all four component edits resolve the replacement node") ||
        !expect(!fixture.execute("get_collider", old_target),
                "clients must use current identities rather than old aliases"))
    {
        return false;
    }
    for (const auto name : names)
    {
        if (!expect(fixture.execute("get_" + std::string(name), target(*node)),
                    "get reads the recreated component using current node id"))
        {
            return false;
        }
    }
    for (std::size_t index = 0; index < 5; ++index)
    {
        if (!fixture.execute("undo", fixture.scene_identity()))
        {
            return false;
        }
    }
    for (std::size_t index = 0; index < 5; ++index)
    {
        if (!fixture.execute("redo", fixture.scene_identity()))
        {
            return false;
        }
    }
    const auto previous_scene = fixture.scene_identity();
    fixture.world.destroy_scene();
    return expect(
        fixture.world.create_scene("Replacement") && !fixture.execute("undo", previous_scene) &&
            fixture.commands.undo_count() == 0 && !fixture.execute("set_collider", old_target),
        "new scene discards history and rejects stale component targets");
}

bool transactions_and_impulses()
{
    Fixture fixture;
    if (!expect(fixture.initialize(), "initialize transaction fixture"))
    {
        return false;
    }
    auto* node = fixture.world.scene()->create_node("Batch owner");
    if (!expect(node != nullptr, "create transaction owner"))
    {
        return false;
    }
    const auto identity = target(*node);
    auto collision = identity;
    collision["friction"] = 0.75;
    auto rigid = identity;
    rigid["mass"] = 0;
    Json batch = {
        {"commands", Json::array({{{"command", "set_collider"}, {"arguments", collision}},
                                  {{"command", "set_rigid_body"}, {"arguments", rigid}}})}};
    if (!expect(!fixture.execute("batch", batch) && !node->get_component<engine::Collider>() &&
                    !node->get_component<engine::RigidBody>() && fixture.commands.undo_count() == 0,
                "invalid later batch item rolls back earlier component creation"))
    {
        return false;
    }
    rigid["mass"] = 2;
    auto source = identity;
    source["clip"] = "project/audio/commands.wav";
    auto listener = identity;
    listener["volume"] = 0.5;
    batch = {
        {"commands", Json::array({{{"command", "set_collider"}, {"arguments", collision}},
                                  {{"command", "set_rigid_body"}, {"arguments", rigid}},
                                  {{"command", "set_audio_source"}, {"arguments", source}},
                                  {{"command", "set_audio_listener"}, {"arguments", listener}}})}};
    if (!expect(fixture.execute("batch", batch) && fixture.commands.undo_count() == 1,
                "all component changes commit as one transaction"))
    {
        return false;
    }
    constexpr std::array types{
        engine::ComponentType::ColliderData, engine::ComponentType::RigidBodyData,
        engine::ComponentType::AudioSourceData, engine::ComponentType::AudioListenerData};
    std::array<engine::schema::ComponentDataT, 4> snapshots;
    for (std::size_t index = 0; index < types.size(); ++index)
    {
        const auto* component = node->get_component(types[index]);
        if (!expect(component && component->build_data(snapshots[index]),
                    "snapshot complete transaction state"))
        {
            return false;
        }
    }
    auto* collider = node->get_component<engine::Collider>();
    auto changed = collider->settings();
    changed.friction = 0.25F;
    if (!expect(collider->set_settings(changed) &&
                    !fixture.execute("undo", fixture.scene_identity()) &&
                    collider->settings().friction == 0.25F && fixture.commands.undo_count() == 1,
                "stale first edit blocks batch undo without overwriting external change"))
    {
        return false;
    }
    for (std::size_t index = 1; index < types.size(); ++index)
    {
        const auto* component = node->get_component(types[index]);
        if (!expect(component && component->data() == snapshots[index],
                    "failed undo restores members already reverted in reverse order"))
        {
            return false;
        }
    }
    if (!expect(collider->restore_data(snapshots[0]) &&
                    fixture.execute("undo", fixture.scene_identity()),
                "transaction undo succeeds after expected state is restored"))
    {
        return false;
    }
    for (const auto type : types)
    {
        if (!expect(!node->get_component(type), "successful batch undo removes every attachment"))
        {
            return false;
        }
    }
    if (!expect(fixture.execute("redo", fixture.scene_identity()), "redo full component batch"))
    {
        return false;
    }
    auto* body = node->get_component<engine::RigidBody>();
    auto impulse = identity;
    impulse["impulse"] = {0, 4, 0};
    if (!expect(body && fixture.execute("add_impulse", impulse) && fixture.result["accepted"] &&
                    body->data() == snapshots[1] && fixture.commands.undo_count() == 1,
                "impulse is a transient request, not a persisted property or undo record"))
    {
        return false;
    }
    body->set_enabled(false);
    if (!expect(!fixture.execute("add_impulse", impulse), "disabled body rejects impulse"))
    {
        return false;
    }
    body->set_enabled(true);
    auto static_body = identity;
    static_body["motion"] = "static_body";
    if (!expect(fixture.execute("set_rigid_body", static_body) &&
                    !fixture.execute("add_impulse", impulse) &&
                    fixture.execute("undo", fixture.scene_identity()),
                "static body rejects impulse and motion edits are reversible"))
    {
        return false;
    }
    impulse["impulse"] = {0, 10000000, 0};
    if (!expect(!fixture.execute("add_impulse", impulse) && body->data() == snapshots[1],
                "oversized impulse cannot change persisted state"))
    {
        return false;
    }
    impulse["impulse"] = {0, 1, 0};
    listener["volume"] = 0.25;
    batch = {{"commands", Json::array({{{"command", "set_audio_listener"}, {"arguments", listener}},
                                       {{"command", "add_impulse"}, {"arguments", impulse}}})}};
    return expect(!fixture.execute("batch", batch) &&
                      node->get_component<engine::AudioListener>()->data() == snapshots[3] &&
                      fixture.commands.undo_count() == 1,
                  "batch rejects irreversible impulse and reverts preceding edits");
}

bool audio_controls()
{
    Fixture fixture;
    if (!expect(fixture.initialize(), "initialize playback command fixture"))
    {
        return false;
    }
    auto* node = fixture.world.scene()->create_node("Speaker");
    if (!expect(node != nullptr, "create speaker"))
    {
        return false;
    }
    const auto identity = target(*node);
    auto arguments = identity;
    arguments.update({{"clip", "project/audio/commands.wav"}, {"playOnAwake", false}});
    if (!expect(fixture.execute("set_audio_source", arguments), "assign an actual audio resource"))
    {
        return false;
    }
    auto* source = node->get_component<engine::AudioSource>();
    const auto history = fixture.commands.undo_count();
    if (!expect(source && source->clip_data() && fixture.execute("get_audio_state", identity) &&
                    !fixture.result["playing"].get<bool>() &&
                    fixture.result["playback"] == static_cast<int>(engine::AudioPlayback::stopped),
                "new audio source is stopped") ||
        !expect(fixture.execute("play_audio", identity) &&
                    source->playback() == engine::AudioPlayback::pending &&
                    !fixture.result["playing"].get<bool>(),
                "play accepts a request without falsely reporting device playback") ||
        !expect(fixture.execute("pause_audio", identity) &&
                    source->playback() == engine::AudioPlayback::paused,
                "pause controls accepted playback") ||
        !expect(fixture.execute("play_audio", identity) &&
                    source->playback() == engine::AudioPlayback::pending,
                "play resumes a paused source") ||
        !expect(fixture.execute("stop_audio", identity) &&
                    source->playback() == engine::AudioPlayback::stopped &&
                    fixture.execute("stop_audio", identity) &&
                    !fixture.execute("pause_audio", identity) &&
                    fixture.commands.undo_count() == history,
                "stop is repeatable and playback controls do not enter undo history"))
    {
        return false;
    }
    arguments = identity;
    arguments["enabled"] = false;
    if (!expect(fixture.execute("set_audio_source", arguments) &&
                    !fixture.execute("play_audio", identity) &&
                    source->playback() == engine::AudioPlayback::stopped,
                "disabled source rejects playback") ||
        !expect(fixture.execute("undo", fixture.scene_identity()) &&
                    fixture.execute("play_audio", identity),
                "undo restores enabled source playback"))
    {
        return false;
    }
    arguments = identity;
    arguments["unexpected"] = true;
    const Json batch = {
        {"commands", Json::array({{{"command", "stop_audio"}, {"arguments", identity}}})}};
    if (!expect(!fixture.execute("stop_audio", arguments) && !fixture.execute("batch", batch) &&
                    source->playback() == engine::AudioPlayback::pending &&
                    fixture.commands.undo_count() == history,
                "invalid controls and undo batches never partly apply playback changes"))
    {
        return false;
    }
    arguments = identity;
    arguments["clip"] = "";
    if (!expect(fixture.execute("set_audio_source", arguments) && !source->clip_data() &&
                    !fixture.execute("play_audio", identity),
                "clearing clip releases audio and rejects play") ||
        !expect(fixture.execute("undo", fixture.scene_identity()) && source->clip_data() &&
                    source->clip_path() == "project/audio/commands.wav",
                "undo restores audio resource ownership"))
    {
        return false;
    }
    auto* empty = fixture.world.scene()->create_node("Invalid source");
    if (!expect(empty != nullptr, "create invalid source owner"))
    {
        return false;
    }
    arguments = target(*empty);
    arguments["clip"] = "builtin/textures/white-srgb";
    return expect(!fixture.execute("set_audio_source", arguments) &&
                      !empty->get_component<engine::AudioSource>() &&
                      fixture.commands.undo_count() == history,
                  "resource type failure removes a newly created partial audio component");
}
} // namespace

int main()
{
    return settings_and_history() && created_node_aliases() && transactions_and_impulses() &&
                   audio_controls()
               ? 0
               : 1;
}
