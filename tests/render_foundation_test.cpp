#include "resource_fixture.hpp"

#if defined(ENGINE_TEST_RESOURCE_GRAPH)
#include "plugins/runtime/resource_graph/resource_graph_system.hpp"
#endif

#include "runtime/engine_config.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/utils/mesh_utils.hpp"
#include "runtime/world/world_system.hpp"

#include <cstdio>
#include <limits>

namespace
{

bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "render foundation test: %s\n", message);
    }
    return value;
}

engine::schema::MeshDataT quad()
{
    engine::schema::MeshDataT result;
    result.path = "project/meshes/quad";
    result.vertices = {
        {-1, -1, 0, 0, 0, 1}, {1, -1, 0, 0, 0, 1}, {1, 1, 0, 0, 0, 1}, {-1, 1, 0, 0, 0, 1}};
    result.indices = {0, 1, 2, 0, 2, 3};
    result.texcoords = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    result.tangents.assign(4, {1, 0, 0, 1});
    result.colors.assign(4, {1, 1, 1, 1});
    result.submeshes = {{0, 3, 0}, {3, 3, 1}};
    return result;
}

bool check_camera_and_light(engine::Scene& scene)
{
    auto* camera_node = scene.create_node("Camera");
    auto* light_node = scene.create_node("Light");
    auto* camera = camera_node ? camera_node->add_component<engine::Camera>() : nullptr;
    auto* light = light_node ? light_node->add_component<engine::Light>() : nullptr;
    if (!expect(camera && light, "create camera and light as node components"))
    {
        return false;
    }
    auto camera_settings = camera->settings();
    camera_settings.projection = engine::schema::Projection::orthographic;
    camera_settings.orthographic_size = 4;
    if (!expect(camera->set_settings(camera_settings), "accept orthographic projection"))
    {
        return false;
    }
    auto invalid_camera = camera_settings;
    invalid_camera.orthographic_size = 0;
    if (!expect(!camera->set_settings(invalid_camera) && camera->settings() == camera_settings,
                "invalid projection leaves previous camera settings intact"))
    {
        return false;
    }
    invalid_camera = camera_settings;
    invalid_camera.projection = static_cast<engine::schema::Projection>(255);
    if (!expect(!camera->set_settings(invalid_camera), "reject unknown projection kind"))
    {
        return false;
    }

    auto light_settings = light->settings();
    for (const auto kind : {engine::schema::LightKind::directional,
                            engine::schema::LightKind::point, engine::schema::LightKind::spot})
    {
        light_settings.kind = kind;
        light_settings.range = 12;
        light_settings.inner_angle = 15;
        light_settings.outer_angle = 35;
        if (!expect(light->set_settings(light_settings), "accept supported light kind"))
        {
            return false;
        }
    }
    auto invalid_light = light_settings;
    invalid_light.range = -1;
    if (!expect(!light->set_settings(invalid_light) && light->settings() == light_settings,
                "invalid light range preserves previous settings"))
    {
        return false;
    }
    invalid_light = light_settings;
    invalid_light.inner_angle = invalid_light.outer_angle + 1;
    if (!expect(!light->set_settings(invalid_light), "reject inverted spotlight cone") ||
        !expect(scene.set_active_camera(camera), "set active orthographic camera"))
    {
        return false;
    }
    engine::schema::SceneDataT snapshot;
    if (!expect(scene.build_data(snapshot), "serialize camera and light generated data"))
    {
        return false;
    }
    bool found_camera = false;
    bool found_light = false;
    for (const auto& node : snapshot.nodes)
    {
        for (const auto& component : node->components)
        {
            if (const auto* value = component->payload.AsCameraData())
            {
                found_camera = *value == camera_settings;
            }
            if (const auto* value = component->payload.AsLightData())
            {
                found_light = *value == light_settings;
            }
        }
    }
    return expect(found_camera && found_light, "snapshot keeps projection and full light data");
}

