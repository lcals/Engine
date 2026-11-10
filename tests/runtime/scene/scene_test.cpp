#include "resource_fixture.hpp"
#include "resource_services.hpp"
#include "scene_snapshot.hpp"

#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/light_probe_volume_component.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/world/world_system.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{

bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "scene test: %s\n", message);
    }
    return condition;
}

std::vector<engine::Component> components_of(engine::Node node)
{
    std::vector<engine::Component> result;
    if (!node.collect_components(result))
    {
        std::abort();
    }
    return result;
}

bool near(float actual, float expected)
{
    return std::abs(actual - expected) < 0.0001F;
}

bool add_scene_resource(test_support::ResourceFixture& fixture, std::string_view path,
                        const engine::schema::SceneDataT& data);

engine::Scene* load_and_restore(test_support::ResourceServices& services,
                                engine::WorldSystem& world, std::string_view path)
{
    engine::ResourceReference source;
    return services.load(path, source) ? world.load_scene(path) : nullptr;
}

bool scene_node_queries(test_support::ResourceServices& services)
{
    using namespace engine;
    WorldSystem world(&services.loader);
    if (!world.initialize({}))
    {
        return false;
    }
    auto* first = world.create_scene("Query ownership");
    auto root = first ? first->create_node("Root") : Node{};
    auto other_root = first ? first->create_node("Root") : Node{};
    auto branch = first ? first->create_node("Branch") : Node{};
    auto leaf = first ? first->create_node("Leaf") : Node{};
    auto empty = first ? first->create_node("") : Node{};
    auto slash = first ? first->create_node("a/b") : Node{};
    auto backslash = first ? first->create_node(R"(back\name)") : Node{};
    if (!root || !other_root || !branch || !leaf || !empty || !slash || !backslash ||
        !branch.set_parent(root) || !leaf.set_parent(branch) || !backslash.set_parent(root) ||
        !root.set_enabled(false) || !root.set_visible(false) || !branch.set_transient(true) ||
        !root.add_component<Light>())
    {
        return false;
    }
    const auto root_local_id = root.local_id();
    if (!expect(first->find_node("Root") == root && first->find_node("") == empty &&
                    first->find_node("a/b") == slash && !first->find_node("root") &&
                    !first->find_node("missing") && !first->find_local_node(0) &&
                    !first->find_local_node(std::numeric_limits<std::uint64_t>::max()) &&
                    first->find_local_node(root_local_id) == root &&
                    !first->find_node(root_local_id) && !first->find_local_node(root.id()) &&
                    root.find_child("Branch/Leaf") == leaf &&
                    root.find_child(R"(back\name)") == backslash && !root.find_child("Leaf") &&
                    !Node{}.find_child("Branch"),
                "queries use exact names and distinct identities without filtering local state"))
    {
        return false;
    }
    for (const auto path : {"", "/Branch", "Branch/", "Branch//Leaf", ".", "..", "Branch/.",
                            "Branch/..", "Branch/../Leaf", "branch"})
    {
        if (!expect(!root.find_child(path), "relative paths reject invalid segments and case"))
        {
            return false;
        }
    }
    auto duplicate = first->create_node("Branch");
    if (!duplicate || !duplicate.set_parent(root) ||
        !expect(
            first->find_node("Branch") == branch && !root.find_child("Branch") &&
                !root.find_child("Branch/Leaf"),
            "name lookup preserves creation order while child paths reject ambiguous siblings") ||
        !branch.set_parent(other_root) ||
        !expect(root.find_child("Branch") == duplicate &&
                    other_root.find_child("Branch/Leaf") == leaf,
                "reparenting immediately changes direct-child paths") ||
        !first->destroy_node(branch.id()) ||
        !expect(!branch && !leaf && !branch.find_child("Leaf") &&
                    first->find_node("Branch") == duplicate,
                "deleted query handles stay invalid and names fall back to the next survivor") ||
        !root.set_persistent(true))
    {
        return false;
    }
    world.unload_scene();
    auto* next = world.create_scene("Another query owner");
    auto replacement = next ? next->create_node("Root") : Node{};
    if (!expect(replacement && next != first && replacement.local_id() == root_local_id &&
                    next->find_local_node(root_local_id) == replacement &&
                    first->find_local_node(root_local_id) == root &&
                    next->find_node("Root") == replacement && first->find_node("Root") == root &&
                    !next->find_node(root.id()) && !first->find_node(replacement.id()) &&
                    root.find_child("Branch") == duplicate,
                "persistent and active scenes resolve overlapping local identities independently"))
    {
        return false;
    }
    world.shutdown();
    return expect(world.initialize({}) && !root && !replacement && !root.find_child("Branch"),
                  "world reinitialization cannot revive query result identities");
}

bool node_component_state(test_support::ResourceServices& services)
{
    test_support::ResourceFixture fixture;
    auto* resources = &services.core;
    engine::WorldSystem world(&services.loader);
    if (!expect(resources && world.initialize({}), "initialize hierarchy state world"))
    {
        return false;
    }
    auto* scene = world.create_scene("State hierarchy");
    auto parent = scene ? scene->create_node("Parent") : engine::Node{};
    auto child = scene ? scene->create_node("Child") : engine::Node{};
    auto light = child ? child.add_component<engine::Light>() : engine::Light{};
    if (!expect(parent && child && light && child.set_parent(parent), "create state hierarchy"))
    {
        return false;
    }
    const auto identity = light.instance_id();
    if (!expect(identity != 0 && identity != child.get_component<engine::Transform>().instance_id(),
                "all component kinds share unique nonzero identities"))
    {
        return false;
    }
    if (!parent.set_visible(false))
    {
        return false;
    }
    if (!expect(light.active() && light.visible() && !light.visible_in_hierarchy(),
                "parent hide changes effective display without disabling simulation"))
    {
        return false;
    }
    if (!light.set_visible(false))
    {
        return false;
    }
    if (!parent.set_visible(true))
    {
        return false;
    }
    if (!parent.set_enabled(false))
    {
        return false;
    }
    if (!expect(child.enabled() && light.enabled() && !light.active() &&
                    !light.visible_in_hierarchy() && light.instance_id() == identity,
                "parent suspension preserves child flags and component identity"))
    {
        return false;
    }
    if (!light.set_enabled(false))
    {
        return false;
    }
    if (!parent.set_enabled(true))
    {
        return false;
    }
    if (!expect(!light.active() && child.active(),
                "reactivation does not overwrite the child's explicitly disabled component"))
    {
        return false;
    }
    if (!child.set_visible(false))
    {
        return false;
    }
    if (!parent.set_enabled(false))
    {
        return false;
    }
    engine::schema::SceneDataT snapshot;
    if (!expect(scene->build_data(snapshot), "snapshot local hierarchy and component flags"))
    {
        return false;
    }
    const auto child_id = child.local_id();
    if (!expect(add_scene_resource(fixture, "state.scene", snapshot) &&
                    (fixture.publish(*resources) && services.prepare_builtins()),
                "publish hierarchy state snapshot"))
    {
        return false;
    }
    scene = load_and_restore(services, world, "state.scene");
    if (!expect(scene != nullptr, "restore flags through the scene resource"))
    {
        return false;
    }
    child = {};
    for (const auto& node : test_support::scene_nodes(*scene))
    {
        if (node.local_id() == child_id)
        {
            child = node;
            break;
        }
    }
    light = child ? child.get_component<engine::Light>() : engine::Light{};
    if (!expect(light && light.instance_id() != identity && !child.visible() && !child.active() &&
                    !light.enabled() && !light.visible(),
                "restoration preserves local flags and allocates fresh component identity"))
    {
        return false;
    }
    if (!expect(child.set_parent({}) && child.active() && !light.active(),
                "reparenting immediately recomputes inherited activation"))
    {
        return false;
    }
    const auto old = light.instance_id();
    if (!child.remove_component(engine::Light::component_type))
    {
        return false;
    }
    light = child.add_component<engine::Light>();
    return expect(light && light.instance_id() != old && light.enabled() && light.visible(),
                  "replacing the component does not resurrect old identity or local flags");
}

bool scene_lifecycle(test_support::ResourceServices& services)
{
    const engine::EngineConfig config;
    engine::WorldSystem world(&services.loader);
    if (!expect(world.create_scene("Before init") == nullptr,
                "scene creation requires initialized world") ||
        !expect(world.initialize(config), "initialize world"))
    {
        return false;
    }
    engine::Scene* scene = world.create_scene("First");
    if (!expect(scene != nullptr && scene->name() == "First" && world.scene() == scene,
                "world owns named scene") ||
        !expect(world.create_scene("Duplicate") == nullptr, "reject second active scene") ||
        !expect(test_support::scene_nodes(*scene).empty() && !scene->active_camera(),
                "scene starts with no hidden objects"))
    {
        return false;
    }
    const std::uint64_t first_id = scene->id();
    engine::Node original = scene->create_node("Original");
    if (!expect(original.valid(), "create flecs-backed node"))
    {
        return false;
    }
    const std::uint64_t original_id = original.id();
    if (!expect(scene->find_node(original_id) == original, "lookup complete node identity") ||
        !expect(scene->destroy_node(original_id), "delete node") ||
        !expect(!scene->destroy_node(original_id) && !scene->find_node(original_id),
                "deleted node identity must stop resolving"))
    {
        return false;
    }
    engine::Node replacement = scene->create_node("Replacement");
    if (!expect(replacement.valid() && replacement.id() != original_id,
                "recycled entity slots have new generation") ||
        !expect(world.tick(1.0 / 60.0), "world with scene advances ECS"))
    {
        return false;
    }
    world.destroy_scene();
    world.destroy_scene();
    if (!expect(world.scene() == nullptr, "destroy scene is idempotent"))
    {
        return false;
    }
    scene = world.create_scene("Second");
    if (!expect(scene != nullptr && scene->id() > first_id, "scene replacement changes identity"))
    {
        return false;
    }
    const std::uint64_t second_id = scene->id();
    world.shutdown();
    world.shutdown();
    if (!expect(world.scene() == nullptr, "world shutdown deletes scene before flecs") ||
        !expect(world.initialize(config), "world can initialize again"))
    {
        return false;
    }
    scene = world.create_scene("Third");
    return expect(scene != nullptr && scene->id() > second_id,
                  "scene identities survive world reinitialization");
}

