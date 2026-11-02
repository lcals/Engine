#include "resource_fixture.hpp"

#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/light.hpp"
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

bool near(float actual, float expected)
{
    return std::abs(actual - expected) < 0.0001F;
}

bool scene_lifecycle()
{
    const engine::EngineConfig config;
    engine::WorldSystem world;
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
        !expect(scene->nodes().empty() && scene->active_camera() == nullptr,
                "scene starts with no hidden objects"))
    {
        return false;
    }
    const std::uint64_t first_id = scene->id();
    engine::Node* original = scene->create_node("Original");
    if (!expect(original != nullptr, "create flecs-backed node"))
    {
        return false;
    }
    const std::uint64_t original_id = original->id();
    if (!expect(scene->find_node(original_id) == original, "lookup complete node identity") ||
        !expect(scene->destroy_node(original_id), "delete node") ||
        !expect(!scene->destroy_node(original_id) && scene->find_node(original_id) == nullptr,
                "deleted node identity must stop resolving"))
    {
        return false;
    }
    engine::Node* replacement = scene->create_node("Replacement");
    if (!expect(replacement != nullptr && replacement->id() != original_id,
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

bool hierarchy_and_transforms()
{
    const engine::EngineConfig config;
    engine::WorldSystem world;
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
    engine::Node* parent = scene->create_node("Parent");
    engine::Node* child = scene->create_node("Child");
    engine::Node* grandchild = scene->create_node("Grandchild");
    engine::Node* sibling = scene->create_node("Unrelated");
    engine::Node* foreign = foreign_scene->create_node("Foreign node");
    if (!expect(parent != nullptr && child != nullptr && grandchild != nullptr &&
                    sibling != nullptr && foreign != nullptr,
                "create hierarchy nodes") ||
        !expect(child->set_parent(parent) && grandchild->set_parent(child), "attach child chain") ||
        !expect(!parent->set_parent(grandchild) && parent->parent() == nullptr,
                "reject cycles without changing parent") ||
        !expect(!child->set_parent(child), "reject self parent") ||
        !expect(!child->set_parent(foreign) && child->parent() == parent,
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
    if (!expect(parent->get_component<engine::Transform>()->set_settings(parent_transform) &&
                    child->get_component<engine::Transform>()->set_settings(child_transform),
                "set parent and local child transforms"))
    {
        return false;
    }
    std::array<float, 16> matrix{};
    if (!expect(child->get_component<engine::Transform>()->world_matrix(matrix.data()),
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
    if (!expect(!child->get_component<engine::Transform>()->set_settings(invalid) &&
                    child->get_component<engine::Transform>()->settings().scale.y() == 1.0,
                "zero scale rejected without altering transform"))
    {
        return false;
    }
    invalid = child_transform;
    invalid.position.mutate_y(std::numeric_limits<double>::quiet_NaN());
    if (!expect(!child->get_component<engine::Transform>()->set_settings(invalid) &&
                    child->get_component<engine::Transform>()->settings().position.y() == 0.0,
                "nonfinite transform rejected without altering transform") ||
        !expect(!child->get_component<engine::Transform>()->world_matrix(nullptr),
                "null matrix output rejected") ||
        !expect(child->set_parent(nullptr), "detach preserves local transform") ||
        !expect(child->get_component<engine::Transform>()->world_matrix(matrix.data()) &&
                    near(matrix[12], 1.0F) && near(matrix[13], 0.0F),
                "detached world matrix uses local coordinates"))
    {
        return false;
    }
    invalid = child_transform;
    invalid.scale.mutate_x(std::numeric_limits<double>::max());
    if (!expect(child->get_component<engine::Transform>()->set_settings(invalid),
                "finite transform may exceed GPU precision") ||
        !expect(!child->get_component<engine::Transform>()->world_matrix(matrix.data()),
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
    if (!expect(child->get_component<engine::Transform>()->set_settings(child_transform) &&
                    child->set_parent(parent),
                "negative scale allowed and child can reattach"))
    {
        return false;
    }
    engine::Camera* camera = grandchild->add_component<engine::Camera>();
    if (!expect(camera != nullptr && scene->active_camera() == nullptr &&
                    scene->set_active_camera(camera),
                "select active camera explicitly"))
    {
        return false;
    }
    const std::uint64_t parent_id = parent->id();
    const std::uint64_t child_id = child->id();
    const std::uint64_t grandchild_id = grandchild->id();
    const std::uint64_t sibling_id = sibling->id();
    return expect(scene->destroy_node(parent_id), "destroy hierarchy root") &&
           expect(scene->find_node(parent_id) == nullptr && scene->find_node(child_id) == nullptr &&
                      scene->find_node(grandchild_id) == nullptr,
                  "root deletion destroys all descendants") &&
           expect(scene->active_camera() == nullptr && scene->nodes().size() == 1 &&
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

bool indexed_scene_restore()
{
    using namespace engine;
    test_support::ResourceFixture fixture;
    auto* resources = ResourceSystem::instance();
    WorldSystem world;
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
    auto* camera = original->nodes()[0]->add_component<Camera>();
    schema::SceneDataT source;
    if (!camera || !original->set_active_camera(camera) || !original->build_data(source))
    {
        return false;
    }
    // Parents follow children; consecutive resource IDs also collide with naive temporary IDs.
    const std::array<std::uint64_t, 4> ids{42, 1, 2, 10000};
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        source.nodes[i]->info->id = ids[i];
    }
    source.nodes[0]->info->parent_id = 2;
    source.nodes[2]->info->parent_id = 1;
    source.info->active_camera_node = 42;
    if (!add_scene_resource(fixture, "indexed.scene", source) || !fixture.publish(*resources))
    {
        return false;
    }
    auto* loaded = world.load_scene("indexed.scene");
    if (!expect(loaded && loaded->nodes().size() == ids.size(), "restore sparse unordered IDs"))
    {
        return false;
    }
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        auto* node = loaded->nodes()[i].get();
        if (!expect(node->info().id == ids[i] && loaded->find_node(node->id()) == node &&
                        std::as_const(*loaded).find_node(node->id()) == node,
                    "resource order and both const/mutable runtime lookups survive restoration"))
        {
            return false;
        }
    }
    auto* leaf = loaded->nodes()[0].get();
    auto* root = loaded->nodes()[1].get();
    auto* branch = loaded->nodes()[2].get();
    auto* spare = loaded->nodes()[3].get();
    if (!expect(leaf->parent() == branch && branch->parent() == root && !root->parent() &&
                    !spare->parent() && loaded->active_camera() == leaf->get_component<Camera>() &&
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
            invalid.nodes[3]->info->id = 2;
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
        if (!add_scene_resource(fixture, path, invalid) || !fixture.publish(*resources) ||
            !expect(!world.load_scene(path) && world.scene() == loaded &&
                        loaded->find_node(leaf->id()) == leaf && leaf->parent() == branch &&
                        loaded->active_camera() == camera,
                    "failed candidate cleans its indices without changing the active scene"))
        {
            return false;
        }
    }
    std::vector<Node*> survivors{spare};
    std::vector<std::uint64_t> removed{leaf->id(), root->id(), branch->id()};
    for (std::size_t i = 0; i < 256; ++i)
    {
        auto* node = loaded->create_node("Indexed growth");
        if (!expect(node && node->info().id == 10001 + i && !node->parent(),
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
            if (!node->set_parent(branch))
            {
                return false;
            }
            removed.push_back(node->id());
        }
    }
    auto* unrelated = loaded->create_node("Unrelated root");
    if (!expect(unrelated && loaded->destroy_node(unrelated->id()) &&
                    loaded->active_camera() == camera,
                "deleting another root keeps the indexed active camera"))
    {
        return false;
    }
    if (!expect(leaf->parent() == branch && loaded->destroy_node(root->id()) &&
                    !loaded->active_camera() && loaded->nodes().size() == survivors.size(),
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
        if (!expect(loaded->nodes()[i].get() == survivors[i] &&
                        loaded->find_node(survivors[i]->id()) == survivors[i] &&
                        !survivors[i]->parent(),
                    "stable erase preserves remaining node order, addresses and root identity"))
        {
            return false;
        }
    }
    source.nodes[0]->info->id = std::numeric_limits<std::uint64_t>::max();
    source.info->active_camera_node = source.nodes[0]->info->id;
    if (!add_scene_resource(fixture, "index-limit.scene", source) || !fixture.publish(*resources))
    {
        return false;
    }
    loaded = world.load_scene("index-limit.scene");
    return expect(loaded && loaded->nodes().size() == ids.size() && loaded->active_camera() &&
                      loaded->nodes()[0]->parent() == loaded->nodes()[2].get() &&
                      !loaded->create_node("Cannot wrap ID"),
                  "maximum local ID may precede other nodes; automatic allocation never wraps");
}

bool incremental_scene_restore()
{
    using namespace engine;
    test_support::ResourceFixture fixture;
    auto* resources = ResourceSystem::instance();
    WorldSystem world({2, 1.0});
    if (!resources || !world.initialize({}))
    {
        return false;
    }
    auto* active = world.create_scene("Keep until commit");
    auto* child = active ? active->create_node("Child") : nullptr;
    auto* parent = active ? active->create_node("Parent") : nullptr;
    schema::SceneDataT data;
    if (!child || !parent || !child->set_parent(parent) ||
        !active->set_active_camera(child->add_component<Camera>()) ||
        !active->build_data(data) || !add_scene_resource(fixture, "staged.scene", data) ||
        !fixture.publish(*resources) || !world.set_paused(true))
    {
        return false;
    }
    ResourcePtr resource;
    if (!fixture.load("staged.scene", resource))
    {
        return false;
    }
    const auto cancelled = world.begin_restore_scene(resource);
    if (!expect(cancelled != 0 && world.begin_restore_scene(resource) == 0 &&
                    world.step_restore_scene(cancelled, 0) == SceneRestoreStatus::loading &&
                    !world.commit_restore_scene(cancelled) && world.scene() == active,
                "zero budget and premature commit preserve active scene"))
    {
        return false;
    }
    world.cancel_restore_scene(cancelled);
    const auto request = world.begin_restore_scene(resource);
    world.cancel_restore_scene(cancelled);
    if (!expect(request > cancelled &&
                    world.step_restore_scene(cancelled, 100) == SceneRestoreStatus::failed &&
                    world.step_restore_scene(request, 100) == SceneRestoreStatus::loading &&
                    world.scene() == active && world.paused(),
                "configured node budget and cancelled request cannot publish candidate") ||
        !expect(world.step_restore_scene(request, 100) == SceneRestoreStatus::loading &&
                    world.step_restore_scene(request, 100) == SceneRestoreStatus::ready,
                "node creation, hierarchy and camera finish over separate steps"))
    {
        return false;
    }
    auto* restored = world.commit_restore_scene(request);
    if (!expect(restored && restored != active && restored->nodes().size() == 2 &&
                    restored->nodes()[0]->parent() == restored->nodes()[1].get() &&
                    restored->active_camera() && !world.paused(),
                "commit publishes complete hierarchy and clears old pause"))
    {
        return false;
    }
    const auto stale = world.begin_restore_scene(resource);
    if (!add_scene_resource(fixture, "another.scene", data) || !fixture.publish(*resources) ||
        !expect(world.step_restore_scene(stale, 100) == SceneRestoreStatus::failed &&
                    !world.commit_restore_scene(stale) && world.scene() == restored,
                "directory replacement invalidates candidate without replacing scene") ||
        !fixture.load("staged.scene", resource) || world.begin_restore_scene(resource) == 0)
    {
        return false;
    }
    world.shutdown();
    return expect(world.scene() == nullptr, "shutdown destroys candidate before ECS world");
}

bool component_contracts()
{
    const engine::EngineConfig config;
    engine::WorldSystem world;
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
    engine::Node* node = scene->create_node("Combined node");
    engine::Node* foreign_node = foreign_scene->create_node("Foreign camera");
    if (!expect(node != nullptr && foreign_node != nullptr, "create component owners"))
    {
        return false;
    }
    if (!expect(node->get_component<engine::Transform>() != nullptr &&
                    node->components().size() == 1,
                "new scene node owns a Transform through the generic component list") ||
        !expect(node->get_component<engine::Camera>() == nullptr &&
                    node->get_component(engine::Camera::component_type) == nullptr,
                "generic typed and untyped lookup return null for missing component") ||
        !expect(node->add_component<engine::Transform>() == nullptr,
                "duplicate Transform is rejected through the same generic method"))
    {
        return false;
    }
    engine::Camera* camera = node->add_component<engine::Camera>();
    engine::Light* light = node->add_component<engine::Light>();
    engine::MeshRenderer* mesh = node->add_component<engine::MeshRenderer>();
    engine::Camera* foreign_camera = foreign_node->add_component<engine::Camera>();
    if (!expect(camera != nullptr && light != nullptr && mesh != nullptr &&
                    foreign_camera != nullptr,
                "different components can share a node") ||
        !expect(node->add_component<engine::Camera>() == nullptr &&
                    node->add_component<engine::Light>() == nullptr &&
                    node->add_component<engine::MeshRenderer>() == nullptr,
                "duplicate component type rejected") ||
        !expect(&camera->node() == node && &light->node() == node && &mesh->node() == node,
                "components borrow their owner") ||
        !expect(camera->type() == engine::ComponentType::CameraData &&
                    light->type() == engine::ComponentType::LightData &&
                    mesh->type() == engine::ComponentType::MeshRendererData,
                "component types are explicit") ||
        !expect(scene->set_active_camera(camera) && !scene->set_active_camera(foreign_camera) &&
                    scene->active_camera() == camera,
                "active camera must belong to scene"))
    {
        return false;
    }
    const engine::Node& read_only_node = *node;
    if (!expect(node->get_component(engine::Camera::component_type) == camera &&
                    read_only_node.get_component<engine::Camera>() == camera &&
                    read_only_node.components().size() == 4,
                "one generic list provides const typed and untyped access"))
    {
        return false;
    }
    engine::CameraSettings camera_settings;
    camera_settings.fov_degrees = 75.0F;
    camera_settings.near_clip = 0.2F;
    camera_settings.far_clip = 50.0F;
    if (!expect(camera->set_settings(camera_settings), "configure camera"))
    {
        return false;
    }
    camera_settings.far_clip = 0.1F;
    if (!expect(!camera->set_settings(camera_settings) && camera->settings().far_clip == 50.0F,
                "invalid clipping plane leaves old camera settings"))
    {
        return false;
    }
    engine::LightSettings light_settings;
    light_settings.intensity = 4.0F;
    if (!expect(light->set_settings(light_settings), "configure light"))
    {
        return false;
    }
    light_settings.color.mutate_r(std::numeric_limits<float>::infinity());
    if (!expect(!light->set_settings(light_settings) && light->settings().color.r() == 1.0F,
                "invalid light color leaves old settings"))
    {
        return false;
    }
    engine::Material& material = mesh->material();
    if (!expect(material.type_path() == "builtin://materials/default-pbr" &&
                    material.set_float("metallic", 0.6F) && material.set_float("roughness", 0.2F) &&
                    material.set_v4("baseColor", engine::Vec4(0.7, 0.3, 0.15, 1.0)) &&
                    mesh->mesh_path() == "builtin://meshes/cube",
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
    if (!expect(material.cull_mode() == engine::schema::CullMode::back &&
                    material.get_float("alphaCutoff", cutoff) && cutoff == 0.0F &&
                    material.set_cull_mode(engine::schema::CullMode::none) &&
                    !material.set_cull_mode(static_cast<engine::schema::CullMode>(255)) &&
                    material.cull_mode() == engine::schema::CullMode::none &&
                    material.set_float("alphaCutoff", 0.5F) &&
                    !material.set_float("alphaCutoff", 1.1F) &&
                    material.get_float("alphaCutoff", cutoff) && cutoff == 0.5F &&
                    material.set_shader("builtin://shaders/unlit") &&
                    material.cull_mode() == engine::schema::CullMode::none &&
                    material.get_float("alphaCutoff", cutoff) && cutoff == 0.0F &&
                    material.set_type("builtin://materials/default-unlit") &&
                    material.cull_mode() == engine::schema::CullMode::back,
                "coverage defaults, invalid states and shader/type changes preserve "
                "their contracts"))
    {
        return false;
    }
    mesh->set_enabled(false);
    if (!expect(!mesh->enabled() && node->get_component<engine::MeshRenderer>() == mesh,
                "disable retains component ownership"))
    {
        return false;
    }
    return expect(scene->set_active_camera(nullptr) && scene->active_camera() == nullptr,
                  "active camera can be cleared");
}

bool camera_rays()
{
    using namespace engine;
    const auto vector_near = [](const Vec3& actual, const Vec3& expected)
    {
        return std::abs(actual.x() - expected.x()) < 0.0001 &&
               std::abs(actual.y() - expected.y()) < 0.0001 &&
               std::abs(actual.z() - expected.z()) < 0.0001;
    };
    const EngineConfig config;
    WorldSystem world;
    if (!expect(world.initialize(config), "initialize camera ray world without a window"))
    {
        return false;
    }
    auto* scene = world.create_scene("Camera rays");
    auto* parent = scene != nullptr ? scene->create_node("Camera parent") : nullptr;
    auto* node = scene != nullptr ? scene->create_node("Camera") : nullptr;
    auto* camera = node != nullptr ? node->add_component<Camera>() : nullptr;
    if (!expect(camera != nullptr && parent != nullptr, "create ray camera"))
    {
        return false;
    }
    CameraSettings settings;
    settings.fov_degrees = 90.0F;
    settings.near_clip = 1.0F;
    settings.far_clip = 100.0F;
    camera->set_enabled(false);
    Vec3 origin;
    Vec3 direction;
    if (!expect(camera->set_settings(settings) && scene->active_camera() == nullptr &&
                    camera->viewport_point_to_ray({0.5, 0.5}, 2.0, origin, direction) &&
                    vector_near(origin, {0.0, 0.0, -1.0}) &&
                    vector_near(direction, {0.0, 0.0, -1.0}),
                "disabled non-active camera computes a center ray from its near plane"))
    {
        return false;
    }
    const double corner_length = std::sqrt(6.0);
    const Vec3 corner_direction{2.0 / corner_length, 1.0 / corner_length, -1.0 / corner_length};
    if (!expect(camera->viewport_point_to_ray({1.0, 0.0}, 2.0, origin, direction) &&
                    vector_near(origin, {2.0, 1.0, -1.0}) &&
                    vector_near(direction, corner_direction),
                "perspective top right follows vertical field of view and aspect ratio") ||
        !expect(
            camera->screen_point_to_ray({800.0, 0.0}, {800.0, 400.0}, origin, direction) &&
                vector_near(origin, {2.0, 1.0, -1.0}) && vector_near(direction, corner_direction) &&
                camera->screen_point_to_ray({400.0, 0.0}, {400.0, 200.0}, origin, direction) &&
                vector_near(origin, {2.0, 1.0, -1.0}) && vector_near(direction, corner_direction),
            "screen rays agree for matching logical or pixel coordinate units") ||
        !expect(camera->viewport_point_to_ray({1.5, 1.5}, 2.0, origin, direction) &&
                    vector_near(origin, {4.0, -2.0, -1.0}),
                "finite points outside the viewport are extrapolated without clamping"))
    {
        return false;
    }
    settings.projection = schema::Projection::orthographic;
    settings.orthographic_size = 2.0F;
    if (!expect(
            camera->set_settings(settings) &&
                camera->viewport_point_to_ray({0.5, 0.5}, 2.0, origin, direction) &&
                vector_near(origin, {0.0, 0.0, -1.0}) && vector_near(direction, {0.0, 0.0, -1.0}) &&
                camera->screen_point_to_ray({800.0, 0.0}, {800.0, 400.0}, origin, direction) &&
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
    if (!expect(camera->set_settings(settings) && node->set_parent(parent) &&
                    parent->get_component<Transform>()->set_settings(parent_settings) &&
                    node->get_component<Transform>()->set_settings(local_settings) &&
                    camera->viewport_point_to_ray({1.0, 0.0}, 2.0, origin, direction) &&
                    vector_near(origin, shear_origin) && vector_near(direction, shear_direction),
                "world ray uses the rendered orthogonal basis under parent scale and shear"))
    {
        return false;
    }
    local_settings.scale.mutate_z(-7.0);
    if (!expect(node->get_component<Transform>()->set_settings(local_settings) &&
                    camera->viewport_point_to_ray({0.5, 0.5}, 2.0, origin, direction) &&
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
        if (!expect(!camera->screen_point_to_ray({0.5, 0.5}, size, origin, direction) &&
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
        if (!expect(!camera->viewport_point_to_ray({0.5, 0.5}, aspect, origin, direction) &&
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
        if (!expect(!camera->viewport_point_to_ray(point, 2.0, origin, direction) &&
                        vector_near(origin, {}) && vector_near(direction, {}) &&
                        !camera->screen_point_to_ray(point, {800.0, 400.0}, origin, direction),
                    "both coordinate APIs reject non-finite points"))
        {
            return false;
        }
    }
    local_settings.scale.mutate_x(1e-100);
    origin = {1.0, 2.0, 3.0};
    direction = {4.0, 5.0, 6.0};
    if (!expect(node->get_component<Transform>()->set_settings(local_settings) &&
                    !camera->viewport_point_to_ray({0.5, 0.5}, 2.0, origin, direction) &&
                    vector_near(origin, {}) && vector_near(direction, {}),
                "a world axis lost to float underflow is rejected as a degenerate transform"))
    {
        return false;
    }
    local_settings.scale.mutate_x(5.0);
    local_settings.position.mutate_x(std::numeric_limits<double>::max());
    origin = {1.0, 2.0, 3.0};
    direction = {4.0, 5.0, 6.0};
    return expect(node->get_component<Transform>()->set_settings(local_settings) &&
                      !camera->viewport_point_to_ray({0.5, 0.5}, 2.0, origin, direction) &&
                      vector_near(origin, {}) && vector_near(direction, {}),
                  "unrepresentable world transform fails without partial ray outputs");
}

bool transient_nodes()
{
    engine::WorldSystem world;
    if (!world.initialize({}))
    {
        return false;
    }
    auto* scene = world.create_scene("Generated hierarchy");
    auto* persistent = scene ? scene->create_node("Source") : nullptr;
    auto* generated = scene ? scene->create_node("Generated") : nullptr;
    auto* child = scene ? scene->create_node("Generated child") : nullptr;
    if (!expect(persistent && generated && child && child->set_parent(generated) &&
                    !generated->transient() && !child->transient(),
                "nodes start persistent"))
    {
        return false;
    }
    generated->set_transient(true);
    child->set_transient(false);
    engine::schema::SceneDataT saved;
    if (!expect(generated->transient() && child->transient() && !persistent->transient() &&
                    scene->build_data(saved) && saved.nodes.size() == 1 &&
                    saved.nodes.front()->info->id == persistent->info().id &&
                    scene->nodes().size() == 3,
                "snapshot excludes generated subtree without changing live nodes"))
    {
        return false;
    }
    auto* camera = child->add_component<engine::Camera>();
    if (!expect(camera && scene->set_active_camera(camera) && !scene->build_data(saved) &&
                    saved.nodes.empty() && !saved.info,
                "snapshot rejects a transient active camera without leaving partial output"))
    {
        return false;
    }
    generated->set_transient(false);
    if (!expect(!child->transient() && scene->build_data(saved) && saved.nodes.size() == 3 &&
                    saved.info->active_camera_node == child->info().id,
                "clearing parent marker restores descendant persistence and camera identity"))
    {
        return false;
    }
    child->set_transient(true);
    if (!scene->set_active_camera(nullptr) || !child->set_parent(nullptr))
    {
        return false;
    }
    return expect(child->transient() && !generated->transient() && scene->build_data(saved) &&
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
    engine::Node* node = scene == nullptr ? nullptr : scene->create_node("Node");
    return expect(node != nullptr && node->add_component<engine::MeshRenderer>() == nullptr &&
                      node->get_component<engine::MeshRenderer>() == nullptr &&
                      node->components().size() == 1,
                  "failed component initialization leaves the generic component list unchanged");
}

bool material_contracts()
{
    const engine::EngineConfig config;
    engine::ResourceSystem second_owner;
    if (!expect(!second_owner.initialize(config), "only one resource host owns definitions"))
    {
        return false;
    }
    const auto* builtin = engine::ResourceSystem::find_shader("builtin://shaders/pbr");
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
    auto* resources = engine::ResourceSystem::instance();
    if (!expect(resources && fixture.add(shader) && fixture.add(definition) &&
                    fixture.publish(*resources),
                "submit custom shader and material entity directory") ||
        !expect(engine::ResourceSystem::find_shader(shader.path) == nullptr &&
                    engine::ResourceSystem::find_material(definition.type_path) == nullptr,
                "directory submission does not load project definitions"))
    {
        return false;
    }
    shader.parameters.back()->name = "changed-source";
    definition.parameters[0]->value.AsVec2Value()->value = {99.0, 99.0};
    engine::WorldSystem world;
    if (!world.initialize(config))
    {
        return false;
    }
    engine::Scene* scene = world.create_scene("Material contracts");
    engine::Node* node = scene == nullptr ? nullptr : scene->create_node("Mesh");
    engine::MeshRenderer* mesh =
        node == nullptr ? nullptr : node->add_component<engine::MeshRenderer>();
    if (!expect(mesh != nullptr, "create mesh after resource initialization"))
    {
        return false;
    }
    engine::Material& material = mesh->material();
    engine::Vec2 vector2;
    engine::Vec3 vector3;
    engine::Vec4 vector4;
    float scalar = 0.0F;
    if (!expect(material.set_type("materials/tests/material-contracts") &&
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
    engine::ResourcePtr invalid;
    return expect(fixture.add(definition) && fixture.publish(*resources) &&
                      fixture.load(definition.type_path, invalid) && invalid->material(),
                  "core can read structurally valid material without loading its dependencies") &&
           expect(!material.set_type(definition.type_path) &&
                      material.type_path() == "builtin://materials/default-pbr" &&
                      material.shader_path() == "builtin://shaders/pbr" &&
                      material.get_float("roughness", scalar) && scalar == 0.4F,
                  "material validates shader parameter types and preserves the previous instance "
                  "without the dependency graph plugin");
}

bool resource_namespaces()
{
    const auto* builtin = engine::ResourceSystem::find_shader("builtin://shaders/pbr");
    auto* resources = engine::ResourceSystem::instance();
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
    engine::ResourcePtr held;
    engine::ResourcePtr held_shader;
    if (!expect(fixture.add(shader) && fixture.add(material) && fixture.publish(*resources) &&
                    fixture.load(material.type_path, held),
                "project namespace uses the same relative names as builtins") ||
        !expect(held->material()->shader_path == shader.path &&
                    engine::ResourceSystem::find_shader(shader.path) == nullptr &&
                    fixture.load(shader.path, held_shader) && held_shader->shader() &&
                    held_shader->shader()->path == shader.path && held_shader->shader() != builtin,
                "core loads explicit material and shader entities without a dependency cache"))
    {
        return false;
    }
    const auto generation = held->generation();
    auto empty = fixture.catalog;
    empty.entries.clear();
    empty.revision = "empty";
    engine::ResourcePtr missing = held;
    if (!expect(resources->update_catalog(empty, {}) && resources->generation() > generation &&
                    held->generation() == generation &&
                    held->material()->shader_path == shader.path &&
                    held_shader->shader()->path == shader.path,
                "directory publication preserves held entities from the previous generation") ||
        !expect(!resources->load(held->info().guid, missing) && !missing,
                "new loads use the current directory without falling back to a held old entity"))
    {
        return false;
    }
    const std::weak_ptr<const engine::Resource> material_weak = held;
    const std::weak_ptr<const engine::Resource> shader_weak = held_shader;
    held.reset();
    held_shader.reset();
    return expect(material_weak.expired() && shader_weak.expired() &&
                      engine::ResourceSystem::find_material(material.type_path) == nullptr &&
                      engine::ResourceSystem::find_shader(shader.path) == nullptr,
                  "each core entity is released with its final explicit owner") &&
           expect(engine::ResourceSystem::find_shader("builtin://shaders/pbr") == builtin,
                  "project removal preserves builtin identity");
}

} // namespace

int main()
{
    const engine::EngineConfig config;
    engine::ResourceSystem resources;
    if (!missing_material_resources() || !resources.initialize(config) || !scene_lifecycle() ||
        !hierarchy_and_transforms() || !indexed_scene_restore() || !incremental_scene_restore() ||
        !component_contracts() ||
        !camera_rays() || !transient_nodes() || !material_contracts() || !resource_namespaces())
    {
        return 1;
    }
    resources.shutdown();
    if (!expect(engine::ResourceSystem::find_shader("builtin://shaders/pbr") == nullptr &&
                    engine::ResourceSystem::find_material("builtin://materials/default-pbr") ==
                        nullptr,
                "resource host shutdown clears all namespaces"))
    {
        return 1;
    }
    std::puts("Scene, components, material parameters and resource namespaces passed.");
    return 0;
}
