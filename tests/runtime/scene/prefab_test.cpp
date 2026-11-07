#include "resource_fixture.hpp"
#include "resource_services.hpp"
#include "scene_snapshot.hpp"

#include "runtime/engine_config.hpp"
#include "runtime/resource/codecs/prefab_resource_codec.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/world/world_system.hpp"

#if defined(ENGINE_TEST_CHARACTER_CONTROLLER)
#include "plugins/runtime/character_controller/character_camera.hpp"
#include "plugins/runtime/character_controller/character_controller.hpp"
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace
{

using namespace engine;

bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "prefab test: %s\n", message);
    }
    return condition;
}

bool add_resource(test_support::ResourceFixture& files, std::string_view path,
                  schema::AssetKind kind, const flatbuffers::FlatBufferBuilder& builder,
                  std::vector<std::string> dependencies = {})
{
    auto entry = std::make_unique<schema::AssetEntryT>();
    std::array<char, 37> guid{};
    std::snprintf(guid.data(), guid.size(), "50000000-0000-4000-8000-%012zu",
                  files.catalog.entries.size() + 1);
    entry->guid = guid.data();
    entry->path = path;
    entry->kind = kind;
    std::sort(dependencies.begin(), dependencies.end());
    dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
    entry->dependencies = std::move(dependencies);
    ByteBuffer entity;
    const ByteView payload(reinterpret_cast<const std::byte*>(builder.GetBufferPointer()),
                           builder.GetSize());
    if (!ResourceSystem::encode_resource(entry->guid, kind, payload, entity, entry->header_size))
    {
        return false;
    }
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto value : entity)
    {
        hash = (hash ^ std::to_integer<std::uint8_t>(value)) * 1099511628211ULL;
    }
    std::array<char, 17> digest{};
    std::snprintf(digest.data(), digest.size(), "%016llx", static_cast<unsigned long long>(hash));
    entry->content_hash = digest.data();
    entry->content_size = entity.size();
    entry->location = std::make_unique<schema::FileLocationT>();
    entry->location->mount = "content";
    entry->location->path = "gen/" + entry->guid;
    entry->location->length = entity.size();
    std::ofstream file(files.root / entry->location->path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(entity.data()),
               static_cast<std::streamsize>(entity.size()));
    file.close();
    if (!file)
    {
        return false;
    }
    files.catalog.entries.push_back(std::move(entry));
    return true;
}

bool add_prefab(test_support::ResourceFixture& files, std::string_view path,
                const schema::PrefabDataT& data)
{
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishPrefabDataBuffer(builder, schema::PrefabData::Pack(builder, &data));
    return add_resource(files, path, schema::AssetKind::prefab, builder);
}

bool add_scene(test_support::ResourceFixture& files, std::string_view path,
               const schema::SceneDataT& data)
{
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishSceneDataBuffer(builder, schema::SceneData::Pack(builder, &data));
    return add_resource(files, path, schema::AssetKind::scene, builder);
}

template <class T> void add_component(schema::NodeDataT& node, const T& settings)
{
    auto component = test_support::component_data();
    component->payload.Set(T(settings));
    node.components.push_back(std::move(component));
}

std::unique_ptr<schema::NodeDataT> node_data(std::uint64_t id, std::uint64_t parent,
                                             std::string_view name)
{
    auto node = std::make_unique<schema::NodeDataT>();
    node->info = test_support::node_info();
    node->info->properties->id = id;
    node->info->parent_id = parent;
    node->info->properties->name = name;
    add_component(*node, schema::TransformDataT{});
    return node;
}

schema::PrefabDataT template_data()
{
    schema::PrefabDataT data;
    data.schema_version = 2;
    data.root_id = 9;
    // 故意让子节点先于根，并使用稀疏源身份。
    auto camera = node_data(20, 9, "Camera");
    add_component(*camera, schema::CameraDataT{});
    data.nodes.push_back(std::move(camera));
    data.nodes.push_back(node_data(9, 0, "Template"));
    auto light = node_data(7, 9, "Light");
    schema::LightDataT settings;
    settings.intensity = 8;
    add_component(*light, settings);
    light->components.back()->flags->visible = false;
    data.nodes.push_back(std::move(light));
    return data;
}

