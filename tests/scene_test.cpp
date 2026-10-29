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
#include <limits>
#include <memory>
#include <utility>

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
    if (!expect(material.type_path() == "builtin/materials/default-pbr" &&
                    material.set_float("metallic", 0.6F) && material.set_float("roughness", 0.2F) &&
                    material.set_v4("baseColor", engine::Vec4(0.7, 0.3, 0.15, 1.0)) &&
                    mesh->mesh_path() == "builtin/meshes/cube",
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
    mesh->set_enabled(false);
    if (!expect(!mesh->enabled() && node->get_component<engine::MeshRenderer>() == mesh,
                "disable retains component ownership"))
    {
        return false;
    }
    return expect(scene->set_active_camera(nullptr) && scene->active_camera() == nullptr,
                  "active camera can be cleared");
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
    const auto* builtin = engine::ResourceSystem::find_shader("builtin/shaders/pbr");
    if (!expect(builtin != nullptr, "resource host installs default shader"))
    {
        return false;
    }
    engine::schema::ShaderDataT shader = *builtin;
    shader.path = "project/shaders/tests/material-contracts";
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
    if (!expect(engine::ResourceSystem::register_shader(shader), "register custom shader layout") ||
        !expect(!engine::ResourceSystem::register_shader(shader), "reject duplicate shader path"))
    {
        return false;
    }
    const auto* registered = engine::ResourceSystem::find_shader(shader.path);
    shader.parameters.back()->name = "changed-source";
    if (!expect(registered != nullptr && registered->parameters.back()->name == "direction",
                "shader registry owns a deep copy"))
    {
        return false;
    }
    for (const auto* path :
         {"/absolute", "project/shaders/../bad", "builtin/shaders//bad", "project/shaders/./bad",
          "project/shaders\\bad", "C:/bad", "builtin/shaders/bad/", "shaders/missing-source",
          "external/shaders/bad", "builtin", "project/"})
    {
        shader.path = path;
        if (!expect(!engine::ResourceSystem::register_shader(shader),
                    "reject noncanonical resource paths"))
        {
            return false;
        }
    }
    shader = *registered;
    shader.path = "builtin/materials/default-pbr";
    if (!expect(!engine::ResourceSystem::register_shader(shader),
                "shader and material paths share one namespace"))
    {
        return false;
    }
    shader.path = "project/shaders/tests/rejected";
    shader.parameters.back()->uniform_name = "u_modelViewProj";
    if (!expect(!engine::ResourceSystem::register_shader(shader),
                "reject reserved engine uniforms"))
    {
        return false;
    }
    shader.parameters.back()->uniform_name = "u_custom_pair";
    shader.parameters.back()->uniform_offset = 1;
    if (!expect(!engine::ResourceSystem::register_shader(shader),
                "reject overlapping uniform slots"))
    {
        return false;
    }
    shader.parameters.back()->uniform_name = "u_custom_direction";
    shader.parameters.back()->uniform_offset = 2;
    if (!expect(!engine::ResourceSystem::register_shader(shader), "reject Vec3 beyond a Vec4 slot"))
    {
        return false;
    }
    engine::schema::MaterialDataT definition;
    definition.type_path = "project/materials/tests/material-contracts";
    definition.shader_path = registered->path;
    auto override_value = std::make_unique<engine::schema::MaterialParameterT>();
    override_value->name = "pos";
    position_default.value = {1.0, 2.0};
    override_value->value.Set(position_default);
    definition.parameters.push_back(std::move(override_value));
    if (!expect(engine::ResourceSystem::register_material(definition),
                "register material with named shader override") ||
        !expect(!engine::ResourceSystem::register_material(definition),
                "reject duplicate material path"))
    {
        return false;
    }
    definition.parameters[0]->value.AsVec2Value()->value = {99.0, 99.0};
    const auto* copied = engine::ResourceSystem::find_material(definition.type_path);
    if (!expect(copied != nullptr && copied->parameters[0]->value.AsVec2Value()->value.x() == 1.0,
                "material registry owns a deep copy"))
    {
        return false;
    }
    definition.type_path = "project/materials/tests/rejected";
    definition.parameters[0]->name = "unknown";
    if (!expect(!engine::ResourceSystem::register_material(definition),
                "reject material override missing from shader"))
    {
        return false;
    }
    definition.parameters[0]->name = "baseColor";
    if (!expect(!engine::ResourceSystem::register_material(definition),
                "reject material override with mismatched value type"))
    {
        return false;
    }
    definition.parameters[0]->name = "lightingMode";
    engine::schema::FloatValueT hidden;
    hidden.value = 1.0F;
    definition.parameters[0]->value.Set(hidden);
    if (!expect(!engine::ResourceSystem::register_material(definition),
                "hidden shader controls cannot be overridden as material parameters"))
    {
        return false;
    }
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
    if (!expect(material.set_type("project/materials/tests/material-contracts") &&
                    material.get_v2("pos", vector2) && vector2.x() == 1.0 && vector2.y() == 2.0 &&
                    material.get_v3("direction", vector3) && vector3.z() == 1.0,
                "material merges custom shader defaults and definition overrides") ||
        !expect(material.set_v2("pos", engine::Vec2(3.0, 4.0)) &&
                    material.set_v3("direction", engine::Vec3(1.0, 2.0, 3.0)) &&
                    material.set_type("project/materials/tests/material-contracts") &&
                    material.set_shader("project/shaders/tests/material-contracts") &&
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
        !expect(!material.set_type("project/materials/missing") &&
                    !material.set_shader("project/shaders/missing") &&
                    material.type_path() == "project/materials/tests/material-contracts" &&
                    material.shader_path() == "project/shaders/tests/material-contracts",
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
        !expect(material.set_shader("builtin/shaders/unlit") &&
                    material.type_path() == "project/materials/tests/material-contracts" &&
                    material.shader_path() == "builtin/shaders/unlit" &&
                    !material.get_v2("pos", vector2) && !material.get_float("metallic", scalar) &&
                    material.get_v4("baseColor", vector4) && vector4.x() == 0.7,
                "shader replacement keeps material identity and rebuilds its exposed parameters") ||
        !expect(material.set_type("builtin/materials/default-pbr") &&
                    material.shader_path() == "builtin/shaders/pbr" &&
                    material.get_float("roughness", scalar) && scalar == 0.4F,
                "changing material restores new definition and shader defaults"))
    {
        return false;
    }
    return true;
}

bool resource_namespaces()
{
    const auto* builtin = engine::ResourceSystem::find_shader("builtin/shaders/pbr");
    if (!expect(builtin != nullptr, "built-in definitions survive material tests"))
    {
        return false;
    }
    engine::schema::ShaderDataT shader = *builtin;
    shader.path = "builtin/shaders/tests/shared";
    if (!expect(engine::ResourceSystem::register_shader(shader),
                "register built-in shader namespace"))
    {
        return false;
    }
    shader.path = "project/shaders/tests/shared";
    if (!expect(engine::ResourceSystem::register_shader(shader),
                "same relative shader name is independent in project namespace"))
    {
        return false;
    }
    engine::schema::MaterialDataT material;
    material.type_path = "builtin/materials/tests/shared";
    material.shader_path = "builtin/shaders/tests/shared";
    if (!expect(engine::ResourceSystem::register_material(material),
                "built-in material references built-in shader"))
    {
        return false;
    }
    material.type_path = "project/materials/tests/shared";
    material.shader_path = "project/shaders/tests/shared";
    if (!expect(engine::ResourceSystem::register_material(material),
                "same relative material name is independent in project namespace"))
    {
        return false;
    }
    material.type_path = "project/materials/tests/reuse-builtin";
    material.shader_path = "builtin/shaders/pbr";
    if (!expect(engine::ResourceSystem::register_material(material),
                "project material may reuse built-in shader"))
    {
        return false;
    }
    material.type_path = "builtin/materials/tests/invalid-dependency";
    material.shader_path = "project/shaders/tests/shared";
    if (!expect(!engine::ResourceSystem::register_material(material) &&
                    engine::ResourceSystem::find_material(material.type_path) == nullptr,
                "built-in material cannot depend on project shader"))
    {
        return false;
    }
    const auto* builtin_material =
        engine::ResourceSystem::find_material("builtin/materials/tests/shared");
    const auto* project_material =
        engine::ResourceSystem::find_material("project/materials/tests/shared");
    if (!expect(builtin_material != nullptr && project_material != nullptr &&
                    builtin_material->shader_path == "builtin/shaders/tests/shared" &&
                    project_material->shader_path == "project/shaders/tests/shared",
                "both namespaces resolve their own distinct definitions"))
    {
        return false;
    }
    // All scene/world owners from the preceding tests have already been destroyed.
    engine::ResourceSystem::clear_project_definitions();
    engine::ResourceSystem::clear_project_definitions();
    return expect(engine::ResourceSystem::find_shader("project/shaders/tests/shared") == nullptr &&
                      engine::ResourceSystem::find_material("project/materials/tests/shared") ==
                          nullptr &&
                      engine::ResourceSystem::find_material(
                          "project/materials/tests/reuse-builtin") == nullptr &&
                      engine::ResourceSystem::find_shader(
                          "project/shaders/tests/material-contracts") == nullptr,
                  "project cleanup removes only project definitions and is repeatable") &&
           expect(engine::ResourceSystem::find_shader("builtin/shaders/pbr") == builtin &&
                      engine::ResourceSystem::find_shader("builtin/shaders/tests/shared") !=
                          nullptr &&
                      engine::ResourceSystem::find_material("builtin/materials/tests/shared") ==
                          builtin_material &&
                      engine::ResourceSystem::find_material("builtin/materials/default-pbr") !=
                          nullptr,
                  "project cleanup preserves built-in definitions and borrowed references");
}

} // namespace

int main()
{
    const engine::EngineConfig config;
    engine::ResourceSystem resources;
    if (!missing_material_resources() || !resources.initialize(config) || !scene_lifecycle() ||
        !hierarchy_and_transforms() || !component_contracts() || !material_contracts() ||
        !resource_namespaces())
    {
        return 1;
    }
    resources.shutdown();
    if (!expect(engine::ResourceSystem::find_shader("builtin/shaders/pbr") == nullptr &&
                    engine::ResourceSystem::find_material("builtin/materials/tests/shared") ==
                        nullptr,
                "resource host shutdown clears all namespaces"))
    {
        return 1;
    }
    std::puts("Scene, components, material parameters and resource namespaces passed.");
    return 0;
}