bool hierarchy_and_transforms(test_support::ResourceServices& services)
{
    const engine::EngineConfig config;
    engine::WorldSystem world(&services.loader);
    engine::WorldSystem foreign_world;
    if (!expect(world.initialize(config) && foreign_world.initialize(config),
                "initialize independent worlds"))
    {
        return false;
    }
    engine::Scene* scene = world.create_scene("Hierarchy");
    engine::Scene* foreign_scene = foreign_world.create_scene("Foreign");
    if (!expect(scene != nullptr && foreign_scene != nullptr, "create independent scenes"))
    {
        return false;
    }
    engine::Node parent = scene->create_node("Parent");
    engine::Node child = scene->create_node("Child");
    engine::Node grandchild = scene->create_node("Grandchild");
    engine::Node sibling = scene->create_node("Unrelated");
    engine::Node foreign = foreign_scene->create_node("Foreign node");
    if (!expect(parent.valid() && child.valid() && grandchild.valid() && sibling.valid() &&
                    foreign.valid(),
                "create hierarchy nodes") ||
        !expect(child.set_parent(parent) && grandchild.set_parent(child), "attach child chain") ||
        !expect(!parent.set_parent(grandchild) && !parent.parent(),
                "reject cycles without changing parent") ||
        !expect(!child.set_parent(child), "reject self parent") ||
        !expect(!child.set_parent(foreign) && child.parent() == parent,
                "reject cross-scene hierarchy"))
    {
        return false;
    }
    engine::TransformSettings parent_transform;
    parent_transform.position = {1.0, 2.0, 3.0};
    parent_transform.rotation.mutate_z(90.0);
    parent_transform.scale.mutate_x(2.0);
    engine::TransformSettings child_transform;
    child_transform.position.mutate_x(1.0);
    if (!expect(parent.get_component<engine::Transform>().set_settings(parent_transform) &&
                    child.get_component<engine::Transform>().set_settings(child_transform),
                "set parent and local child transforms"))
    {
        return false;
    }
    std::array<float, 16> matrix{};
    if (!expect(child.get_component<engine::Transform>().world_matrix(matrix.data()),
                "compute world matrix") ||
        !expect(near(matrix[12], 1.0F) && near(matrix[13], 4.0F) && near(matrix[14], 3.0F),
                "world translation applies parent rotation and nonuniform scale") ||
        !expect(near(matrix[0], 0.0F) && near(matrix[1], 2.0F) && near(matrix[4], -1.0F),
                "matrix is column major and uses T R S order"))
    {
        return false;
    }
    engine::TransformSettings invalid = child_transform;
    invalid.scale.mutate_y(0.0);
    if (!expect(
            !child.get_component<engine::Transform>().set_settings(invalid) &&
                test_support::read_settings(child.get_component<engine::Transform>()).scale.y() ==
                    1.0,
            "zero scale rejected without altering transform"))
    {
        return false;
    }
    invalid = child_transform;
    invalid.position.mutate_y(std::numeric_limits<double>::quiet_NaN());
    if (!expect(!child.get_component<engine::Transform>().set_settings(invalid) &&
                    test_support::read_settings(child.get_component<engine::Transform>())
                            .position.y() == 0.0,
                "nonfinite transform rejected without altering transform") ||
        !expect(!child.get_component<engine::Transform>().world_matrix(nullptr),
                "null matrix output rejected") ||
        !expect(child.set_parent({}), "detach preserves local transform") ||
        !expect(child.get_component<engine::Transform>().world_matrix(matrix.data()) &&
                    near(matrix[12], 1.0F) && near(matrix[13], 0.0F),
                "detached world matrix uses local coordinates"))
    {
        return false;
    }
    invalid = child_transform;
    invalid.scale.mutate_x(std::numeric_limits<double>::max());
    if (!expect(child.get_component<engine::Transform>().set_settings(invalid),
                "finite transform may exceed GPU precision") ||
        !expect(!child.get_component<engine::Transform>().world_matrix(matrix.data()),
                "reject float matrix overflow"))
    {
        return false;
    }
    for (std::size_t index = 0; index < matrix.size(); ++index)
    {
        if (!expect(matrix[index] == (index % 5 == 0 ? 1.0F : 0.0F),
                    "failed matrix output remains complete identity"))
        {
            return false;
        }
    }
    child_transform.scale.mutate_z(-1.0);
    if (!expect(child.get_component<engine::Transform>().set_settings(child_transform) &&
                    child.set_parent(parent),
                "negative scale allowed and child can reattach"))
    {
        return false;
    }
    engine::Camera camera = grandchild.add_component<engine::Camera>();
    if (!expect(camera.valid() && !scene->active_camera() && scene->set_active_camera(camera),
                "select active camera explicitly"))
    {
        return false;
    }
    const std::uint64_t parent_id = parent.id();
    const std::uint64_t child_id = child.id();
    const std::uint64_t grandchild_id = grandchild.id();
    const std::uint64_t sibling_id = sibling.id();
    return expect(scene->destroy_node(parent_id), "destroy hierarchy root") &&
           expect(!scene->find_node(parent_id) && !scene->find_node(child_id) &&
                      !scene->find_node(grandchild_id),
                  "root deletion destroys all descendants") &&
           expect(!scene->active_camera() && test_support::scene_nodes(*scene).size() == 1 &&
                      scene->find_node(sibling_id) == sibling,
                  "subtree deletion clears active camera and preserves unrelated nodes");
}

// Use the public resource boundary so restoration still runs through WorldSystem's candidate scene.
bool add_scene_resource(test_support::ResourceFixture& fixture, std::string_view path,
                        const engine::schema::SceneDataT& data)
{
    using namespace engine;
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishSceneDataBuffer(builder, schema::SceneData::Pack(builder, &data));
    auto entry = std::make_unique<schema::AssetEntryT>();
    std::array<char, 37> guid{};
    std::snprintf(guid.data(), guid.size(), "40000000-0000-4000-8000-%012zu",
                  fixture.catalog.entries.size() + 1);
    entry->guid = guid.data();
    entry->path = path;
    entry->kind = schema::AssetKind::scene;
    entry->dependencies = test_support::scene_dependencies(data.nodes);
    ByteBuffer entity;
    const ByteView payload(reinterpret_cast<const std::byte*>(builder.GetBufferPointer()),
                           builder.GetSize());
    if (!ResourceSystem::encode_resource(entry->guid, entry->kind, payload, entity,
                                         entry->header_size))
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
    std::ofstream stream(fixture.root / entry->location->path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(entity.data()),
                 static_cast<std::streamsize>(entity.size()));
    stream.close();
    if (!stream)
    {
        return false;
    }
    fixture.catalog.entries.push_back(std::move(entry));
    return true;
}

