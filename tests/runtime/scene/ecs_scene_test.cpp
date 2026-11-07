#include "resource_fixture.hpp"
#include "resource_services.hpp"
#include "scene_snapshot.hpp"

#include "runtime/engine_config.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/world/world_system.hpp"

#include <array>
#include <cstdio>
#include <type_traits>
#include <vector>

namespace
{
using namespace engine;

static_assert(std::is_trivially_copyable_v<Node>);
static_assert(std::is_trivially_copyable_v<Transform>);
static_assert(!std::is_polymorphic_v<Component>);
static_assert(sizeof(Node) <= sizeof(void*) + 2 * sizeof(std::uint64_t));

bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "ecs scene: %s\n", message);
    }
    return value;
}

bool light_drive_valid(Node owner, std::uint64_t instance) noexcept
{
    const auto light = owner.get_component<Light>();
    return light && light.instance_id() == instance && light.enabled();
}

bool producer_transform_drive()
{
    WorldSystem world;
    if (!world.initialize({}))
    {
        return false;
    }
    auto* scene = world.create_scene("Generic transform producer");
    auto owner = scene ? scene->create_node("Owner") : Node{};
    auto child = scene ? scene->create_node("Child") : Node{};
    auto light = owner.add_component<Light>();
    auto transform = child.get_component<Transform>();
    auto settings = test_support::read_settings(transform);
    settings.position = {1, 0, 0};
    if (!light || !child.set_parent(owner) || !transform.set_settings(settings))
    {
        return false;
    }
    ecs_detail::TransformDrive drive;
    drive.owner_type = Light::component_type;
    drive.owner_instance_id = light.instance_id();
    drive.validate = light_drive_valid;
    drive.local[12] = 9;
    drive.active = true;
    flecs::world(world.native_world()).entity(owner.id()).set<ecs_detail::TransformDrive>(drive);
    world.invalidate_scene_state();
    std::array<float, 16> matrix;
    if (!expect(transform.world_matrix(matrix.data()) && matrix[12] == 10,
                "World consumes a valid drive without knowing its producer type") ||
        !light.set_enabled(false))
    {
        return false;
    }
    // A producer marks changes to the state inspected by its validator.
    world.invalidate_scene_state();
    if (!expect(transform.world_matrix(matrix.data()) && matrix[12] == 1,
                "producer validation rejects disabled state before the next system tick") ||
        !light.set_enabled(true))
    {
        return false;
    }
    world.invalidate_scene_state();
    if (!expect(transform.world_matrix(matrix.data()) && matrix[12] == 10,
                "a still-live producer may resume its validated drive") ||
        !owner.remove_component(Light::component_type))
    {
        return false;
    }
    light = owner.add_component<Light>();
    return expect(light && transform.world_matrix(matrix.data()) && matrix[12] == 1,
                  "a new component instance cannot revive the removed producer's drive");
}