Node local_node(Scene& scene, std::uint64_t id)
{
    for (const auto node : test_support::scene_nodes(scene))
    {
        if (node.local_id() == id)
        {
            return node;
        }
    }
    return {};
}

Node mapped_node(Scene& scene, const schema::PrefabInstanceDataT& instance, std::uint64_t source_id)
{
    for (const auto& mapping : instance.node_mappings)
    {
        if (mapping->source_id == source_id)
        {
            return local_node(scene, mapping->target_id);
        }
    }
    return {};
}

struct Fixture
{
    test_support::ResourceFixture files;
    test_support::ResourceServices services;
    engine::ResourceSystem& resources = services.core;
    engine::WorldSystem world{&services.loader};

    bool initialize()
    {
        return (services.initialize({}) && services.prepare_builtins()) && world.initialize({}) &&
               world.create_scene("Prefab scene") != nullptr;
    }
};

bool round_trip_and_lifecycle()
{
    Fixture fixture;
    if (!expect(fixture.initialize() && add_prefab(fixture.files, "unit.prefab", template_data()) &&
                    fixture.services.publish_and_load(fixture.files.catalog,
                                                      {{"content", fixture.files.root}}),
                "publish an ordinary component subtree"))
    {
        return false;
    }
    auto* scene = fixture.world.scene();
    auto existing = scene->create_node("Existing");
    auto original_camera = existing ? existing.add_component<Camera>() : Camera{};
    if (!original_camera || !scene->set_active_camera(original_camera))
    {
        return false;
    }
    auto first = scene->instantiate_prefab("unit.prefab", "First");
    auto second = scene->instantiate_prefab("unit.prefab", "");
    if (!expect(first && second && first != second &&
                    test_support::scene_nodes(*scene).size() == 7 &&
                    test_support::prefab_instances(*scene).size() == 2 && first.name() == "First" &&
                    second.name() == "Template" && scene->active_camera() == original_camera,
                "two instances preserve caller camera and independent ordinary node trees"))
    {
        return false;
    }
    const auto first_link = test_support::prefab_instances(*scene)[0];
    const auto second_link = test_support::prefab_instances(*scene)[1];
    auto camera_node = mapped_node(*scene, first_link, 20);
    auto light_node = mapped_node(*scene, first_link, 7);
    if (!expect(camera_node && light_node && camera_node.parent() == first &&
                    light_node.parent() == first &&
                    test_support::read_settings(light_node.get_component<Light>()).intensity == 8 &&
                    !light_node.get_component<Light>().visible() &&
                    first_link.overrides.size() == 1 &&
                    first_link.overrides[0]->fields == std::vector<std::string>{"name"} &&
                    second_link.overrides.empty(),
                "source-local hierarchy, state and explicit instance name survive expansion"))
    {
        return false;
    }
    auto invalid = first_link;
    invalid.node_mappings.back()->target_id = second.local_id();
    if (!expect(!scene->set_prefab_instance(invalid) &&
                    test_support::prefab_instances(*scene)[0] == first_link,
                "foreign subtree mapping is rejected without replacing existing metadata") ||
        !expect(scene->remove_prefab_instance(first.local_id()) &&
                    scene->find_node(first.id()) == first && scene->set_prefab_instance(first_link),
                "unlinking and relinking only changes source metadata"))
    {
        return false;
    }
    if (!scene->set_active_camera(camera_node.get_component<Camera>()))
    {
        return false;
    }
    const auto camera_local_id = camera_node.local_id();
    schema::SceneDataT snapshot;
    if (!expect(second.set_parent(first) && !scene->build_data(snapshot) && second.set_parent({}),
                "nested source links cannot silently enter a saved scene"))
    {
        return false;
    }
    if (!expect(scene->build_data(snapshot) && snapshot.prefab_instances.size() == 2 &&
                    add_scene(fixture.files, "saved.scene", snapshot) &&
                    fixture.services.publish_and_load(fixture.files.catalog,
                                                      {{"content", fixture.files.root}}),
                "save expanded runtime data together with stable source links and overrides"))
    {
        return false;
    }
    std::erase_if(fixture.files.catalog.entries,
                  [](const auto& entry) { return entry->kind == schema::AssetKind::prefab; });
    if (!fixture.services.publish_and_load(fixture.files.catalog,
                                           {{"content", fixture.files.root}}))
    {
        return false;
    }
    scene = fixture.world.load_scene("saved.scene");
    if (!expect(scene && scene->active_camera() &&
                    scene->active_camera().node().local_id() == camera_local_id &&
                    test_support::prefab_instances(*scene).size() == 2 &&
                    test_support::prefab_instances(*scene)[1].root_id == first_link.root_id &&
                    test_support::prefab_instances(*scene)[1] == first_link,
                "scene restore needs no template and keeps local IDs, camera and exact overrides"))
    {
        return false;
    }
    first = local_node(*scene, first_link.root_id);
    second = local_node(*scene, second_link.root_id);
    if (!expect(first && second && first.set_persistent(true), "make one instance persistent"))
    {
        return false;
    }
    const auto first_runtime_id = first.id();
    fixture.world.unload_scene();
    if (!expect(fixture.world.find_node(first_runtime_id) == first &&
                    fixture.world.scenes().size() == 1 &&
                    test_support::prefab_instances(*scene).size() == 1 &&
                    test_support::prefab_instances(*scene)[0].root_id == first_link.root_id,
                "scene unload retains the persistent instance and releases unrelated links"))
    {
        return false;
    }
    return expect(scene->destroy_node(first_runtime_id) &&
                      test_support::prefab_instances(*scene).empty() &&
                      test_support::scene_nodes(*scene).empty(),
                  "destroying an instance root removes its source record and full subtree");
}