bool indexed_scene_restore(test_support::ResourceServices& services)
{
    using namespace engine;
    test_support::ResourceFixture fixture;
    auto* resources = &services.core;
    WorldSystem world(&services.loader);
    if (!resources || !world.initialize({}))
    {
        return false;
    }
    auto* original = world.create_scene("Indexed nodes");
    if (!original)
    {
        return false;
    }
    for (const char* name : {"Leaf", "Root", "Branch", "Spare"})
    {
        if (!original->create_node(name))
        {
            return false;
        }
    }
    auto camera = test_support::scene_nodes(*original)[0].add_component<Camera>();
    schema::SceneDataT source;
    source.schema_version = 3;
    if (!camera || !original->set_active_camera(camera) || !original->build_data(source))
    {
        return false;
    }
    // Parents follow children; consecutive resource IDs also collide with naive temporary IDs.
    const std::array<std::uint64_t, 4> ids{42, 1, 2, 10000};
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        source.nodes[i]->info->properties->id = ids[i];
    }
    source.nodes[0]->info->parent_id = 2;
    source.nodes[2]->info->parent_id = 1;
    source.info->active_camera_node = 42;
    if (!add_scene_resource(fixture, "indexed.scene", source) ||
        !(fixture.publish(*resources) && services.prepare_builtins()))
    {
        return false;
    }
    auto* loaded = load_and_restore(services, world, "indexed.scene");
    if (!expect(loaded && test_support::scene_nodes(*loaded).size() == ids.size(),
                "restore sparse unordered IDs"))
    {
        return false;
    }
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        auto node = test_support::scene_nodes(*loaded)[i];
        if (!expect(node.local_id() == ids[i] && loaded->find_node(node.id()) == node &&
                        std::as_const(*loaded).find_node(node.id()) == node &&
                        std::as_const(*loaded).find_local_node(ids[i]) == node &&
                        loaded->find_node(node.name()) == node,
                    "resource order and both const/mutable runtime lookups survive restoration"))
        {
            return false;
        }
    }
    auto leaf = test_support::scene_nodes(*loaded)[0];
    auto root = test_support::scene_nodes(*loaded)[1];
    auto branch = test_support::scene_nodes(*loaded)[2];
    auto spare = test_support::scene_nodes(*loaded)[3];
    if (!expect(leaf.parent() == branch && branch.parent() == root && !root.parent() &&
                    !spare.parent() && loaded->active_camera() == leaf.get_component<Camera>() &&
                    !loaded->find_node(0),
                "local identity index resolves parents and camera independently of runtime IDs"))
    {
        return false;
    }
    camera = loaded->active_camera();
    for (unsigned failure = 0; failure < 5; ++failure)
    {
        auto invalid = source;
        if (failure == 0)
        {
            invalid.nodes[3]->info->properties->id = 2;
        }
        else if (failure == 1)
        {
            invalid.nodes[1]->info->parent_id = 42;
        }
        else if (failure == 2)
        {
            invalid.nodes[3]->info->parent_id = 999;
        }
        else if (failure == 3)
        {
            invalid.info->active_camera_node = 10000;
        }
        else
        {
            invalid.nodes[3]->components.clear();
        }
        const auto path = "invalid-index-" + std::to_string(failure) + ".scene";
        if (!add_scene_resource(fixture, path, invalid) ||
            !(fixture.publish(*resources) && services.prepare_builtins()) ||
            !expect(!load_and_restore(services, world, path) && world.scene() == loaded &&
                        loaded->find_node(leaf.id()) == leaf && leaf.parent() == branch &&
                        loaded->active_camera() == camera,
                    "failed candidate cleans its indices without changing the active scene"))
        {
            return false;
        }
    }
    std::vector<Node> survivors{spare};
    std::vector<std::uint64_t> removed{leaf.id(), root.id(), branch.id()};
    for (std::size_t i = 0; i < 256; ++i)
    {
        auto node = loaded->create_node("Indexed growth");
        if (!expect(node && node.local_id() == 10001 + i && !node.parent(),
                    "new local IDs advance from the restored maximum"))
        {
            return false;
        }
        if (i % 2 == 0)
        {
            survivors.push_back(node);
        }
        else
        {
            if (!node.set_parent(branch))
            {
                return false;
            }
            removed.push_back(node.id());
        }
    }
    auto unrelated = loaded->create_node("Unrelated root");
    if (!expect(unrelated && loaded->destroy_node(unrelated.id()) &&
                    loaded->active_camera() == camera,
                "deleting another root keeps the indexed active camera"))
    {
        return false;
    }
    if (!expect(leaf.parent() == branch && loaded->destroy_node(root.id()) &&
                    !loaded->active_camera() &&
                    test_support::scene_nodes(*loaded).size() == survivors.size(),
                "subtree deletion works after index growth with interleaved surviving nodes"))
    {
        return false;
    }
    for (auto id : removed)
    {
        if (!expect(!loaded->find_node(id),
                    "deleted descendants cannot resolve through stale indices"))
        {
            return false;
        }
    }
    for (std::size_t i = 0; i < survivors.size(); ++i)
    {
        if (!expect(test_support::scene_nodes(*loaded)[i] == survivors[i] &&
                        loaded->find_node(survivors[i].id()) == survivors[i] &&
                        !survivors[i].parent(),
                    "stable erase preserves remaining node order, addresses and root identity"))
        {
            return false;
        }
    }
    loaded = load_and_restore(services, world, "indexed.scene");
    if (!expect(loaded && !spare && loaded->find_local_node(ids[3]) &&
                    loaded->find_local_node(ids[3]).id() != spare.id(),
                "restoring the same local identity creates a fresh entity instead of reviving a "
                "handle"))
    {
        return false;
    }
    source.nodes[0]->info->properties->id = std::numeric_limits<std::uint64_t>::max();
    source.info->active_camera_node = source.nodes[0]->info->properties->id;
    if (!add_scene_resource(fixture, "index-limit.scene", source) ||
        !(fixture.publish(*resources) && services.prepare_builtins()))
    {
        return false;
    }
    loaded = load_and_restore(services, world, "index-limit.scene");
    return expect(loaded && test_support::scene_nodes(*loaded).size() == ids.size() &&
                      loaded->active_camera() &&
                      loaded->find_local_node(std::numeric_limits<std::uint64_t>::max()) ==
                          test_support::scene_nodes(*loaded)[0] &&
                      test_support::scene_nodes(*loaded)[0].parent() ==
                          test_support::scene_nodes(*loaded)[2] &&
                      !loaded->create_node("Cannot wrap ID"),
                  "maximum local ID may precede other nodes; automatic allocation never wraps");
}

bool active_camera_lifecycle(test_support::ResourceServices& services)
{
    using namespace engine;
    WorldSystem world(&services.loader);
    test_support::ResourceFixture fixture;
    auto* resources = &services.core;
    if (!resources || !world.initialize({}))
    {
        return false;
    }
    auto* scene = world.create_scene("Camera lifecycle");
    auto node = scene ? scene->create_node("Camera") : Node{};
    auto camera = node.add_component<Camera>();
    schema::SceneDataT saved;
    if (!camera || !scene->set_active_camera(camera) ||
        !node.remove_component(Camera::component_type) ||
        !expect(!camera && !scene->active_camera() && scene->build_data(saved) && saved.info &&
                    saved.info->active_camera_node == 0,
                "removing the selected Camera clears the saved selection"))
    {
        return false;
    }
    auto replacement = node.add_component<Camera>();
    if (!expect(replacement && !scene->active_camera() && !scene->set_active_camera(camera),
                "a replacement Camera neither revives the old handle nor selects itself") ||
        !scene->set_active_camera(replacement) || !replacement.set_transient(true) ||
        !expect(!scene->build_data(saved) && !saved.info && saved.nodes.empty(),
                "an omitted active Camera rejects the complete snapshot") ||
        !replacement.set_transient(false) || !scene->build_data(saved) ||
        !add_scene_resource(fixture, "camera-lifecycle.scene", saved) ||
        !(fixture.publish(*resources) && services.prepare_builtins()))
    {
        return false;
    }
    auto* restored = load_and_restore(services, world, "camera-lifecycle.scene");
    if (!expect(restored && restored->active_camera() &&
                    restored->active_camera().node().name() == "Camera",
                "the surviving active Camera round-trips through resource restoration"))
    {
        return false;
    }
    node = restored->active_camera().node();
    if (!node.remove_component(Camera::component_type) || !restored->build_data(saved) ||
        !add_scene_resource(fixture, "removed-camera.scene", saved) ||
        !(fixture.publish(*resources) && services.prepare_builtins()))
    {
        return false;
    }
    restored = load_and_restore(services, world, "removed-camera.scene");
    return expect(restored && !restored->active_camera(),
                  "a scene saved after Camera removal remains restorable");
}

bool incremental_scene_restore(test_support::ResourceServices& services)
{
    using namespace engine;
    test_support::ResourceFixture fixture;
    auto* resources = &services.core;
    WorldSystem world(&services.loader, {2, 1.0});
    if (!resources || !world.initialize({}))
    {
        return false;
    }
    auto* active = world.create_scene("Keep until commit");
    auto child = active ? active->create_node("Child") : engine::Node{};
    auto parent = active ? active->create_node("Parent") : engine::Node{};
    schema::SceneDataT data;
    data.schema_version = 3;
    if (!child || !parent || !child.set_parent(parent) ||
        !active->set_active_camera(child.add_component<Camera>()) || !active->build_data(data) ||
        !add_scene_resource(fixture, "staged.scene", data) ||
        !(fixture.publish(*resources) && services.prepare_builtins()) || !world.set_paused(true))
    {
        return false;
    }
    engine::ResourceReference resource;
    if (!services.load("staged.scene", resource))
    {
        return false;
    }
    SceneScopeId cancelled;
    if (!world.begin_scope(SceneLoadKind::replace_scene, 0, cancelled) ||
        !world.restore_scope(cancelled, resource.handle()) ||
        !expect(world.step_scope(cancelled, {0, 0.001}) == SceneWorkState::working &&
                    world.step_scope(cancelled, {64, -1.0}) == SceneWorkState::failed &&
                    !world.publish_scope(cancelled) && world.scene() == active,
                "invalid budget and premature publication preserve the active scene") ||
        !world.cancel_scope(cancelled))
    {
        return false;
    }
    world.release_scope(cancelled);
    SceneScopeId request;
    if (!world.begin_scope(SceneLoadKind::replace_scene, 0, request) ||
        !world.restore_scope(request, resource.handle()) ||
        !expect(world.step_scope(cancelled, {}) == SceneWorkState::failed &&
                    world.step_scope(request, {100, 1.0}) == SceneWorkState::working &&
                    world.paused(),
                "cancelled identity cannot advance a later candidate"))
    {
        return false;
    }
    SceneWorkState state = SceneWorkState::working;
    for (int tick = 0; tick < 32 && state == SceneWorkState::working; ++tick)
    {
        state = world.step_scope(request, {100, 1.0});
    }
    auto* candidate = world.scope_candidate(request);
    if (!expect(state == SceneWorkState::ready && candidate && candidate->active_camera() &&
                    test_support::scene_nodes(*candidate).size() == 2 &&
                    !world.find_node(candidate->active_camera().node().id()),
                "restoration stages complete hierarchy without exposing candidate nodes") ||
        !world.seal_scope(request))
    {
        return false;
    }
    state = SceneWorkState::working;
    for (int tick = 0; tick < 32 && state == SceneWorkState::working; ++tick)
    {
        state = world.step_scope(request, {100, 1.0});
    }
    if (!expect(state == SceneWorkState::ready && world.publish_scope(request) &&
                    world.scene() == candidate && !world.paused() && world.activate_scope(request),
                "prepared scope publishes and activates explicitly"))
    {
        return false;
    }
    world.shutdown();
    return expect(!world.scope_candidate(request), "shutdown invalidates all scope identities");
}