bool scene_lifetime_phases()
{
    test_support::ResourceServices services;
    auto& resources = services.core;
    WorldSystem world(&services.loader);
    test_support::ResourceFixture fixture;
    if (!services.initialize({}) || !world.initialize({}))
    {
        return false;
    }
    auto* active = world.create_scene("Active");
    auto node = active ? active->create_node("Retained until safe point") : Node{};
    schema::SceneDataT snapshot;
    engine::ResourceReference resource;
    if (!node || !active->build_data(snapshot) || !fixture.add("phases.scene", snapshot) ||
        !fixture.publish(resources) || !services.load("phases.scene", resource))
    {
        return false;
    }
    for (bool multi_threaded : {true, false})
    {
        const auto request = world.begin_restore_scene(resource.handle());
        if (!request || world.step_restore_scene(request, 64) != SceneRestoreStatus::ready)
        {
            return false;
        }
        {
            auto query =
                flecs::world(world.native_world())
                    .query_builder<const schema::NodePropertiesT, const ecs_detail::NodeRuntime>()
                    .without<ecs_detail::Published>()
                    .build();
            std::size_t candidates = 0;
            bool isolated = true;
            query.each(
                [&](flecs::entity entity, const schema::NodePropertiesT& properties,
                    const ecs_detail::NodeRuntime& state)
                {
                    ++candidates;
                    const auto found = state.scene->find_local_node(properties.id);
                    isolated = isolated && state.scene != active && found &&
                               found.id() == entity.id() &&
                               state.scene->find_node(properties.name) == found &&
                               !world.find_node(entity.id());
                });
            if (!expect(isolated && candidates == 1 && world.scenes().size() == 1 &&
                            world.scene() == active && active->find_node(node.id()) == node,
                        "candidate queries resolve locally without publishing nodes or replacing "
                        "the active owner"))
            {
                return false;
            }
        }
        auto* native = world.native_world();
        const auto marker = ecs_new(native);
        auto* stage = ecs_get_stage(native, 0);
        ecs_readonly_begin(native, multi_threaded);
        ecs_add_id(stage, marker, EcsDisabled);
        const bool staged = ecs_is_stage(stage) && !ecs_is_stage(native) &&
                            !ecs_has_id(native, marker, EcsDisabled);
        const bool rejected = !world.destroy_scene(active->id()) &&
                              !world.load_scene("phases.scene") &&
                              world.step_restore_scene(request, 64) == SceneRestoreStatus::failed &&
                              !world.commit_restore_scene(request);
        world.cancel_restore_scene(request);
        world.unload_scene();
        world.destroy_scene();
        const bool owners_retained = world.scene() == active && node.valid();
        ecs_readonly_end(native);
        const bool merged = ecs_has_id(native, marker, EcsDisabled);
        ecs_delete(native, marker);
        if (!expect(staged && merged && rejected && owners_retained && world.scene() == active &&
                        node && world.step_restore_scene(request, 0) == SceneRestoreStatus::ready,
                    "both readonly modes isolate stage commands and retain scene/candidate owners"))
        {
            return false;
        }
        world.cancel_restore_scene(request);
        if (world.step_restore_scene(request, 0) != SceneRestoreStatus::failed)
        {
            return false;
        }
    }
    world.destroy_scene();
    return expect(!node && world.scene() == nullptr,
                  "scene destruction runs after all ECS borrows have ended");
}