bool check_mesh(engine::Scene& scene)
{
    auto* node = scene.create_node("Editable mesh");
    auto* renderer = node ? node->add_component<engine::MeshRenderer>() : nullptr;
    if (!expect(renderer && renderer->material_count() == 1,
                "mesh starts with the compatible first material slot") ||
        !expect(renderer->set_material_count(2) && renderer->material(1) != nullptr &&
                    renderer->material(2) == nullptr,
                "material slots have explicit bounds") ||
        !expect(!renderer->set_material_count(0) && !renderer->set_material_count(17),
                "material slot count stays within supported bounds"))
    {
        return false;
    }
    auto data = quad();
    const auto old_revision = renderer->mesh_revision();
    if (!expect(renderer->set_mesh_data(data) && renderer->mesh_revision() > old_revision,
                "dynamic mesh submission advances its revision") ||
        !expect(renderer->mesh_data() && *renderer->mesh_data() == data,
                "dynamic mesh preserves UV, tangent, color and submesh data"))
    {
        return false;
    }
    const auto revision = renderer->mesh_revision();
    data.vertices.front().mutate_x(-2);
    if (!expect(renderer->mesh_data()->vertices.front().x() == -1,
                "submitted mesh owns an independent copy of caller data"))
    {
        return false;
    }
    auto invalid = data;
    invalid.indices.back() = 99;
    if (!expect(!renderer->set_mesh_data(invalid) && renderer->mesh_revision() == revision,
                "out-of-range indices reject the complete update"))
    {
        return false;
    }
    invalid = data;
    invalid.texcoords.pop_back();
    if (!expect(!renderer->set_mesh_data(invalid), "reject incomplete optional vertex channel"))
    {
        return false;
    }
    invalid = data;
    invalid.vertices.front().mutate_x(std::numeric_limits<float>::infinity());
    if (!expect(!renderer->set_mesh_data(invalid), "reject non-finite positions"))
    {
        return false;
    }
    invalid = data;
    invalid.submeshes = {{0, 3, 0}, {0, 3, 1}};
    return expect(!renderer->set_mesh_data(invalid), "reject overlapping submesh ranges") &&
           expect(renderer->set_mesh_data(data) && renderer->mesh_revision() > revision,
                  "valid geometry update recovers after rejected data") &&
           expect(!renderer->set_material_count(1),
                  "cannot remove material slots referenced by the active mesh") &&
           expect(renderer->set_mesh("builtin/meshes/cube") && renderer->mesh_data() != nullptr,
                  "resource mesh can replace dynamic geometry") &&
           expect(renderer->set_material_count(1) && renderer->material(1) == nullptr,
                  "removing an unused material slot invalidates its lookup");
}

bool check_deformation(engine::Scene& scene)
{
    auto* node = scene.create_node("Deformed mesh");
    auto* renderer = node ? node->add_component<engine::MeshRenderer>() : nullptr;
    if (!expect(renderer && renderer->set_material_count(2), "create deformation owner"))
    {
        return false;
    }
    const auto* source = renderer->mesh_data();
    const auto original = *source;
    const auto source_revision = renderer->mesh_revision();
    const auto source_instance = renderer->mesh_instance_id();
    engine::schema::ComponentDataT snapshot;
    std::vector<engine::Vec4> tangents;
    auto vertices = source->vertices;
    if (!expect(renderer->build_data(snapshot) &&
                    engine::MeshUtils::build_tangents(*source, tangents),
                "capture bind geometry and persistent identity"))
    {
        return false;
    }
    for (auto& vertex : vertices)
    {
        vertex.mutate_x(vertex.x() + 3.0F);
    }
    if (!expect(renderer->set_deformation(vertices, tangents), "accept complete deformed frame"))
    {
        return false;
    }
    const auto revision = renderer->deformation_revision();
    const auto first_x = renderer->deformed_vertices().front().x();
    vertices.front().mutate_x(50.0F);
    tangents.front().mutate_w(-tangents.front().w());
    engine::schema::ComponentDataT after;
    if (!expect(renderer->deformed_vertices().front().x() == first_x &&
                    renderer->deformed_tangents().front().w() != tangents.front().w(),
                "deformation owns caller-independent vertex and tangent copies") ||
        !expect(renderer->mesh_data() == source && *renderer->mesh_data() == original &&
                    renderer->mesh_revision() == source_revision &&
                    renderer->mesh_path() == "builtin/meshes/cube" &&
                    renderer->material_count() == 2,
                "deformation preserves bind data, GUID ownership and material slots") ||
        !expect(renderer->build_data(after) && after == snapshot,
                "persistent snapshot excludes the transient pose"))
    {
        return false;
    }
    auto invalid = vertices;
    invalid.pop_back();
    if (!expect(!renderer->set_deformation(invalid, tangents) &&
                    !renderer->set_deformation(vertices, {}),
                "reject mismatched deformation streams"))
    {
        return false;
    }
    invalid = vertices;
    invalid.front().mutate_x(std::numeric_limits<float>::quiet_NaN());
    if (!expect(!renderer->set_deformation(invalid, tangents), "reject nonfinite pose"))
    {
        return false;
    }
    invalid = vertices;
    invalid.front().mutate_nx(0);
    invalid.front().mutate_ny(0);
    invalid.front().mutate_nz(0);
    auto invalid_tangents = tangents;
    invalid_tangents.front().mutate_w(0);
    if (!expect(!renderer->set_deformation(invalid, tangents) &&
                    !renderer->set_deformation(vertices, invalid_tangents),
                "reject zero normals and invalid tangent handedness"))
    {
        return false;
    }
    invalid_tangents = tangents;
    invalid_tangents.front() = {vertices.front().nx(), vertices.front().ny(), vertices.front().nz(),
                                1};
    if (!expect(!renderer->set_deformation(vertices, invalid_tangents) &&
                    renderer->deformation_revision() == revision &&
                    renderer->deformed_vertices().front().x() == first_x,
                "invalid tangent frame leaves the last complete pose intact") ||
        !expect(!renderer->set_mesh("project/missing.mesh") &&
                    renderer->deformation_revision() == revision &&
                    !renderer->deformed_vertices().empty(),
                "failed source change retains deformation"))
    {
        return false;
    }
    renderer->clear_deformation();
    const auto cleared_revision = renderer->deformation_revision();
    renderer->clear_deformation();
    if (!expect(renderer->deformed_vertices().empty() && renderer->deformed_tangents().empty() &&
                    cleared_revision > revision &&
                    renderer->deformation_revision() == cleared_revision &&
                    renderer->mesh_data() == source,
                "clear restores source geometry and is repeatable") ||
        !expect(renderer->set_deformation(vertices, tangents) && renderer->restore_data(snapshot) &&
                    renderer->deformed_vertices().empty(),
                "restoring persistent data discards transient pose") ||
        !expect(renderer->set_deformation(vertices, tangents) &&
                    renderer->set_mesh("builtin/meshes/cube") &&
                    renderer->deformed_vertices().empty(),
                "successful source replacement discards transient pose"))
    {
        return false;
    }
    if (!expect(renderer->set_deformation(vertices, tangents) && renderer->set_mesh_data(quad()) &&
                    renderer->deformed_vertices().empty() &&
                    renderer->mesh_instance_id() == source_instance,
                "dynamic topology replacement discards pose and retains instance identity") ||
        !node->remove_component(engine::MeshRenderer::component_type))
    {
        return false;
    }
    renderer = node->add_component<engine::MeshRenderer>();
    return expect(renderer != nullptr && renderer->mesh_instance_id() != 0 &&
                      renderer->mesh_instance_id() != source_instance,
                  "replacement component has distinct identity even on the same node");
}

