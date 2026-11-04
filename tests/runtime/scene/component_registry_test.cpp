#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/component_registry.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"
#include "scene_snapshot.hpp"

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

// An unregistered plugin slot exercises type creation, not duplicate-registration rejection.
struct RegistrationRuntime
{
    unsigned int value = 7;
};

class RegistrationProbe final : public engine::Component
{
public:
    using Data = engine::schema::TerrainDataT;
    static constexpr auto component_type = engine::ComponentType::TerrainData;
    explicit RegistrationProbe(engine::Component value) : Component(value, component_type)
    {
    }
    static bool register_type(engine::ComponentRegistry& registry)
    {
        return engine::register_component_storage<RegistrationProbe, Data, RegistrationRuntime>(
            registry);
    }
    bool restore_data(const engine::schema::ComponentDataT& input)
    {
        const auto* value = input.payload.AsTerrainData();
        if (!writable() || !input.flags || !value)
        {
            return false;
        }
        *write_data<Data>() = *value;
        return restore_flags(input);
    }
};

bool registration_phase_guard()
{
    engine::WorldSystem world;
    if (!world.initialize({}))
    {
        return false;
    }
    auto& registry = world.component_registry();
    auto* native = world.native_world();
    const auto type_count = ecs_count_id(native, ecs_id(EcsComponent));
    if (!expect(!registry.contains(RegistrationProbe::component_type),
                "the probe is an unregistered plugin type"))
    {
        return false;
    }
    ecs_readonly_begin(native, true);
    const bool readonly_rejected = !registry.add<RegistrationProbe>();
    ecs_readonly_end(native);
    if (!expect(readonly_rejected && !registry.contains(RegistrationProbe::component_type) &&
                    ecs_count_id(native, ecs_id(EcsComponent)) == type_count,
                "readonly registration leaves registry and native ECS types untouched"))
    {
        return false;
    }
    const auto marker = ecs_new(native);
    auto* stage = ecs_get_stage(native, 0);
    ecs_readonly_begin(native, false);
    ecs_add_id(stage, marker, EcsDisabled);
    const bool staged_rejected =
        !registry.add<RegistrationProbe>() && !ecs_has_id(native, marker, EcsDisabled);
    ecs_readonly_end(native);
    const bool merged = ecs_has_id(native, marker, EcsDisabled);
    ecs_delete(native, marker);
    if (!expect(staged_rejected && merged &&
                    !registry.contains(RegistrationProbe::component_type) &&
                    ecs_count_id(native, ecs_id(EcsComponent)) == type_count,
                "single-thread readonly registration cannot hide new types in a native stage") ||
        !expect(registry.add<RegistrationProbe>(),
                "the same plugin can register when the structural phase is stable"))
    {
        return false;
    }
    auto* scene = world.create_scene("Registration phase");
    auto node = scene ? scene->create_node("Probe") : engine::Node{};
    auto component = registry.create(RegistrationProbe::component_type, node);
    engine::schema::ComponentDataT snapshot;
    flecs::world ecs(native);
    const auto* runtime = node ? ecs.entity(node.id()).try_get<RegistrationRuntime>() : nullptr;
    return expect(component && component.build_data(snapshot) &&
                      snapshot.payload.type == RegistrationProbe::component_type && runtime &&
                      runtime->value == 7 &&
                      node.remove_component(RegistrationProbe::component_type) && !component,
                  "successful registration creates and removes both data and runtime columns");
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
    auto node = scene ? scene->create_node("Node") : engine::Node{};
    auto& registry = world.component_registry();
    if (!expect(node && registry.contains(engine::Light::component_type), "register defaults") ||
        !expect(!registry.add<engine::Light>(), "reject duplicate registration") ||
        !expect(!registry.create(static_cast<engine::ComponentType>(255), node),
                "unknown component fails without changing the node"))
    {
        return false;
    }
    auto light = registry.create(engine::Light::component_type, node);
    if (!expect(light && !registry.create(engine::Light::component_type, node),
                "registered storage uses ECS ownership and uniqueness"))
    {
        return false;
    }
    engine::schema::ComponentDataT snapshot;
    snapshot.flags = std::make_unique<engine::schema::ComponentFlagsT>();
    if (!light.build_data(snapshot))
    {
        return false;
    }
    snapshot.flags->enabled = false;
    snapshot.payload.AsLightData()->intensity = 2.5F;
    if (!expect(light.restore_data(snapshot) && !light.enabled() &&
                    node.get_component<engine::Light>().settings().intensity == 2.5F,
                "restore dispatches through Component interface"))
    {
        return false;
    }
    snapshot.flags->enabled = true;
    snapshot.payload.AsLightData()->intensity = std::numeric_limits<float>::quiet_NaN();
    if (!expect(!light.restore_data(snapshot) && !light.enabled() &&
                    node.get_component<engine::Light>().settings().intensity == 2.5F,
                "invalid typed restore leaves previous state unchanged"))
    {
        return false;
    }
    auto component = registry.create(engine::MeshRenderer::component_type, node);
    auto mesh = node.get_component<engine::MeshRenderer>();
    if (!expect(component && component.build_data(snapshot), "mesh snapshot through interface"))
    {
        return false;
    }
    if (!expect(!mesh.static_instancing_enabled() && !mesh.static_lod_enabled() &&
                    mesh.set_static_instancing_enabled(true) && mesh.set_static_lod_enabled(true) &&
                    component.build_data(snapshot) &&
                    snapshot.payload.AsMeshRendererData()->static_instancing_enabled &&
                    snapshot.payload.AsMeshRendererData()->static_lod_enabled,
                "optional mesh choices default off and opt-in is stored in generated ECS data") ||
        !expect(mesh.set_mesh("builtin://meshes/cube") && mesh.static_instancing_enabled() &&
                    mesh.static_lod_enabled(),
                "changing the mesh resource preserves this instance's submission choices") ||
        !expect(mesh.set_static_instancing_enabled(false) && mesh.set_static_lod_enabled(false) &&
                    component.restore_data(snapshot) && mesh.static_instancing_enabled() &&
                    mesh.static_lod_enabled(),
                "component restore retains both explicitly enabled choices"))
    {
        return false;
    }
    auto camera = node.add_component<engine::Camera>();
    auto lens = camera.settings();
    if (!expect(camera && !lens.frustum_culling_enabled,
                "optional camera frustum culling defaults off"))
    {
        return false;
    }
    lens.frustum_culling_enabled = true;
    engine::schema::ComponentDataT camera_snapshot;
    if (!expect(camera.set_settings(lens) && camera.build_data(camera_snapshot) &&
                    camera_snapshot.payload.AsCameraData()->frustum_culling_enabled,
                "camera stores its visibility policy through the normal settings contract"))
    {
        return false;
    }
    lens.frustum_culling_enabled = false;
    if (!expect(camera.set_settings(lens) && camera.restore_data(camera_snapshot) &&
                    camera.settings().frustum_culling_enabled,
                "camera restore preserves explicit opt-in without separate renderer state"))
    {
        return false;
    }
    ecs_readonly_begin(world.native_world(), false);
    const bool rejected =
        !mesh.set_static_instancing_enabled(false) && !mesh.set_static_lod_enabled(false);
    ecs_readonly_end(world.native_world());
    if (!expect(rejected && mesh.static_instancing_enabled() && mesh.static_lod_enabled(),
                "readonly phases cannot partially change either mesh submission attribute"))
    {
        return false;
    }
    const auto* geometry = mesh.mesh_data();
    const auto revision = mesh.mesh_revision();
    auto invalid = snapshot;
    invalid.payload.AsMeshRendererData()->additional_material_guids = {
        "00000000-0000-4000-8000-000000000099"};
    if (!expect(!component.restore_data(invalid) && mesh.mesh_data() == geometry &&
                    mesh.mesh_revision() == revision && mesh.material_count() == 1,
                "late material failure rolls back mesh resources and slots") ||
        !expect(component.restore_data(snapshot) && mesh.mesh_revision() == revision + 1,
                "complete restore commits once"))
    {
        return false;
    }
    engine::schema::ComponentDataT mismatch;
    mismatch.flags = std::make_unique<engine::schema::ComponentFlagsT>();
    mismatch.payload.Set(engine::schema::CameraDataT{});
    engine::schema::SceneDataT scene_data;
    if (!expect(!component.restore_data(mismatch), "reject incompatible payload") ||
        !expect(scene->build_data(scene_data),
                "scene snapshots registered components without type dispatch"))
    {
        return false;
    }
    const auto old = mesh;
    if (!node.remove_component(engine::MeshRenderer::component_type))
    {
        return false;
    }
    auto replacement = node.add_component<engine::MeshRenderer>();
    auto stale = old;
    return expect(replacement && !replacement.static_instancing_enabled() &&
                      !replacement.static_lod_enabled() && !stale.static_instancing_enabled() &&
                      !stale.static_lod_enabled() && !stale.set_static_instancing_enabled(true) &&
                      !stale.set_static_lod_enabled(true),
                  "component recreation uses defaults without reviving stale attribute handles");
}

} // namespace

int main()
{
    return registration_phase_guard() && check() ? 0 : 1;
}