bool check()
{
    WorldSystem world;
    if (!world.initialize({}))
    {
        return false;
    }
    auto* scene = world.create_scene("ECS many objects");
    if (!scene)
    {
        return false;
    }
    auto root = scene->create_node("Root");
    std::vector<Node> nodes;
    std::vector<Transform> transforms;
    constexpr std::size_t count = 10000;
    nodes.reserve(count);
    transforms.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        auto node = scene->create_node("Object");
        auto transform = node.get_component<Transform>();
        TransformSettings value;
        value.position = {static_cast<double>(i), 0, 0};
        if (!node || !node.set_parent(root) || !transform.set_settings(value))
        {
            return false;
        }
        nodes.push_back(node);
        transforms.push_back(transform);
    }
    const auto original = transforms.front();
    for (std::size_t i = 0; i < count; i += 2)
    {
        // Add/remove forces archetype moves; the captured handles keep attachment identity.
        auto light = nodes[i].add_component<Light>();
        if (!light || !nodes[i].remove_component(Light::component_type) || light.valid() ||
            !transforms[i].valid())
        {
            return false;
        }
    }
    if (!expect(original == nodes.front().get_component<Transform>(),
                "moving ECS tables preserves component identity") ||
        !world.refresh_scene_state())
    {
        return false;
    }
    {
        flecs::world native(world.native_world());
        auto query = native
                         .query_builder<const ecs_detail::ComponentStorage<Transform::Data>,
                                        const ecs_detail::WorldTransform>()
                         .with<ecs_detail::Published>()
                         .build();
        std::size_t matched = 0;
        bool correct = true;
        query.each(
            [&](const auto& stored, const ecs_detail::WorldTransform& cached)
            {
                ++matched;
                correct = correct && cached.valid &&
                          cached.matrix[12] == static_cast<float>(stored.data.position.x());
            });
        if (!expect(correct && matched == count + 1,
                    "typed ECS query reads all authoritative data"))
        {
            return false;
        }
    }
    ecs_readonly_begin(world.native_world(), true);
    const bool readonly_safe = original.valid() &&
                               test_support::read_settings(original).position.x() == 0 &&
                               !root.set_enabled(false) && !scene->create_node("Rejected") &&
                               !nodes.front().remove_component(Transform::component_type);
    ecs_readonly_end(world.native_world());
    if (!expect(readonly_safe && root.enabled() && original.valid(),
                "read-only ECS phase allows reads and rejects facade mutations"))
    {
        return false;
    }
    auto* native = world.native_world();
    const auto marker = ecs_new(native);
    auto* stage = ecs_get_stage(native, 0);
    ecs_readonly_begin(native, false);
    ecs_add_id(stage, marker, EcsDisabled);
    const bool staged_safe = !nodes.front().set_parent({}) &&
                             !ecs_has_id(native, marker, EcsDisabled) &&
                             nodes.front().parent() == root;
    ecs_readonly_end(native);
    const bool merged = ecs_has_id(native, marker, EcsDisabled);
    ecs_delete(native, marker);
    if (!expect(staged_safe && merged && nodes.front().parent() == root,
                "facade rejects queued writes; merging native stage commands keeps its parent"))
    {
        return false;
    }
    if (!root.set_enabled(false) || !root.set_visible(false))
    {
        return false;
    }
    for (const auto node : nodes)
    {
        if (node.active() || node.visible_in_hierarchy() || !node.enabled() || !node.visible())
        {
            return false;
        }
    }
    if (!root.set_enabled(true) || !root.set_visible(true))
    {
        return false;
    }
    if (!expect(nodes.back().active() && nodes.back().visible_in_hierarchy(),
                "parent state changes are immediately visible"))
    {
        return false;
    }
    auto old_transform = transforms.back();
    if (!nodes.back().remove_component(Transform::component_type))
    {
        return false;
    }
    auto replacement = nodes.back().add_component<Transform>();
    if (!expect(replacement && !old_transform && replacement != old_transform,
                "removed and reattached component never revives an old handle"))
    {
        return false;
    }
    const auto dead_parent = scene->create_node("Deleted parent");
    if (!scene->destroy_node(dead_parent.id()) || nodes[0].set_parent(dead_parent) ||
        nodes[0].parent() != root)
    {
        return false;
    }
    const auto stale_node = nodes.front();
    if (!scene->destroy_node(root.id()))
    {
        return false;
    }
    std::vector<Node> remaining;
    if (!expect(!root && !stale_node && !original && scene->collect_nodes(remaining) &&
                    remaining.empty(),
                "parent deletion removes all 10000 descendants and their component data"))
    {
        return false;
    }
    auto old_epoch = scene->create_node("Before shutdown");
    world.shutdown();
    if (!expect(!old_epoch && world.initialize({}) && !old_epoch,
                "world reinitialization rejects handles from the previous epoch"))
    {
        return false;
    }
    scene = world.create_scene("Restart");
    auto fresh = scene ? scene->create_node("Fresh") : Node{};
    return expect(fresh && fresh != old_epoch && !old_epoch,
                  "restart creates independent entities");
}
} // namespace

int main()
{
    if (!check() || !scene_lifetime_phases() || !producer_transform_drive())
    {
        return 1;
    }
    std::puts(
        "ECS 10000 objects: typed data, table moves, hierarchy, deletion and epoch checks passed.");
    return 0;
}