bool persistent_nodes(test_support::ResourceServices& services)
{
    using namespace engine;
    WorldSystem world(&services.loader);
    if (!world.initialize({}))
    {
        return false;
    }
    auto* scene = world.create_scene("Persistent hierarchy");
    auto child = scene ? scene->create_node("Created before root") : engine::Node{};
    auto ordinary = scene ? scene->create_node("Ordinary") : engine::Node{};
    auto root = scene ? scene->create_node("Persistent root") : engine::Node{};
    auto leaf = scene ? scene->create_node("Leaf") : engine::Node{};
    if (!child || !ordinary || !root || !leaf || !child.set_parent(root) || !leaf.set_parent(child))
    {
        return false;
    }
    const auto scene_id = scene->id();
    const auto root_id = root.id();
    const auto child_id = child.id();
    const auto leaf_id = leaf.id();
    const auto ordinary_id = ordinary.id();
    const auto root_local_id = root.local_id();
    auto transform = root.get_component<Transform>();
    if (!expect(!root.persistent() && root.set_persistent(true) && root.set_persistent(true) &&
                    child.persistent() && leaf.persistent() && !child.set_persistent(false),
                "only roots can set persistence; repeated calls preserve the complete subtree") ||
        !expect(!root.set_parent(ordinary) && !root.parent() && !root.transient(),
                "marked root cannot be reparented and persistence does not imply transient") ||
        !world.set_paused(true))
    {
        return false;
    }
    world.unload_scene();
    world.unload_scene();
    if (!expect(!world.scene() && !world.paused() && world.scenes().size() == 1 &&
                    world.find_node(root_id) == root && world.find_node(child_id) == child &&
                    std::as_const(world).find_node(leaf_id) == leaf &&
                    !world.find_node(ordinary_id) && root.scene() == scene &&
                    root.local_id() == root_local_id &&
                    root.get_component<Transform>() == transform && child.parent() == root &&
                    leaf.parent() == child,
                "unload preserves original owners and identities, including child-before-parent"))
    {
        return false;
    }
    auto* next = world.create_scene("Next");
    auto next_node = next ? next->create_node("Next root") : engine::Node{};
    if (!next_node ||
        !expect(!child.set_parent(next_node), "cross-scene parenting stays rejected") ||
        !expect(child.set_parent({}) && !child.persistent() && !leaf.persistent(),
                "detached branch no longer inherits persistence"))
    {
        return false;
    }
    world.unload_scene();
    if (!expect(world.find_node(root_id) == root && !world.find_node(child_id) &&
                    !world.find_node(leaf_id) && world.scenes().size() == 1,
                "next unload removes detached branch and ordinary next scene") ||
        !expect(root.set_persistent(false) && world.find_node(root_id) == root,
                "unmark does not immediately destroy a node"))
    {
        return false;
    }
    world.unload_scene();
    if (!expect(world.scenes().empty() && !world.find_node(root_id) &&
                    !world.destroy_scene(scene_id),
                "unmarked roots and their empty retained scene are reclaimed"))
    {
        return false;
    }
    auto* retained_scene = world.create_scene("Retained");
    auto retained = retained_scene ? retained_scene->create_node("Retained") : engine::Node{};
    if (!retained || !retained.set_persistent(true))
    {
        return false;
    }
    const auto retained_scene_id = retained_scene->id();
    world.unload_scene();
    auto* active = world.create_scene("Other owner");
    if (!active || !world.set_paused(true) ||
        !expect(world.destroy_scene(retained_scene_id) && world.scene() == active && world.paused(),
                "precise cleanup does not clear another scene or its pause state"))
    {
        return false;
    }
    auto explicit_root = active->create_node("Explicit destroy");
    if (!explicit_root || !explicit_root.set_persistent(true))
    {
        return false;
    }
    const auto explicit_id = explicit_root.id();
    world.unload_scene();
    if (!expect(active->destroy_node(explicit_id) && !world.find_node(explicit_id) &&
                    world.tick(0) && world.scenes().empty(),
                "explicit node destruction reclaims empty container at the next world update"))
    {
        return false;
    }
    active = world.create_scene("Shutdown");
    auto last = active ? active->create_node("Last") : engine::Node{};
    if (!last || !last.set_persistent(true))
    {
        return false;
    }
    const auto last_id = last.id();
    world.unload_scene();
    world.destroy_scene();
    world.destroy_scene();
    return expect(world.scenes().empty() && !world.find_node(last_id),
                  "complete cleanup releases persistent nodes even without an active scene");
}

bool persistent_scene_loading(test_support::ResourceServices& services)
{
    using namespace engine;
    test_support::ResourceFixture fixture;
    auto* resources = &services.core;
    WorldSystem world(&services.loader, {64, 1.0});
    auto* source = resources && world.initialize({}) ? world.create_scene("Source") : nullptr;
    auto node = source ? source->create_node("Resource owner") : engine::Node{};
    schema::SceneDataT data;
    data.schema_version = 3;
    if (!node || !source->build_data(data) ||
        !add_scene_resource(fixture, "persistent.scene", data))
    {
        return false;
    }
    data.info->name = "Replacement";
    if (!add_scene_resource(fixture, "replacement.scene", data) ||
        !(fixture.publish(*resources) && services.prepare_builtins()))
    {
        return false;
    }
    engine::ResourceReference resource;
    if (!services.load("persistent.scene", resource))
    {
        return false;
    }
    const auto originally_loaded = [&]()
    {
        engine::ResourceReference current;
        return current.acquire(&services.loader, "persistent.scene");
    };
    const auto replace = [&](ResourceReference& value) -> Scene*
    {
        SceneScopeId id;
        if (!world.begin_scope(SceneLoadKind::replace_scene, 0, id) ||
            !world.restore_scope(id, value.handle()))
        {
            return nullptr;
        }
        for (int i = 0; i < 32 && world.step_scope(id, {64, 1.0}) == SceneWorkState::working; ++i)
        {
        }
        if (!world.seal_scope(id))
        {
            return nullptr;
        }
        for (int i = 0; i < 32 && world.step_scope(id, {64, 1.0}) == SceneWorkState::working; ++i)
        {
        }
        if (!world.publish_scope(id) || !world.activate_scope(id) || !world.tick(0))
        {
            return nullptr;
        }
        return world.scene();
    };
    auto* loaded = replace(resource);
    auto persistent = loaded ? test_support::scene_nodes(*loaded).front() : Node{};
    if (!persistent || !persistent.set_persistent(true))
    {
        return false;
    }
    resource.reset();
    if (!expect(originally_loaded(), "published scene owns its loaded root resource") ||
        !services.load("replacement.scene", resource))
    {
        return false;
    }
    SceneScopeId cancelled;
    if (!world.begin_scope(SceneLoadKind::replace_scene, 0, cancelled) ||
        !world.restore_scope(cancelled, resource.handle()) || !world.cancel_scope(cancelled) ||
        !expect(!world.publish_scope(cancelled) && world.scene() == loaded && persistent,
                "cancelled scope retains the active scene and persistent handles"))
    {
        return false;
    }
    world.release_scope(cancelled);
    auto* next = replace(resource);
    if (!expect(next && next != loaded && persistent &&
                    world.find_node(persistent.id()) == persistent && !originally_loaded() &&
                    persistent.set_enabled(true),
                "replacement preserves writable persistent nodes and releases the old scene root"))
    {
        return false;
    }
    SceneScopeId pending;
    if (!world.begin_scope(SceneLoadKind::replace_scene, 0, pending))
    {
        return false;
    }
    world.unload_scene();
    return expect(!world.scope_candidate(pending) && !world.scene() && persistent,
                  "explicit unload cancels pending scopes and retains persistent subtrees");
}