bool explicit_removal_and_reserved_identities()
{
    Fixture fixture;
    if (!fixture.initialize() || !add_prefab(fixture.files, "unit.prefab", template_data()) ||
        !fixture.services.publish_and_load(fixture.files.catalog,
                                           {{"content", fixture.files.root}}))
    {
        return false;
    }
    auto* scene = fixture.world.scene();
    auto root = scene->instantiate_prefab("unit.prefab", "");
    if (!root)
    {
        return false;
    }
    auto link = test_support::prefab_instances(*scene)[0];
    auto child = mapped_node(*scene, link, 20);
    schema::SceneDataT snapshot;
    if (!expect(child && scene->destroy_node(child.id()) && !scene->build_data(snapshot) &&
                    snapshot.nodes.empty() && test_support::prefab_instances(*scene).size() == 1,
                "runtime removal never silently becomes an editor override"))
    {
        return false;
    }
    auto removed = std::make_unique<schema::PrefabNodeOverrideT>();
    removed->node_id = 20;
    removed->removed = true;
    link.overrides.push_back(std::move(removed));
    auto tombstone = std::make_unique<schema::NodeIdMappingT>();
    tombstone->source_id = 999;
    tombstone->target_id = 9000;
    tombstone->retired = true;
    link.node_mappings.push_back(std::move(tombstone));
    if (!expect(scene->set_prefab_instance(link) && scene->build_data(snapshot) &&
                    snapshot.prefab_instances[0]->node_mappings.size() == 4,
                "explicit removal and a source-deleted tombstone keep stable mapping identities"))
    {
        return false;
    }
    if (!add_scene(fixture.files, "removed.scene", snapshot) ||
        !fixture.services.publish_and_load(fixture.files.catalog,
                                           {{"content", fixture.files.root}}))
    {
        return false;
    }
    scene = fixture.world.load_scene("removed.scene");
    root = scene ? local_node(*scene, link.root_id) : Node{};
    if (!expect(root && test_support::prefab_instances(*scene).size() == 1,
                "scene restore preserves explicit removals and retired identities"))
    {
        return false;
    }
    auto fresh = scene->create_node("Fresh");
    if (!expect(fresh && fresh.local_id() > 9000,
                "new nodes cannot reuse deleted template mapping identities"))
    {
        return false;
    }
    auto light = mapped_node(*scene, link, 7);
    if (!light.set_transient(true))
    {
        return false;
    }
    if (!expect(!scene->build_data(snapshot),
                "a transient mapped child cannot silently disappear from a saved instance"))
    {
        return false;
    }
    if (!root.set_transient(true))
    {
        return false;
    }
    return expect(scene->build_data(snapshot) && snapshot.prefab_instances.empty() &&
                      snapshot.nodes.size() == 1,
                  "a fully transient instance is omitted with its source record");
}