bool check_textures(engine::ResourceSystem& resources, engine::Scene& scene, bool shared)
{
    test_support::ResourceFixture fixture;
    engine::schema::TextureDataT texture;
    texture.path = "project/textures/gray";
    texture.width = 1;
    texture.height = 1;
    texture.encoding = engine::schema::TextureEncoding::rgba8;
    texture.format = engine::schema::TextureFormat::rgba8;
    texture.data = {128, 128, 128, 255};
    if (!expect(fixture.add(texture) && fixture.publish(resources),
                "publish a texture through the GUID resource catalog"))
    {
        return false;
    }
    auto* first_node = scene.create_node("First textured mesh");
    auto* second_node = scene.create_node("Second textured mesh");
    auto* first = first_node ? first_node->add_component<engine::MeshRenderer>() : nullptr;
    auto* second = second_node ? second_node->add_component<engine::MeshRenderer>() : nullptr;
    if (!expect(first && second, "create texture owners") ||
        !expect(first->material().set_texture("baseColorTexture", texture.path) &&
                    second->material().set_texture("baseColorTexture", texture.path),
                "resolve texture by named shader sampler"))
    {
        return false;
    }
    const auto& first_texture = first->material().texture("baseColorTexture");
    const auto& second_texture = second->material().texture("baseColorTexture");
    std::weak_ptr<const engine::Resource> first_held = first_texture;
    std::weak_ptr<const engine::Resource> second_held = second_texture;
    std::string_view path;
    if (!expect(first_texture && second_texture && first_texture->texture() &&
                    second_texture->texture() && (first_texture == second_texture) == shared &&
                    *first_texture->texture() == *second_texture->texture(),
                "core loads distinct texture entities; injected graph shares matching entities") ||
        !expect(first->material().get_texture("baseColorTexture", path) && path == texture.path,
                "query returns the resolved texture path") ||
        !expect(!first->material().set_texture("missingSampler", texture.path) &&
                    !first->material().set_texture("baseColorTexture", "builtin/meshes/cube") &&
                    !first->material().set_texture("baseColorTexture", "project/missing.png") &&
                    first->material().get_texture("baseColorTexture", path) && path == texture.path,
                "invalid texture assignments preserve current resource"))
    {
        return false;
    }
    const auto first_id = first_node->id();
    const auto second_id = second_node->id();
    return expect(scene.destroy_node(first_id) && first_held.expired() != shared &&
                      !second_held.expired(),
                  "destroying a material releases only resources without remaining owners") &&
           expect(scene.destroy_node(second_id) && first_held.expired() && second_held.expired(),
                  "last material owner releases every loaded texture entity");
}

} // namespace

int main()
{
    const engine::EngineConfig config;
    engine::ResourceSystem resources;
#if defined(ENGINE_TEST_RESOURCE_GRAPH)
    engine::plugins::resource_graph::ResourceGraphSystem graph(resources);
#endif
    engine::WorldSystem world;
    if (!expect(resources.initialize(config) && world.initialize(config),
                "initialize resource and world systems"))
    {
        return 1;
    }
    auto* scene = world.create_scene("Render foundation");
    if (!expect(scene != nullptr, "create scene") || !check_camera_and_light(*scene) ||
        !check_mesh(*scene) || !check_deformation(*scene) ||
        !check_textures(resources, *scene, false))
    {
        return 1;
    }
#if defined(ENGINE_TEST_RESOURCE_GRAPH)
    if (!expect(graph.initialize(config), "initialize optional dependency graph"))
    {
        return 1;
    }
    resources.set_loader(&graph);
    if (!check_textures(resources, *scene, true))
    {
        return 1;
    }
    resources.set_loader(nullptr);
#endif
    std::puts("Render component validation, dynamic mesh ownership and texture sharing passed.");
    return 0;
}