bool component_contracts(test_support::ResourceServices& services)
{
    const engine::EngineConfig config;
    engine::WorldSystem world(&services.loader);
    engine::WorldSystem foreign_world;
    if (!expect(world.initialize(config) && foreign_world.initialize(config),
                "initialize component worlds"))
    {
        return false;
    }
    engine::Scene* scene = world.create_scene("Components");
    engine::Scene* foreign_scene = foreign_world.create_scene("Foreign components");
    if (!expect(scene != nullptr && foreign_scene != nullptr, "create component scenes"))
    {
        return false;
    }
    engine::Node node = scene->create_node("Combined node");
    engine::Node foreign_node = foreign_scene->create_node("Foreign camera");
    if (!expect(node.valid() && foreign_node.valid(), "create component owners"))
    {
        return false;
    }
    if (!expect(node.get_component<engine::Transform>().valid() && components_of(node).size() == 1,
                "new scene node owns a Transform through the generic component list") ||
        !expect(!node.get_component<engine::Camera>() &&
                    !node.get_component(engine::Camera::component_type),
                "generic typed and untyped lookup return null for missing component") ||
        !expect(!node.add_component<engine::Transform>(),
                "duplicate Transform is rejected through the same generic method"))
    {
        return false;
    }
    engine::Camera camera = node.add_component<engine::Camera>();
    engine::Light light = node.add_component<engine::Light>();
    engine::MeshRenderer mesh = node.add_component<engine::MeshRenderer>();
    engine::Camera foreign_camera = foreign_node.add_component<engine::Camera>();
    if (!expect(camera.valid() && light.valid() && mesh.valid() && foreign_camera.valid(),
                "different components can share a node") ||
        !expect(!node.add_component<engine::Camera>() && !node.add_component<engine::Light>() &&
                    !node.add_component<engine::MeshRenderer>(),
                "duplicate component type rejected") ||
        !expect(camera.node() == node && light.node() == node && mesh.node() == node,
                "components borrow their owner") ||
        !expect(camera.type() == engine::ComponentType::CameraData &&
                    light.type() == engine::ComponentType::LightData &&
                    mesh.type() == engine::ComponentType::MeshRendererData,
                "component types are explicit") ||
        !expect(scene->set_active_camera(camera) && !scene->set_active_camera(foreign_camera) &&
                    scene->active_camera() == camera,
                "active camera must belong to scene"))
    {
        return false;
    }
    const engine::Node& read_only_node = node;
    if (!expect(node.get_component(engine::Camera::component_type) == camera &&
                    read_only_node.get_component<engine::Camera>() == camera &&
                    components_of(read_only_node).size() == 4,
                "one generic list provides const typed and untyped access"))
    {
        return false;
    }
    engine::CameraSettings camera_settings;
    camera_settings.fov_degrees = 75.0F;
    camera_settings.near_clip = 0.2F;
    camera_settings.far_clip = 50.0F;
    if (!expect(camera.set_settings(camera_settings), "configure camera"))
    {
        return false;
    }
    camera_settings.far_clip = 0.1F;
    if (!expect(!camera.set_settings(camera_settings) &&
                    test_support::read_settings(camera).far_clip == 50.0F,
                "invalid clipping plane leaves old camera settings"))
    {
        return false;
    }
    engine::LightSettings light_settings;
    light_settings.intensity = 4.0F;
    if (!expect(light.set_settings(light_settings), "configure light"))
    {
        return false;
    }
    light_settings.color.mutate_r(std::numeric_limits<float>::infinity());
    if (!expect(!light.set_settings(light_settings) &&
                    test_support::read_settings(light).color.r() == 1.0F,
                "invalid light color leaves old settings"))
    {
        return false;
    }
    engine::Material material = mesh.material();
    float default_metallic = -1.0F;
    float default_roughness = -1.0F;
    float default_workflow = -1.0F;
    if (!expect(material.type_path() == "builtin://materials/default-pbr" &&
                    material.shader_path() == "builtin://shaders/pbr" &&
                    material.get_float("metallic", default_metallic) && default_metallic == 0.0F &&
                    material.get_float("roughness", default_roughness) &&
                    near(default_roughness, 0.4F) &&
                    material.get_float("specularWorkflow", default_workflow) &&
                    default_workflow == 0.0F,
                "new mesh starts with the standard metallic-roughness PBR material and shader"))
    {
        return false;
    }
    if (!expect(mesh.set_material_count(2), "add an independent default material slot"))
    {
        return false;
    }
    const auto added_material = mesh.material(1);
    if (!expect(added_material.type_path() == "builtin://materials/default-pbr" &&
                    added_material.shader_path() == "builtin://shaders/pbr" &&
                    added_material.get_float("metallic", default_metallic) &&
                    default_metallic == 0.0F &&
                    added_material.get_float("roughness", default_roughness) &&
                    near(default_roughness, 0.4F) &&
                    added_material.get_float("specularWorkflow", default_workflow) &&
                    default_workflow == 0.0F && mesh.set_material_count(1),
                "additional material slots also start with metallic-roughness PBR"))
    {
        return false;
    }
    if (!expect(material.type_path() == "builtin://materials/default-pbr" &&
                    material.set_float("metallic", 0.6F) && material.set_float("roughness", 0.2F) &&
                    material.set_v4("baseColor", engine::Vec4(0.7, 0.3, 0.15, 1.0)) &&
                    mesh.mesh_path() == "builtin://meshes/cube",
                "configure built-in cube material by named typed parameters"))
    {
        return false;
    }
    float roughness = 0.0F;
    if (!expect(!material.set_float("roughness", -1.0F) &&
                    material.get_float("roughness", roughness) && roughness == 0.2F,
                "invalid material value leaves old parameter"))
    {
        return false;
    }
    float hex_enabled = -1;
    if (!expect(
            material.get_float("hexTilingEnabled", hex_enabled) && hex_enabled == 0 &&
                !material.set_float("hexTilingEnabled", 0.5F) &&
                !material.set_float("hexTilingContrast", 0) &&
                !material.set_float("hexTilingContrast", 1) &&
                material.set_float("hexTilingEnabled", 1) &&
                material.set_float("hexTilingRotation", 0.75F) &&
                !material.set_float("alphaCutoff", 0.5F) &&
                !material.set_float("specularWorkflow", 1) &&
                !material.set_blend_mode(engine::schema::BlendMode::alpha_blend) &&
                material.blend_mode() == engine::schema::BlendMode::opaque &&
                material.get_float("hexTilingEnabled", hex_enabled) && hex_enabled == 1 &&
                material.set_float("hexTilingEnabled", 0),
            "Hex is opt-in, rejects unsupported PBR combinations atomically and can be disabled"))
    {
        return false;
    }
    float cutoff = -1.0F;
    float highlight = -1.0F;
    if (!expect(material.set_shader("builtin://shaders/blinn-phong") &&
                    material.get_float("specularHighlights", highlight) && highlight == 1.0F &&
                    material.set_float("smoothnessSource", 1.0F) &&
                    material.set_float("specularHighlights", 0.0F) &&
                    !material.set_float("specularHighlights", 2.0F) &&
                    !material.set_float("lightingMode", 0.0F),
                "Blinn-Phong is selected by shader path with named material parameters"))
    {
        return false;
    }
    if (!expect(material.blend_mode() == engine::schema::BlendMode::opaque &&
                    material.set_blend_mode(engine::schema::BlendMode::alpha_blend) &&
                    !material.set_blend_mode(static_cast<engine::schema::BlendMode>(255)) &&
                    material.blend_mode() == engine::schema::BlendMode::alpha_blend &&
                    material.cull_mode() == engine::schema::CullMode::back &&
                    material.get_float("alphaCutoff", cutoff) && cutoff == 0.0F &&
                    material.set_cull_mode(engine::schema::CullMode::none) &&
                    !material.set_cull_mode(static_cast<engine::schema::CullMode>(255)) &&
                    material.cull_mode() == engine::schema::CullMode::none &&
                    material.set_float("alphaCutoff", 0.5F) &&
                    !material.set_float("alphaCutoff", 1.1F) &&
                    material.get_float("alphaCutoff", cutoff) && cutoff == 0.5F &&
                    material.set_shader("builtin://shaders/unlit") &&
                    material.blend_mode() == engine::schema::BlendMode::alpha_blend &&
                    material.cull_mode() == engine::schema::CullMode::none &&
                    material.get_float("alphaCutoff", cutoff) && cutoff == 0.0F &&
                    material.set_type("builtin://materials/default-unlit") &&
                    material.blend_mode() == engine::schema::BlendMode::opaque &&
                    material.cull_mode() == engine::schema::CullMode::back,
                "coverage defaults, invalid states and shader/type changes preserve "
                "their contracts"))
    {
        return false;
    }
    if (!mesh.set_enabled(false))
    {
        return false;
    }
    if (!expect(!mesh.enabled() && node.get_component<engine::MeshRenderer>() == mesh,
                "disable retains component ownership"))
    {
        return false;
    }
    return expect(scene->set_active_camera({}) && !scene->active_camera(),
                  "active camera can be cleared");
}

