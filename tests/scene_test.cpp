#include "runtime/engine_config.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

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
    engine::Transform parent_transform;
    parent_transform.position = {1.0, 2.0, 3.0};
    parent_transform.rotation.z = 90.0;
    parent_transform.scale.x = 2.0;
    engine::Transform child_transform;
    child_transform.position.x = 1.0;
    if (!expect(parent->set_transform(parent_transform) && child->set_transform(child_transform),
                "set parent and local child transforms"))
    {
        return false;
    }
    std::array<float, 16> matrix{};
    if (!expect(child->world_matrix(matrix.data()), "compute world matrix") ||
        !expect(near(matrix[12], 1.0F) && near(matrix[13], 4.0F) && near(matrix[14], 3.0F),
                "world translation applies parent rotation and nonuniform scale") ||
        !expect(near(matrix[0], 0.0F) && near(matrix[1], 2.0F) && near(matrix[4], -1.0F),
                "matrix is column major and uses T R S order"))
    {
        return false;
    }
    engine::Transform invalid = child_transform;
    invalid.scale.y = 0.0;
    if (!expect(!child->set_transform(invalid) && child->transform().scale.y == 1.0,
                "zero scale rejected without altering transform"))
    {
        return false;
    }
    invalid = child_transform;
    invalid.position.y = std::numeric_limits<double>::quiet_NaN();
    if (!expect(!child->set_transform(invalid) && child->transform().position.y == 0.0,
                "nonfinite transform rejected without altering transform") ||
        !expect(!child->world_matrix(nullptr), "null matrix output rejected") ||
        !expect(child->set_parent(nullptr), "detach preserves local transform") ||
        !expect(child->world_matrix(matrix.data()) && near(matrix[12], 1.0F) &&
                    near(matrix[13], 0.0F),
                "detached world matrix uses local coordinates"))
    {
        return false;
    }
    invalid = child_transform;
    invalid.scale.x = std::numeric_limits<double>::max();
    if (!expect(child->set_transform(invalid), "finite transform may exceed GPU precision") ||
        !expect(!child->world_matrix(matrix.data()), "reject float matrix overflow"))
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
    child_transform.scale.z = -1.0;
    if (!expect(child->set_transform(child_transform) && child->set_parent(parent),
                "negative scale allowed and child can reattach"))
    {
        return false;
    }
    engine::Camera* camera = grandchild->add_camera();
    if (!expect(camera != nullptr && scene->active_camera() == camera,
                "first camera component becomes active"))
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
    engine::Camera* camera = node->add_camera();
    engine::Light* light = node->add_light();
    engine::MeshRenderer* mesh = node->add_mesh_renderer();
    engine::Camera* foreign_camera = foreign_node->add_camera();
    if (!expect(camera != nullptr && light != nullptr && mesh != nullptr &&
                    foreign_camera != nullptr,
                "different components can share a node") ||
        !expect(node->add_camera() == nullptr && node->add_light() == nullptr &&
                    node->add_mesh_renderer() == nullptr,
                "duplicate component type rejected") ||
        !expect(&camera->node() == node && &light->node() == node && &mesh->node() == node,
                "components borrow their owner") ||
        !expect(camera->type() == engine::ComponentType::camera &&
                    light->type() == engine::ComponentType::light &&
                    mesh->type() == engine::ComponentType::mesh_renderer,
                "component types are explicit") ||
        !expect(!scene->set_active_camera(foreign_camera) && scene->active_camera() == camera,
                "active camera must belong to scene"))
    {
        return false;
    }
    engine::CameraSettings camera_settings{75.0F, 0.2F, 50.0F};
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
    light_settings.color.r = std::numeric_limits<float>::infinity();
    if (!expect(!light->set_settings(light_settings) && light->settings().color.r == 1.0F,
                "invalid light color leaves old settings"))
    {
        return false;
    }
    engine::Material material;
    material.metallic = 0.6F;
    material.roughness = 0.2F;
    if (!expect(mesh->set_material(material) && mesh->mesh() == engine::MeshKind::cube,
                "configure built-in cube material"))
    {
        return false;
    }
    material.roughness = -1.0F;
    if (!expect(!mesh->set_material(material) && mesh->material().roughness == 0.2F,
                "invalid material leaves old values"))
    {
        return false;
    }
    mesh->set_enabled(false);
    if (!expect(!mesh->enabled() && node->mesh_renderer() == mesh,
                "disable retains component ownership"))
    {
        return false;
    }
    return expect(scene->set_active_camera(nullptr) && scene->active_camera() == nullptr,
                  "active camera can be cleared");
}

} // namespace

int main()
{
    if (!scene_lifecycle() || !hierarchy_and_transforms() || !component_contracts())
    {
        return 1;
    }
    std::puts("Scene identity, node hierarchy, transforms, and component contracts passed.");
    return 0;
}