bool rollback_and_plugin_references()
{
    Fixture fixture;
    auto character = template_data();
    schema::CharacterControllerDataT controller;
    controller.camera_node_id = 20;
    add_component(*character.nodes[1], controller);
    schema::CharacterCameraDataT camera;
    camera.target_node_id = 9;
    add_component(*character.nodes[0], camera);
    auto missing_plugin = template_data();
    add_component(*missing_plugin.nodes.back(), controller);
    auto external = character;
    external.nodes[0]->components.back()->payload.AsCharacterCameraData()->target_node_id = 9999;
    if (!fixture.initialize() || !add_prefab(fixture.files, "character.prefab", character) ||
        !add_prefab(fixture.files, "missing-plugin.prefab", missing_plugin) ||
        !add_prefab(fixture.files, "external.prefab", external) ||
        !fixture.services.publish_and_load(fixture.files.catalog,
                                           {{"content", fixture.files.root}}))
    {
        return false;
    }
    auto* scene = fixture.world.scene();
    auto existing = scene->create_node("Existing");
    auto active_camera = existing ? existing.add_component<Camera>() : Camera{};
    if (!active_camera || !scene->set_active_camera(active_camera))
    {
        return false;
    }
    if (!expect(!scene->instantiate_prefab("missing.prefab", "Missing") &&
                    !scene->instantiate_prefab("builtin://meshes/cube", "Wrong kind") &&
                    !scene->instantiate_prefab("missing-plugin.prefab", "Missing plugin") &&
                    test_support::scene_nodes(*scene).size() == 1 &&
                    test_support::prefab_instances(*scene).empty() &&
                    scene->active_camera() == active_camera,
                "missing resources or plugins roll back even unattached candidate nodes"))
    {
        return false;
    }
#if defined(ENGINE_TEST_CHARACTER_CONTROLLER)
    using plugins::character_controller::CharacterCamera;
    using plugins::character_controller::CharacterController;
    if (!fixture.world.component_registry().add<CharacterController>() ||
        !fixture.world.component_registry().add<CharacterCamera>())
    {
        return false;
    }
    auto first = scene->instantiate_prefab("character.prefab", "First");
    auto second = scene->instantiate_prefab("character.prefab", "Second");
    if (!expect(first && second && test_support::prefab_instances(*scene).size() == 2,
                "plugins register their ordinary component factories"))
    {
        return false;
    }
    for (const auto& link : test_support::prefab_instances(*scene))
    {
        const auto player = mapped_node(*scene, link, 9);
        const auto view = mapped_node(*scene, link, 20);
        if (!expect(player && view &&
                        test_support::read_settings(player.get_component<CharacterController>())
                                .camera_node_id == view.local_id() &&
                        test_support::read_settings(view.get_component<CharacterCamera>())
                                .target_node_id == player.local_id(),
                    "each instance remaps both directions of the plugin-owned camera binding"))
        {
            return false;
        }
        std::array<schema::NodeIdMappingT, 1> retired;
        retired[0].source_id = 9;
        retired[0].target_id = player.local_id();
        retired[0].retired = true;
        auto reference = *character.nodes[0]->components.back();
        const auto before = reference;
        if (!expect(
                !view.get_component<CharacterCamera>().remap_node_references(reference, retired) &&
                    reference == before,
                "retired template identities cannot revive a camera target reference"))
        {
            return false;
        }
    }
    const auto count = test_support::scene_nodes(*scene).size();
    if (!expect(!scene->instantiate_prefab("external.prefab", "External reference") &&
                    test_support::scene_nodes(*scene).size() == count &&
                    test_support::prefab_instances(*scene).size() == 2 &&
                    scene->active_camera() == active_camera,
                "unbound external template references fail without affecting live instances"))
    {
        return false;
    }
#endif
    return true;
}

} // namespace

int main()
{
    auto old_format = template_data();
    old_format.schema_version = 1;
    if (!expect(!PrefabResourceCodec::validate(old_format) &&
                    schema::PrefabDataT{}.schema_version == 0 &&
                    schema::SceneDataT{}.schema_version == 0,
                "old or unmarked scene and prefab layouts require reimport"))
    {
        return 1;
    }
    return round_trip_and_lifecycle() && explicit_removal_and_reserved_identities() &&
                   rollback_and_plugin_references()
               ? 0
               : 1;
}