bool camera_rays(test_support::ResourceServices& services)
{
    using namespace engine;
    const auto vector_near = [](const Vec3& actual, const Vec3& expected)
    {
        return std::abs(actual.x() - expected.x()) < 0.0001 &&
               std::abs(actual.y() - expected.y()) < 0.0001 &&
               std::abs(actual.z() - expected.z()) < 0.0001;
    };
    const EngineConfig config;
    WorldSystem world(&services.loader);
    if (!expect(world.initialize(config), "initialize camera ray world without a window"))
    {
        return false;
    }
    auto* scene = world.create_scene("Camera rays");
    auto parent = scene != nullptr ? scene->create_node("Camera parent") : engine::Node{};
    auto node = scene != nullptr ? scene->create_node("Camera") : engine::Node{};
    auto camera = node.valid() ? node.add_component<Camera>() : Camera{};
    if (!expect(camera.valid() && parent.valid(), "create ray camera"))
    {
        return false;
    }
    CameraSettings settings;
    settings.fov_degrees = 90.0F;
    settings.near_clip = 1.0F;
    settings.far_clip = 100.0F;
    if (!camera.set_enabled(false))
    {
        return false;
    }
    Vec3 origin;
    Vec3 direction;
    if (!expect(camera.set_settings(settings) && !scene->active_camera() &&
                    camera.viewport_point_to_ray({0.5, 0.5}, 2.0, origin, direction) &&
                    vector_near(origin, {0.0, 0.0, -1.0}) &&
                    vector_near(direction, {0.0, 0.0, -1.0}),
                "disabled non-active camera computes a center ray from its near plane"))
    {
        return false;
    }
    const double corner_length = std::sqrt(6.0);
    const Vec3 corner_direction{2.0 / corner_length, 1.0 / corner_length, -1.0 / corner_length};
    if (!expect(camera.viewport_point_to_ray({1.0, 0.0}, 2.0, origin, direction) &&
                    vector_near(origin, {2.0, 1.0, -1.0}) &&
                    vector_near(direction, corner_direction),
                "perspective top right follows vertical field of view and aspect ratio") ||
        !expect(
            camera.screen_point_to_ray({800.0, 0.0}, {800.0, 400.0}, origin, direction) &&
                vector_near(origin, {2.0, 1.0, -1.0}) && vector_near(direction, corner_direction) &&
                camera.screen_point_to_ray({400.0, 0.0}, {400.0, 200.0}, origin, direction) &&
                vector_near(origin, {2.0, 1.0, -1.0}) && vector_near(direction, corner_direction),
            "screen rays agree for matching logical or pixel coordinate units") ||
        !expect(camera.viewport_point_to_ray({1.5, 1.5}, 2.0, origin, direction) &&
                    vector_near(origin, {4.0, -2.0, -1.0}),
                "finite points outside the viewport are extrapolated without clamping"))
    {
        return false;
    }
    settings.projection = schema::Projection::orthographic;
    settings.orthographic_size = 2.0F;
    if (!expect(
            camera.set_settings(settings) &&
                camera.viewport_point_to_ray({0.5, 0.5}, 2.0, origin, direction) &&
                vector_near(origin, {0.0, 0.0, -1.0}) && vector_near(direction, {0.0, 0.0, -1.0}) &&
                camera.screen_point_to_ray({800.0, 0.0}, {800.0, 400.0}, origin, direction) &&
                vector_near(origin, {4.0, 2.0, -1.0}) && vector_near(direction, {0.0, 0.0, -1.0}),
            "orthographic rays shift along the near plane while remaining parallel"))
    {
        return false;
    }
    settings.projection = schema::Projection::perspective;
    TransformSettings parent_settings;
    parent_settings.position = {10.0, 20.0, 30.0};
    parent_settings.scale = {2.0, 3.0, 4.0};
    TransformSettings local_settings;
    local_settings.position = {1.0, 2.0, 3.0};
    local_settings.rotation = {0.0, 45.0, 0.0};
    local_settings.scale = {5.0, 6.0, 7.0};
    const double root_five = std::sqrt(5.0);
    const Vec3 shear_origin{12.0 + 3.0 / root_five, 27.0, 42.0 - 4.0 / root_five};
    const Vec3 shear_direction{3.0 / root_five / corner_length, 1.0 / corner_length,
                               -4.0 / root_five / corner_length};
    if (!expect(camera.set_settings(settings) && node.set_parent(parent) &&
                    parent.get_component<Transform>().set_settings(parent_settings) &&
                    node.get_component<Transform>().set_settings(local_settings) &&
                    camera.viewport_point_to_ray({1.0, 0.0}, 2.0, origin, direction) &&
                    vector_near(origin, shear_origin) && vector_near(direction, shear_direction),
                "world ray uses the rendered orthogonal basis under parent scale and shear"))
    {
        return false;
    }
    local_settings.scale.mutate_z(-7.0);
    if (!expect(node.get_component<Transform>().set_settings(local_settings) &&
                    camera.viewport_point_to_ray({0.5, 0.5}, 2.0, origin, direction) &&
                    vector_near(origin, {12.0 + 1.0 / root_five, 26.0, 42.0 + 2.0 / root_five}) &&
                    vector_near(direction, {1.0 / root_five, 0.0, 2.0 / root_five}),
                "mirrored camera keeps the rendered forward direction"))
    {
        return false;
    }
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    const std::array<Vec2, 5> invalid_sizes = {Vec2{0.0, 100.0}, Vec2{100.0, 0.0},
                                               Vec2{-100.0, 100.0}, Vec2{nan, 100.0},
                                               Vec2{100.0, infinity}};
    for (const Vec2& size : invalid_sizes)
    {
        origin = {1.0, 2.0, 3.0};
        direction = {4.0, 5.0, 6.0};
        if (!expect(!camera.screen_point_to_ray({0.5, 0.5}, size, origin, direction) &&
                        vector_near(origin, {}) && vector_near(direction, {}),
                    "invalid screen dimensions clear both ray outputs"))
        {
            return false;
        }
    }
    for (double aspect : {0.0, -1.0, nan, infinity, std::numeric_limits<double>::max()})
    {
        origin = {1.0, 2.0, 3.0};
        direction = {4.0, 5.0, 6.0};
        if (!expect(!camera.viewport_point_to_ray({0.5, 0.5}, aspect, origin, direction) &&
                        vector_near(origin, {}) && vector_near(direction, {}),
                    "invalid or unrepresentable aspect clears both ray outputs"))
        {
            return false;
        }
    }
    for (const Vec2 point : {Vec2{nan, 0.0}, Vec2{0.0, infinity}})
    {
        origin = {1.0, 2.0, 3.0};
        direction = {4.0, 5.0, 6.0};
        if (!expect(!camera.viewport_point_to_ray(point, 2.0, origin, direction) &&
                        vector_near(origin, {}) && vector_near(direction, {}) &&
                        !camera.screen_point_to_ray(point, {800.0, 400.0}, origin, direction),
                    "both coordinate APIs reject non-finite points"))
        {
            return false;
        }
    }
    local_settings.scale.mutate_x(1e-100);
    origin = {1.0, 2.0, 3.0};
    direction = {4.0, 5.0, 6.0};
    if (!expect(node.get_component<Transform>().set_settings(local_settings) &&
                    !camera.viewport_point_to_ray({0.5, 0.5}, 2.0, origin, direction) &&
                    vector_near(origin, {}) && vector_near(direction, {}),
                "a world axis lost to float underflow is rejected as a degenerate transform"))
    {
        return false;
    }
    local_settings.scale.mutate_x(5.0);
    local_settings.position.mutate_x(std::numeric_limits<double>::max());
    origin = {1.0, 2.0, 3.0};
    direction = {4.0, 5.0, 6.0};
    return expect(node.get_component<Transform>().set_settings(local_settings) &&
                      !camera.viewport_point_to_ray({0.5, 0.5}, 2.0, origin, direction) &&
                      vector_near(origin, {}) && vector_near(direction, {}),
                  "unrepresentable world transform fails without partial ray outputs");
}

bool transient_nodes(test_support::ResourceServices& services)
{
    engine::WorldSystem world(&services.loader);
    if (!world.initialize({}))
    {
        return false;
    }
    auto* scene = world.create_scene("Generated hierarchy");
    auto persistent = scene ? scene->create_node("Source") : engine::Node{};
    auto generated = scene ? scene->create_node("Generated") : engine::Node{};
    auto child = scene ? scene->create_node("Generated child") : engine::Node{};
    if (!expect(persistent && generated && child && child.set_parent(generated) &&
                    !generated.transient() && !child.transient(),
                "nodes start persistent"))
    {
        return false;
    }
    if (!generated.set_transient(true))
    {
        return false;
    }
    if (!child.set_transient(false))
    {
        return false;
    }
    engine::schema::SceneDataT saved;
    if (!expect(generated.transient() && child.transient() && !persistent.transient() &&
                    scene->build_data(saved) && saved.nodes.size() == 1 &&
                    saved.nodes.front()->info->properties->id == persistent.local_id() &&
                    test_support::scene_nodes(*scene).size() == 3,
                "snapshot excludes generated subtree without changing live nodes"))
    {
        return false;
    }
    auto camera = child.add_component<engine::Camera>();
    if (!expect(camera && scene->set_active_camera(camera) && !scene->build_data(saved) &&
                    saved.nodes.empty() && !saved.info,
                "snapshot rejects a transient active camera without leaving partial output"))
    {
        return false;
    }
    if (!generated.set_transient(false))
    {
        return false;
    }
    if (!expect(!child.transient() && scene->build_data(saved) && saved.nodes.size() == 3 &&
                    saved.info->active_camera_node == child.local_id(),
                "clearing parent marker restores descendant persistence and camera identity"))
    {
        return false;
    }
    if (!child.set_transient(true))
    {
        return false;
    }
    if (!scene->set_active_camera({}) || !child.set_parent({}))
    {
        return false;
    }
    return expect(child.transient() && !generated.transient() && scene->build_data(saved) &&
                      saved.nodes.size() == 2,
                  "own transient marker survives reparenting");
}

