#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/component_registry.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

#include <cstdio>
#include <limits>

namespace
{

bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "component registry: %s\n", message);
    }
    return value;
}

bool check()
{
    engine::ResourceSystem resources;
    engine::WorldSystem world;
    const engine::EngineConfig config;
    if (!resources.initialize(config) || !world.initialize(config))
    {
        return false;
    }
    auto* scene = world.create_scene("Components");
    auto* node = scene ? scene->create_node("Node") : nullptr;
    engine::ComponentRegistry registry;
    if (!expect(node && engine::register_scene_components(registry), "register defaults") ||
        !expect(!registry.add<engine::Light>(), "reject duplicate registration") ||
        !expect(!registry.create(static_cast<engine::ComponentType>(255), *node),
                "unknown component fails without changing the node"))
    {
        return false;
    }
    auto* light = registry.create(engine::Light::component_type, *node);
    if (!expect(light && !registry.create(engine::Light::component_type, *node),
                "registered factory uses Node ownership and uniqueness"))
    {
        return false;
    }
    engine::schema::ComponentDataT snapshot;
    if (!light->build_data(snapshot))
    {
        return false;
    }
    snapshot.enabled = false;
    snapshot.payload.AsLightData()->intensity = 2.5F;
    if (!expect(light->restore_data(snapshot) && !light->enabled() &&
                    node->get_component<engine::Light>()->settings().intensity == 2.5F,
                "restore dispatches through Component interface"))
    {
        return false;
    }
    snapshot.enabled = true;
    snapshot.payload.AsLightData()->intensity = std::numeric_limits<float>::quiet_NaN();
    if (!expect(!light->restore_data(snapshot) && !light->enabled() &&
                    node->get_component<engine::Light>()->settings().intensity == 2.5F,
                "invalid typed restore leaves previous state unchanged"))
    {
        return false;
    }
    auto* component = registry.create(engine::MeshRenderer::component_type, *node);
    auto* mesh = node->get_component<engine::MeshRenderer>();
    if (!expect(component && component->build_data(snapshot), "mesh snapshot through interface"))
    {
        return false;
    }
    const auto* geometry = mesh->mesh_data();
    const auto revision = mesh->mesh_revision();
    auto invalid = snapshot;
    invalid.payload.AsMeshRendererData()->additional_material_guids = {
        "00000000-0000-4000-8000-000000000099"};
    if (!expect(!component->restore_data(invalid) && mesh->mesh_data() == geometry &&
                    mesh->mesh_revision() == revision && mesh->material_count() == 1,
                "late material failure rolls back mesh resources and slots") ||
        !expect(component->restore_data(snapshot) && mesh->mesh_revision() == revision + 1,
                "complete restore commits once"))
    {
        return false;
    }
    engine::schema::ComponentDataT mismatch;
    mismatch.payload.Set(engine::schema::CameraDataT{});
    engine::schema::SceneDataT scene_data;
    return expect(!component->restore_data(mismatch), "reject incompatible payload") &&
           expect(scene->build_data(scene_data),
                  "scene snapshots registered components without type dispatch");
}

} // namespace

int main()
{
    return check() ? 0 : 1;
}