bool missing_material_resources()
{
    const engine::EngineConfig config;
    engine::WorldSystem world;
    if (!world.initialize(config))
    {
        return false;
    }
    engine::Scene* scene = world.create_scene("Before resource initialization");
    engine::Node node = scene == nullptr ? engine::Node{} : scene->create_node("Node");
    return expect(node.valid() && !node.add_component<engine::MeshRenderer>() &&
                      !node.get_component<engine::MeshRenderer>() &&
                      components_of(node).size() == 1,
                  "failed component initialization leaves the generic component list unchanged");
}

bool material_contracts(test_support::ResourceServices& services)
{
    const engine::EngineConfig config;
    engine::ResourceSystem second_owner;
    if (!expect(second_owner.initialize(config), "independent resource cores can coexist"))
    {
        return false;
    }
    const auto* builtin = services.loaded<engine::ShaderResource>("builtin://shaders/pbr");
    if (!expect(builtin != nullptr, "resource host installs default shader"))
    {
        return false;
    }
    engine::schema::ShaderDataT shader = *builtin;
    shader.path = "shaders/tests/material-contracts";
    auto position = std::make_unique<engine::schema::ShaderParameterT>();
    position->name = "pos";
    position->uniform_name = "u_custom_pair";
    engine::schema::Vec2ValueT position_default;
    position_default.value = {0.0, 0.0};
    position->default_value.Set(position_default);
    shader.parameters.push_back(std::move(position));
    auto direction = std::make_unique<engine::schema::ShaderParameterT>();
    direction->name = "direction";
    direction->uniform_name = "u_custom_direction";
    engine::schema::Vec3ValueT direction_default;
    direction_default.value = {0.0, 0.0, 1.0};
    direction->default_value.Set(direction_default);
    shader.parameters.push_back(std::move(direction));
    test_support::ResourceFixture fixture;
    engine::schema::MaterialDataT definition;
    definition.type_path = "materials/tests/material-contracts";
    definition.shader_path = shader.path;
    auto override_value = std::make_unique<engine::schema::MaterialParameterT>();
    override_value->name = "pos";
    position_default.value = {1.0, 2.0};
    override_value->value.Set(position_default);
    definition.parameters.push_back(std::move(override_value));
    auto* resources = &services.core;
    if (!expect(resources && fixture.add(shader) && fixture.add(definition) &&
                    (fixture.publish(*resources) && services.prepare_builtins()),
                "submit custom shader and material entity directory") ||
        !expect(services.loaded<engine::ShaderResource>(shader.path) == nullptr &&
                    services.loaded<engine::MaterialResource>(definition.type_path) == nullptr,
                "directory submission does not load project definitions"))
    {
        return false;
    }
    shader.parameters.back()->name = "changed-source";
    definition.parameters[0]->value.AsVec2Value()->value = {99.0, 99.0};
    engine::WorldSystem world(&services.loader);
    if (!world.initialize(config))
    {
        return false;
    }
    engine::Scene* scene = world.create_scene("Material contracts");
    engine::Node node = scene == nullptr ? engine::Node{} : scene->create_node("Mesh");
    engine::MeshRenderer mesh =
        !node ? engine::MeshRenderer{} : node.add_component<engine::MeshRenderer>();
    if (!expect(mesh.valid(), "create mesh after resource initialization"))
    {
        return false;
    }
    engine::Material material = mesh.material();
    engine::Vec2 vector2;
    engine::Vec3 vector3;
    engine::Vec4 vector4;
    float scalar = 0.0F;
    if (!expect(services.load("materials/tests/material-contracts") &&
                    material.set_type("materials/tests/material-contracts") &&
                    material.get_v2("pos", vector2) && vector2.x() == 1.0 && vector2.y() == 2.0 &&
                    material.get_v3("direction", vector3) && vector3.z() == 1.0,
                "material merges custom shader defaults and definition overrides") ||
        !expect(material.set_v2("pos", engine::Vec2(3.0, 4.0)) &&
                    material.set_v3("direction", engine::Vec3(1.0, 2.0, 3.0)) &&
                    material.set_type("materials/tests/material-contracts") &&
                    material.set_shader("shaders/tests/material-contracts") &&
                    material.get_v2("pos", vector2) && vector2.x() == 3.0 &&
                    material.get_v3("direction", vector3) && vector3.z() == 3.0,
                "custom Vec2 and Vec3 setters work and same material or shader retains instance "
                "values") ||
        !expect(!material.set_float("pos", 1.0F) && !material.set_v2("Pos", engine::Vec2()) &&
                    !material.set_v4("missing", engine::Vec4()) &&
                    !material.set_float("lightingMode", 1.0F),
                "reject wrong kind, case, unknown names and hidden shader parameters") ||
        !expect(
            !material.set_v2("pos", engine::Vec2(std::numeric_limits<double>::infinity(), 0.0)) &&
                !material.set_v3("direction",
                                 engine::Vec3(0.0, std::numeric_limits<double>::max(), 0.0)) &&
                !material.set_float("roughness", std::numeric_limits<float>::quiet_NaN()) &&
                !material.set_v4("baseColor", engine::Vec4(1.1, 0.0, 0.0, 1.0)) &&
                material.get_v2("pos", vector2) && vector2.x() == 3.0,
            "reject nonfinite, GPU-overflow and bounded values without changing parameters") ||
        !expect(!material.set_type("materials/missing") &&
                    !material.set_shader("shaders/missing") &&
                    material.type_path() == "materials/tests/material-contracts" &&
                    material.shader_path() == "shaders/tests/material-contracts",
                "missing definitions preserve material and shader paths"))
    {
        return false;
    }
    vector2 = {9.0, 9.0};
    vector3 = {9.0, 9.0, 9.0};
    vector4 = {9.0, 9.0, 9.0, 9.0};
    scalar = 9.0F;
    if (!expect(!material.get_float("pos", scalar) && scalar == 0.0F &&
                    !material.get_v2("missing", vector2) && vector2.x() == 0.0 &&
                    vector2.y() == 0.0 && !material.get_v3("missing", vector3) &&
                    vector3.z() == 0.0 && !material.get_v4("missing", vector4) &&
                    vector4.w() == 0.0,
                "failed typed getters produce complete zero values") ||
        !expect(material.set_shader("builtin://shaders/unlit") &&
                    material.type_path() == "materials/tests/material-contracts" &&
                    material.shader_path() == "builtin://shaders/unlit" &&
                    !material.get_v2("pos", vector2) && !material.get_float("metallic", scalar) &&
                    material.get_v4("baseColor", vector4) && vector4.x() == 0.7,
                "shader replacement keeps material identity and rebuilds its exposed parameters") ||
        !expect(material.set_type("builtin://materials/default-pbr") &&
                    material.shader_path() == "builtin://shaders/pbr" &&
                    material.get_float("roughness", scalar) && scalar == 0.4F,
                "changing material restores new definition and shader defaults"))
    {
        return false;
    }
    definition.type_path = "materials/tests/invalid-parameter";
    engine::schema::FloatValueT wrong_type;
    wrong_type.value = 1.0;
    definition.parameters.front()->value.Set(wrong_type);
    std::unique_ptr<engine::Resource> invalid;
    return expect(fixture.add(definition) &&
                      (fixture.publish(*resources) && services.prepare_builtins()) &&
                      fixture.read(*resources, definition.type_path, invalid) &&
                      engine::resource_detail::data<engine::MaterialResource>(invalid.get()),
                  "core can read structurally valid material without loading its dependencies") &&
           expect(!material.set_type(definition.type_path) &&
                      material.type_path() == "builtin://materials/default-pbr" &&
                      material.shader_path() == "builtin://shaders/pbr" &&
                      material.get_float("roughness", scalar) && scalar == 0.4F,
                  "material validates shader parameter types and preserves the previous instance "
                  "without the dependency graph plugin");
}

bool resource_namespaces(test_support::ResourceServices& services)
{
    const auto* builtin = services.loaded<engine::ShaderResource>("builtin://shaders/pbr");
    auto* resources = &services.core;
    if (!expect(builtin && resources, "built-in definitions survive material tests"))
    {
        return false;
    }
    test_support::ResourceFixture fixture;
    engine::schema::ShaderDataT shader(*builtin);
    shader.path = "shaders/pbr";
    engine::schema::MaterialDataT material;
    material.type_path = "materials/default-pbr";
    material.shader_path = shader.path;
    engine::ResourceReference held;
    engine::ResourceReference held_shader;
    if (!expect(fixture.add(shader) && fixture.add(material) &&
                    (fixture.publish(*resources) && services.prepare_builtins()) &&
                    services.load(material.type_path, held),
                "project namespace uses the same relative names as builtins") ||
        !expect(held.data<engine::MaterialResource>()->shader_path == shader.path &&
                    services.loaded<engine::ShaderResource>(shader.path) != nullptr &&
                    services.load(shader.path, held_shader) &&
                    held_shader.data<engine::ShaderResource>() &&
                    held_shader.data<engine::ShaderResource>()->path == shader.path &&
                    held_shader.data<engine::ShaderResource>() != builtin,
                "plugin loads the declared dependency closure and exposes typed immutable values"))
    {
        return false;
    }
    const auto generation = held->generation();
    auto empty = fixture.catalog;
    empty.entries.clear();
    empty.revision = "empty";
    engine::ResourceReference missing;
    if (!expect(resources->update_catalog(empty, {}) && resources->generation() > generation &&
                    held->generation() == generation &&
                    held.data<engine::MaterialResource>()->shader_path == shader.path &&
                    held_shader.data<engine::ShaderResource>()->path == shader.path,
                "directory publication preserves held entities from the previous generation") ||
        !expect(!services.load(held->info().guid, missing) && !missing,
                "new loads use the current directory without falling back to a held old entity"))
    {
        return false;
    }
    const auto material_identity = held.handle();
    const auto shader_identity = held_shader.handle();
    held.reset();
    held_shader.reset();
    const engine::Resource* released = nullptr;
    return expect(services.loader.get(material_identity, released) ==
                          engine::ResourceError::invalid_handle &&
                      services.loader.get(shader_identity, released) ==
                          engine::ResourceError::invalid_handle,
                  "released acquisitions cannot be used after catalog replacement") &&
           expect(services.prepare_builtins() &&
                      services.loaded<engine::ShaderResource>("builtin://shaders/pbr"),
                  "project removal allows explicit preparation of current builtin definitions");
}

bool baked_lighting_bindings(test_support::ResourceServices& services)
{
    auto* resources = &services.core;
    engine::WorldSystem world(&services.loader);
    test_support::ResourceFixture fixture;
    engine::schema::MeshDataT mesh_data;
    mesh_data.path = "lighting/test.mesh";
    mesh_data.vertices = {{0, 0, 0, 0, 1, 0}, {1, 0, 0, 0, 1, 0}, {0, 0, 1, 0, 1, 0}};
    mesh_data.indices = {0, 2, 1};
    mesh_data.lightmap_texcoords = {{0.1, 0.1}, {0.9, 0.1}, {0.1, 0.9}};
    engine::schema::TextureDataT texture;
    texture.schema_version = 2;
    texture.path = "lighting/test.texture";
    texture.width = 1;
    texture.height = 1;
    texture.encoding = engine::schema::TextureEncoding::native;
    texture.format = engine::schema::TextureFormat::rgba8;
    texture.subresources.emplace_back(
        0, 0, 0, static_cast<std::uint64_t>(texture.width) * texture.height * 4);
    texture.srgb = false;
    texture.data = {128, 128, 128, 255};
    engine::schema::LightProbeDataT probes;
    probes.path = "lighting/test.probes";
    probes.bounds_min = engine::schema::Vec3(-1, -1, -1);
    probes.bounds_max = engine::schema::Vec3(1, 1, 1);
    probes.grid_x = probes.grid_y = probes.grid_z = 2;
    probes.baked_transform = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    probes.coefficients.resize(72, engine::schema::Vec3(0.1, 0.1, 0.1));
    probes.valid.resize(8, 1);
    probes.source_fingerprint = "0123456789abcdef";
    if (!expect(resources && fixture.add(mesh_data) && fixture.add(texture) &&
                    fixture.add(probes) &&
                    (fixture.publish(*resources) && services.prepare_builtins()) &&
                    world.initialize({}),
                "publish actual UV2, linear lightmap and probe resources"))
    {
        return false;
    }
    auto* scene = world.create_scene("Baked lighting");
    auto node = scene ? scene->create_node("Baked mesh") : engine::Node{};
    auto mesh = node ? node.add_component<engine::MeshRenderer>() : engine::MeshRenderer{};
    auto probe = node ? node.add_component<engine::LightProbeVolumeComponent>()
                      : engine::LightProbeVolumeComponent{};
    auto transform = node ? node.get_component<engine::Transform>() : engine::Transform{};
    engine::LightProbeVolumeSettings probe_settings;
    if (!expect(services.load(mesh_data.path) && services.load(texture.path) &&
                    services.load(probes.path) && mesh && probe && transform &&
                    mesh.set_mesh(mesh_data.path) && mesh.set_bake_static(true) &&
                    mesh.set_lightmap(texture.path) && mesh.lightmap_resource().id != 0 &&
                    mesh.lightmap_path() == texture.path &&
                    resources->catalog().identities().path_to_guid(probes.path,
                                                                   probe_settings.probes_guid) &&
                    probe.set_settings(probe_settings) && probe.resource().id != 0,
                "bind compiled baked resources through ordinary components"))
    {
        return false;
    }
    engine::schema::ComponentDataT saved;
    saved.flags = std::make_unique<engine::schema::ComponentFlagsT>();
    if (!expect(mesh.build_data(saved) &&
                    !saved.payload.AsMeshRendererData()->lightmap_mesh_hash.empty() &&
                    saved.payload.AsMeshRendererData()->lightmap_transform.size() == 16 &&
                    !mesh.set_lightmap("builtin://textures/white-srgb") &&
                    mesh.lightmap_resource().id != 0,
                "persist bake fingerprint; invalid replacement preserves existing lightmap"))
    {
        return false;
    }
    auto moved = test_support::read_settings(transform);
    moved.position = {2, 0, 0};
    if (!expect(transform.set_settings(moved) && mesh.lightmap_resource().id == 0 &&
                    probe.resource().id == 0 && mesh.restore_data(saved) &&
                    mesh.lightmap_resource().id == 0,
                "moving geometry/probe invalidates bake and restoration cannot rebase fingerprint"))
    {
        return false;
    }
    moved.position = {0, 0, 0};
    if (!expect(transform.set_settings(moved) && mesh.lightmap_resource().id != 0 &&
                    probe.resource().id != 0 && mesh.set_mesh("builtin://meshes/cube") &&
                    mesh.lightmap_resource().id == 0 && !mesh.set_lightmap(texture.path),
                "matching baked pose restores validity; replacing geometry clears lightmap"))
    {
        return false;
    }
    return expect(probe.set_settings({}) && probe.resource().id == 0 && mesh.set_lightmap(""),
                  "clearing baked resource bindings releases held resources");
}

bool scene_scope_publication(test_support::ResourceServices& services)
{
    using namespace engine;
    WorldSystem world(&services.loader, {2, 1.0});
    if (!world.initialize({}))
    {
        return false;
    }
    auto* old = world.create_scene("Original");
    auto old_node = old ? old->create_node("Old") : Node{};
    SceneScopeId scope;
    if (!expect(old_node && world.begin_scope(SceneLoadKind::replace_scene, 0, scope),
                "create isolated replacement scope"))
    {
        return false;
    }
    auto* candidate = world.scope_candidate(scope);
    auto node = candidate ? candidate->create_node("Replacement") : Node{};
    const auto pose = node.get_component<Transform>();
    if (!expect(node && pose && !world.find_node(node.id()) && world.find_node(old_node.id()),
                "candidate remains hidden while old scene is available") ||
        !expect(world.seal_scope(scope) && !node.set_enabled(false) &&
                    !candidate->create_node("Late"),
                "sealed candidates reject writes"))
    {
        return false;
    }
    if (!expect(world.step_scope(scope, {2, 1.0, 1024}) == SceneWorkState::ready &&
                    world.publish_scope(scope) && world.find_node(node.id()) &&
                    !world.find_node(old_node.id()) && !node.set_enabled(false),
                "publication switches render visibility before simulation"))
    {
        return false;
    }
    if (!expect(world.activate_scope(scope) && node.set_enabled(true),
                "simulation activation restores normal writes"))
    {
        return false;
    }
    const auto* destination = world.scene();
    SceneScopeId batch;
    if (!expect(world.begin_scope(SceneLoadKind::append_batch, destination->id(), batch),
                "create append batch"))
    {
        return false;
    }
    auto* staged = world.scope_candidate(batch);
    const auto extra = staged ? staged->create_node("Extra") : Node{};
    if (!expect(extra && extra.local_id() != node.local_id() && world.seal_scope(batch) &&
                    world.step_scope(batch, {2, 1.0, 1024}) == SceneWorkState::ready &&
                    !destination->find_node(extra.id()) && world.find_node(node.id()),
                "append mapping does not expose partial content"))
    {
        return false;
    }
    if (!expect(world.publish_scope(batch) && world.activate_scope(batch) &&
                    world.scene() == destination && destination->find_node(extra.id()) &&
                    extra.scene() == destination && world.find_node(node.id()),
                "append publishes the whole batch without replacing the scene"))
    {
        return false;
    }
    world.destroy_scene();
    return expect(!world.scope_candidate(scope) && !world.scope_candidate(batch) && !extra,
                  "world cleanup invalidates scopes and transferred entity handles");
}

} // namespace

int main()
{
    const engine::EngineConfig config;
    test_support::ResourceServices services;
    if (!missing_material_resources() || !services.initialize(config) ||
        !services.prepare_builtins() || !scene_lifecycle(services) ||
        !scene_node_queries(services) || !node_component_state(services) ||
        !hierarchy_and_transforms(services) || !indexed_scene_restore(services) ||
        !active_camera_lifecycle(services) || !incremental_scene_restore(services) ||
        !persistent_nodes(services) || !persistent_scene_loading(services) ||
        !component_contracts(services) || !camera_rays(services) || !transient_nodes(services) ||
        !material_contracts(services) || !baked_lighting_bindings(services) ||
        !resource_namespaces(services) || !scene_scope_publication(services))
    {
        return 1;
    }
    services.shutdown();
    std::puts("Scene, components, material parameters and resource namespaces passed.");
    return 0;
}
