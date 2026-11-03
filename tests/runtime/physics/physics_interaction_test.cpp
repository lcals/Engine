#include "scene_snapshot.hpp"

#include "runtime/engine_config.hpp"
#include "runtime/physics/mesh_collision_data.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/scene/character_motor.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/rigid_body.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/schema/mesh_generated.h"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"
#include "runtime/world/world_system.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <memory>
#include <vector>

namespace
{

constexpr double fixed_step = 1.0 / 60.0;
engine::TaskSystem* physics_tasks = nullptr;
using engine::PhysicsContactEvent;
using engine::PhysicsContactPhase;
using engine::PhysicsObject;
using engine::PhysicsObjectKind;

bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "physics interaction: %s\n", message);
    }
    return value;
}

bool same(const PhysicsObject& first, const PhysicsObject& second)
{
    return first.scene_id == second.scene_id && first.node_id == second.node_id &&
           first.kind == second.kind && first.instance_id == second.instance_id;
}

PhysicsObject object(engine::Node node, PhysicsObjectKind kind)
{
    const auto instance = kind == PhysicsObjectKind::character
                              ? node.get_component<engine::CharacterMotor>().instance_id()
                              : node.get_component<engine::Collider>().instance_id();
    return {node.scene()->id(), node.id(), kind, instance};
}

bool pair(const PhysicsContactEvent& event, const PhysicsObject& first, const PhysicsObject& second)
{
    return (same(event.a, first) && same(event.b, second)) ||
           (same(event.a, second) && same(event.b, first));
}

std::vector<PhysicsContactEvent> events(const engine::PhysicsSystem& physics,
                                        const PhysicsObject& first, const PhysicsObject& second)
{
    std::vector<PhysicsContactEvent> result;
    for (const auto& event : physics.contacts())
    {
        if (pair(event, first, second))
        {
            result.push_back(event);
        }
    }
    return result;
}

bool empty(const engine::PhysicsQueryHit& hit)
{
    return !hit.hit && hit.object.scene_id == 0 && hit.object.node_id == 0 &&
           hit.object.instance_id == 0 && hit.point == engine::Vec3{} &&
           hit.normal == engine::Vec3{} && hit.distance == 0 && hit.fraction == 0;
}

bool position(engine::Node node, const engine::Vec3& value)
{
    auto transform = node.get_component<engine::Transform>();
    auto settings = transform.settings();
    settings.position = value;
    return transform.set_settings(settings);
}

struct Fixture
{
    engine::WorldSystem world;
    engine::PhysicsSystem physics{&world, physics_tasks};

    bool initialize()
    {
        return world.initialize({}) && physics.initialize({}) &&
               world.create_scene("Physics interactions") != nullptr;
    }

    engine::Node box(const char* name, const engine::Vec3& center,
                     const engine::Vec3& extents = {0.5, 0.5, 0.5},
                     engine::schema::BodyMotion motion = engine::schema::BodyMotion::static_body,
                     bool trigger = false)
    {
        auto node = world.scene()->create_node(name);
        auto collider = node ? node.add_component<engine::Collider>() : engine::Collider{};
        if (!collider || !position(node, center))
        {
            return {};
        }
        auto shape = collider.settings();
        shape.half_extents = extents;
        shape.is_trigger = trigger;
        shape.restitution = 0;
        if (!collider.set_settings(shape))
        {
            return {};
        }
        if (motion != engine::schema::BodyMotion::static_body)
        {
            auto body = node.add_component<engine::RigidBody>();
            if (!body)
            {
                return {};
            }
            auto settings = body.settings();
            settings.motion = motion;
            settings.gravity_factor = 0;
            settings.linear_damping = 0;
            settings.angular_damping = 0;
            if (!body.set_settings(settings))
            {
                return {};
            }
        }
        return node;
    }

    engine::Node character(const char* name, const engine::Vec3& feet)
    {
        auto node = world.scene()->create_node(name);
        auto motor = node ? node.add_component<engine::CharacterMotor>() : engine::CharacterMotor{};
        if (!motor || !position(node, feet))
        {
            return {};
        }
        auto settings = motor.settings();
        settings.gravity_factor = 0;
        settings.external_damping = 0;
        return motor.set_settings(settings) ? node : engine::Node{};
    }

    bool advance(unsigned int steps)
    {
        for (unsigned int index = 0; index < steps; ++index)
        {
            if (!physics.tick(fixed_step))
            {
                return false;
            }
        }
        return true;
    }
};

bool character_queries()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto wall = fixture.box("Occluder", {-2, 0.9, 0}, {0.3, 1, 1});
    auto character = fixture.character("Character", {});
    auto background = fixture.box("Background", {3, 0.9, 0});
    if (!wall || !character || !background || !fixture.physics.tick(0))
    {
        return false;
    }
    engine::PhysicsQueryFilter filter;
    engine::PhysicsQueryHit hit;
    const auto queries = [&](const PhysicsObject& expected)
    {
        return fixture.physics.query_ray({-5, 0.9, 0}, {10, 0, 0}, filter, hit) && hit.hit &&
               same(hit.object, expected) && fixture.physics.is_hit_valid(hit) &&
               fixture.physics.query_sphere({-5, 0.9, 0}, {10, 0, 0}, 0.2F, filter, hit) &&
               hit.hit && same(hit.object, expected) && fixture.physics.is_hit_valid(hit);
    };
    const auto wall_id = object(wall, PhysicsObjectKind::collider);
    const auto character_id = object(character, PhysicsObjectKind::character);
    const auto background_id = object(background, PhysicsObjectKind::collider);
    if (!expect(queries(wall_id), "solid foreground occludes characters in ray and sphere queries"))
    {
        return false;
    }
    if (!wall.get_component<engine::Collider>().set_enabled(false))
    {
        std::abort();
    }
    if (!expect(!fixture.physics.is_object_valid(wall_id) && queries(character_id) &&
                    std::abs(hit.distance - 4.5F) < 0.03F && hit.normal.x() < -0.99 &&
                    std::abs(hit.point.x() + 0.3) < 0.03,
                "disabled foreground exposes the actual motor capsule before synchronization"))
    {
        return false;
    }
    const auto saved = hit;
    filter.include_static = filter.include_dynamic = filter.include_kinematic = false;
    if (!expect(queries(character_id), "characters are independent of every rigid motion filter"))
    {
        return false;
    }
    filter.include_characters = false;
    if (!expect(fixture.physics.query_ray({-5, 0.9, 0}, {10, 0, 0}, filter, hit) && empty(hit) &&
                    fixture.physics.query_sphere({-5, 0.9, 0}, {10, 0, 0}, 0.2F, filter, hit) &&
                    empty(hit),
                "excluding characters and rigid categories gives a valid empty result"))
    {
        return false;
    }
    filter = {};
    filter.include_characters = false;
    if (!expect(queries(background_id), "character exclusion reveals the background collider"))
    {
        return false;
    }
    filter = {};
    filter.ignore_node_id = character.id();
    if (!expect(queries(background_id), "self exclusion filters the selected character"))
    {
        return false;
    }
    filter = {};
    if (!character.get_component<engine::Transform>().set_enabled(false))
    {
        std::abort();
    }
    if (!expect(!fixture.physics.is_hit_valid(saved) && queries(background_id),
                "disabled character transform invalidates its identity immediately"))
    {
        return false;
    }
    if (!character.get_component<engine::Transform>().set_enabled(true))
    {
        std::abort();
    }
    auto motor = character.get_component<engine::CharacterMotor>();
    if (!motor.set_enabled(false))
    {
        std::abort();
    }
    if (!expect(!fixture.physics.is_object_valid(character_id) && queries(background_id),
                "disabled motor does not obscure background objects"))
    {
        return false;
    }
    if (!motor.set_enabled(true))
    {
        std::abort();
    }
    if (!character.remove_component(engine::CharacterMotor::component_type))
    {
        return false;
    }
    motor = character.add_component<engine::CharacterMotor>();
    if (!expect(motor && motor.instance_id() != character_id.instance_id &&
                    !fixture.physics.is_object_valid(character_id) &&
                    !fixture.physics.is_hit_valid(saved) && queries(background_id) &&
                    fixture.physics.tick(0) &&
                    queries(object(character, PhysicsObjectKind::character)),
                "same-node motor replacement rejects old query identities before and after sync"))
    {
        return false;
    }
    const auto replacement = hit.object;
    if (!expect(fixture.world.scene()->destroy_node(character.id()) &&
                    !fixture.physics.is_object_valid(replacement) && queries(background_id),
                "deleted characters cannot retain query visibility"))
    {
        return false;
    }
    return true;
}

std::shared_ptr<const engine::schema::MeshDataT> patch(float x)
{
    auto mesh = std::make_shared<engine::schema::MeshDataT>();
    mesh->vertices = {{x - 0.5F, 0, -0.5F, 0, 1, 0},
                      {x + 0.5F, 0, -0.5F, 0, 1, 0},
                      {x - 0.5F, 0, 0.5F, 0, 1, 0},
                      {x + 0.5F, 0, 0.5F, 0, 1, 0}};
    mesh->indices = {0, 2, 1, 1, 2, 3};
    return mesh;
}

bool overlap_objects_and_filters()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto solid = fixture.box("Static", {-3, 1, 0});
    auto moving =
        fixture.box("Dynamic", {-1, 1, 0}, {0.5, 0.5, 0.5}, engine::schema::BodyMotion::dynamic);
    auto kinematic =
        fixture.box("Kinematic", {1, 1, 0}, {0.5, 0.5, 0.5}, engine::schema::BodyMotion::kinematic);
    auto trigger = fixture.box("Trigger", {3, 1, 0}, {0.5, 0.5, 0.5},
                               engine::schema::BodyMotion::static_body, true);
    auto character = fixture.character("Character", {0, 0, 3});
    auto segmented = fixture.box("Segmented mesh", {0, 0, -3});
    auto parent = fixture.world.scene()->create_node("Ignored parent");
    if (!solid || !moving || !kinematic || !trigger || !character || !segmented || !parent ||
        !solid.set_parent(parent) || !fixture.box("Outside", {20, 1, 0}))
    {
        return false;
    }
    engine::MeshCollisionData parts;
    parts.parts = {patch(-0.75F), patch(0.75F)};
    if (!fixture.physics.set_mesh_parts(segmented.get_component<engine::Collider>(), parts) ||
        !fixture.physics.tick(0))
    {
        return false;
    }
    const std::array identities{object(solid, PhysicsObjectKind::collider),
                                object(moving, PhysicsObjectKind::collider),
                                object(kinematic, PhysicsObjectKind::collider),
                                object(trigger, PhysicsObjectKind::collider),
                                object(character, PhysicsObjectKind::character),
                                object(segmented, PhysicsObjectKind::collider)};
    engine::PhysicsQueryFilter filter;
    std::vector<PhysicsObject> sphere;
    std::vector<PhysicsObject> box;
    const auto query = [&](std::initializer_list<std::size_t> expected)
    {
        if (!fixture.physics.overlap_sphere({0, 1, 0}, 6, filter, sphere) ||
            !fixture.physics.overlap_box({0, 1, 0}, {6, 2, 6}, filter, box) ||
            sphere.size() != expected.size() || box.size() != sphere.size() ||
            !std::equal(sphere.begin(), sphere.end(), box.begin(), same))
        {
            return false;
        }
        for (const auto index : expected)
        {
            if (std::count_if(sphere.begin(), sphere.end(), [&](const auto& value)
                              { return same(value, identities[index]); }) != 1)
            {
                return false;
            }
        }
        const auto previous = sphere;
        return fixture.physics.overlap_sphere({0, 1, 0}, 6, filter, sphere) &&
               sphere.size() == previous.size() &&
               std::equal(sphere.begin(), sphere.end(), previous.begin(), same);
    };
    if (!expect(query({0, 1, 2, 4, 5}),
                "sphere and box return all solid objects once, including a multi-part mesh"))
    {
        return false;
    }
    filter.include_sensors = true;
    if (!expect(query({0, 1, 2, 3, 4, 5}), "sensor opt-in adds every trigger to either overlap"))
    {
        return false;
    }
    filter.include_static = false;
    if (!expect(query({1, 2, 4}), "trigger inclusion still observes the rigid motion category"))
    {
        return false;
    }
    filter.include_kinematic = false;
    filter.include_dynamic = false;
    if (!expect(query({4}), "characters remain after all rigid motion categories are excluded"))
    {
        return false;
    }
    filter.include_characters = false;
    if (!expect(query({}), "excluding all object categories succeeds with an empty range"))
    {
        return false;
    }
    filter = {};
    filter.ignore_node_id = parent.id();
    if (!expect(query({1, 2, 4, 5}), "range self-filter excludes collider descendants"))
    {
        return false;
    }
    filter.ignore_descendants = false;
    if (!expect(query({0, 1, 2, 4, 5}), "node-only range self-filter retains descendants"))
    {
        return false;
    }
    filter = {};
    if (!moving.get_component<engine::Collider>().set_enabled(false))
    {
        std::abort();
    }
    if (!character.get_component<engine::CharacterMotor>().set_enabled(false))
    {
        std::abort();
    }
    if (!expect(query({0, 2, 5}), "range queries filter disabled objects before physics sync"))
    {
        return false;
    }
    return expect(fixture.world.scene()->destroy_node(segmented.id()) && query({0, 2}),
                  "deleting a multi-part collider removes all its query leaves immediately");
}

bool overlap_failures()
{
    Fixture fixture;
    const PhysicsObject stale{99, 99, PhysicsObjectKind::character, 99};
    std::vector<PhysicsObject> output{stale};
    if (!expect(!fixture.physics.overlap_sphere({}, 1, {}, output) && output.empty(),
                "uninitialized range query clears prior caller results") ||
        !fixture.initialize())
    {
        return false;
    }
    output = {stale};
    if (!expect(!fixture.physics.overlap_box({}, {1, 1, 1}, {}, output) && output.empty() &&
                    fixture.physics.tick(0),
                "range queries reject a scene until explicit synchronization"))
    {
        return false;
    }
    for (const float radius :
         std::array{0.0F, -1.0F, 1001.0F, std::numeric_limits<float>::quiet_NaN(),
                    std::numeric_limits<float>::infinity()})
    {
        output = {stale};
        if (!expect(!fixture.physics.overlap_sphere({}, radius, {}, output) && output.empty(),
                    "invalid sphere range never leaks a partial or previous result"))
        {
            return false;
        }
    }
    for (const engine::Vec3 extents :
         std::array{engine::Vec3{0, 1, 1}, engine::Vec3{1, -1, 1}, engine::Vec3{1, 1, 1001},
                    engine::Vec3{std::numeric_limits<double>::quiet_NaN(), 1, 1},
                    engine::Vec3{1, std::numeric_limits<double>::infinity(), 1}})
    {
        output = {stale};
        if (!expect(!fixture.physics.overlap_box({}, extents, {}, output) && output.empty(),
                    "invalid box half extents clear the complete output list"))
        {
            return false;
        }
    }
    engine::PhysicsQueryFilter filter;
    filter.ignore_node_id = std::numeric_limits<std::uint64_t>::max();
    output = {stale};
    if (!expect(!fixture.physics.overlap_sphere({}, 1, filter, output) && output.empty(),
                "unknown ignored node is an error, not an unfiltered range query"))
    {
        return false;
    }
    output = {stale};
    if (!expect(!fixture.physics.overlap_box({}, {1, 1, 1}, filter, output) && output.empty(),
                "box self-filter rejects foreign node identity without old output"))
    {
        return false;
    }
    output = {stale};
    if (!expect(!fixture.physics.overlap_sphere({1.0e8, 0, 0}, 1, {}, output) && output.empty(),
                "out-of-range centers reject before collecting any object"))
    {
        return false;
    }
    fixture.world.destroy_scene();
    output = {stale};
    return expect(!fixture.physics.overlap_box({}, {1, 1, 1}, {}, output) && output.empty(),
                  "destroyed scenes cannot return stale range objects");
}

bool resting_contacts_and_substeps()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto floor = fixture.box("Floor", {0, -0.5, 0}, {6, 0.5, 6});
    auto falling =
        fixture.box("Falling", {0, 0.6, 0}, {0.5, 0.5, 0.5}, engine::schema::BodyMotion::dynamic);
    if (!floor || !falling)
    {
        return false;
    }
    auto body = falling.get_component<engine::RigidBody>();
    auto settings = body.settings();
    settings.gravity_factor = 1;
    if (!body.set_settings(settings))
    {
        return false;
    }
    const auto floor_id = object(floor, PhysicsObjectKind::collider);
    const auto falling_id = object(falling, PhysicsObjectKind::collider);
    PhysicsContactEvent first;
    bool began = false;
    for (unsigned int index = 0; index < 60 && !began; ++index)
    {
        if (!fixture.physics.tick(fixed_step))
        {
            return false;
        }
        const auto current = events(fixture.physics, floor_id, falling_id);
        if (!current.empty())
        {
            if (!expect(current.size() == 1 && current.front().phase == PhysicsContactPhase::begin,
                        "first rigid contact publishes one begin for its object pair"))
            {
                return false;
            }
            first = current.front();
            began = true;
        }
    }
    if (!expect(began && first.frame > 0 && first.step > 0 && !first.is_trigger &&
                    std::abs(first.point.y()) < 0.1 &&
                    (same(first.a, floor_id) ? first.normal.y() > 0.99 : first.normal.y() < -0.99),
                "rigid contact preserves world geometry and an A-to-B normal") ||
        !fixture.physics.tick(3 * fixed_step))
    {
        return false;
    }
    const auto multiple = events(fixture.physics, floor_id, falling_id);
    if (!expect(multiple.size() == 3 && multiple.front().frame > first.frame &&
                    std::all_of(multiple.begin(), multiple.end(),
                                [&](const auto& event)
                                {
                                    return event.phase == PhysicsContactPhase::stay &&
                                           event.frame == multiple.front().frame;
                                }) &&
                    multiple[0].step == first.step + 1 && multiple[1].step == first.step + 2 &&
                    multiple[2].step == first.step + 3 && fixture.advance(300),
                "one host tick retains a stay event from every fixed substep"))
    {
        return false;
    }
    const auto rested = events(fixture.physics, floor_id, falling_id);
    if (!expect(rested.size() == 1 && rested.front().phase == PhysicsContactPhase::stay &&
                    std::abs(body.linear_velocity().y()) < 0.01 &&
                    fixture.physics.tick(8 * fixed_step),
                "resting contact remains active after the normal sleep interval"))
    {
        return false;
    }
    const auto sleeping = events(fixture.physics, floor_id, falling_id);
    if (!expect(sleeping.size() == 8 &&
                    std::all_of(sleeping.begin(), sleeping.end(), [](const auto& event)
                                { return event.phase == PhysicsContactPhase::stay; }) &&
                    fixture.physics.contacts().size() == sleeping.size() &&
                    fixture.physics.tick(0) && fixture.physics.contacts().empty(),
                "sleeping pairs keep each stay while zero-substep ticks publish none"))
    {
        return false;
    }
    if (!position(falling, {10, 0.5, 0}) || !fixture.physics.tick(fixed_step))
    {
        return false;
    }
    const auto ended = events(fixture.physics, floor_id, falling_id);
    return expect(ended.size() == 1 && ended.front().phase == PhysicsContactPhase::end &&
                      ended.front().step > sleeping.back().step &&
                      fixture.physics.is_object_valid(ended.front().a) &&
                      fixture.physics.is_object_valid(ended.front().b) &&
                      fixture.physics.tick(fixed_step) && fixture.physics.contacts().empty(),
                  "separating live objects emits one end, then expires without repeated exits");
}

bool contact_lifecycle()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto sensor =
        fixture.box("Sensor", {}, {2, 2, 2}, engine::schema::BodyMotion::static_body, true);
    auto body = fixture.box("Body", {}, {0.25, 0.25, 0.25}, engine::schema::BodyMotion::dynamic);
    if (!sensor || !body || !fixture.physics.tick(fixed_step))
    {
        return false;
    }
    const auto sensor_id = object(sensor, PhysicsObjectKind::collider);
    auto body_id = object(body, PhysicsObjectKind::collider);
    const auto started = events(fixture.physics, sensor_id, body_id);
    if (!expect(started.size() == 1 && started.front().phase == PhysicsContactPhase::begin &&
                    started.front().is_trigger,
                "sensor contact starts a lifecycle without physical response"))
    {
        return false;
    }
    auto collider = body.get_component<engine::Collider>();
    if (!collider.set_enabled(false))
    {
        std::abort();
    }
    if (!fixture.physics.tick(0))
    {
        return false;
    }
    const auto disabled = events(fixture.physics, sensor_id, body_id);
    if (!expect(
            disabled.size() == 1 && disabled.front().phase == PhysicsContactPhase::end &&
                disabled.front().frame > started.front().frame &&
                disabled.front().step == started.front().step &&
                !fixture.physics.is_object_valid(body_id) &&
                disabled.front().point == started.front().point &&
                disabled.front().normal == started.front().normal,
            "zero-step disable emits end with the invalid historical identity and last geometry"))
    {
        return false;
    }
    if (!collider.set_enabled(true))
    {
        std::abort();
    }
    if (!fixture.physics.tick(fixed_step))
    {
        return false;
    }
    const auto resumed = events(fixture.physics, sensor_id, body_id);
    if (!expect(resumed.size() == 1 && resumed.front().phase == PhysicsContactPhase::begin &&
                    fixture.physics.is_object_valid(body_id),
                "reenabling the same collider begins a new contact interval"))
    {
        return false;
    }
    if (!body.remove_component(engine::Collider::component_type))
    {
        return false;
    }
    collider = body.add_component<engine::Collider>();
    if (!collider || !fixture.physics.tick(0))
    {
        return false;
    }
    const auto replaced = events(fixture.physics, sensor_id, body_id);
    if (!expect(replaced.size() == 1 && replaced.front().phase == PhysicsContactPhase::end &&
                    !fixture.physics.is_object_valid(body_id) &&
                    collider.instance_id() != body_id.instance_id,
                "same-node collider replacement closes the old instance even on a zero-step tick"))
    {
        return false;
    }
    body_id = object(body, PhysicsObjectKind::collider);
    if (!fixture.physics.tick(fixed_step))
    {
        return false;
    }
    const auto replacement = events(fixture.physics, sensor_id, body_id);
    if (!expect(replacement.size() == 1 &&
                    replacement.front().phase == PhysicsContactPhase::begin &&
                    fixture.world.scene()->destroy_node(body.id()) && fixture.physics.tick(0),
                "the replacement starts independently before its node is removed"))
    {
        return false;
    }
    const auto removed = events(fixture.physics, sensor_id, body_id);
    if (!expect(removed.size() == 1 && removed.front().phase == PhysicsContactPhase::end &&
                    !fixture.physics.is_object_valid(body_id) && fixture.physics.tick(0) &&
                    fixture.physics.contacts().empty(),
                "node deletion reports the old contact once instead of silently dropping its end"))
    {
        return false;
    }
    body = fixture.box("Next body", {}, {0.25, 0.25, 0.25}, engine::schema::BodyMotion::dynamic);
    if (!body || !fixture.physics.tick(fixed_step) || fixture.physics.contacts().empty())
    {
        return false;
    }
    fixture.physics.clear();
    if (!expect(fixture.physics.contacts().empty() && fixture.physics.tick(0) &&
                    fixture.physics.contacts().empty() && fixture.physics.tick(fixed_step) &&
                    !fixture.physics.contacts().empty(),
                "clear discards old intervals and explicit synchronization starts fresh contacts"))
    {
        return false;
    }
    fixture.world.destroy_scene();
    return expect(fixture.world.create_scene("Replacement scene") != nullptr &&
                      fixture.physics.contacts().empty() && fixture.physics.tick(0) &&
                      fixture.physics.contacts().empty() && fixture.physics.tick(fixed_step) &&
                      fixture.physics.contacts().empty(),
                  "scene replacement never forwards old begin, stay or end into the new scene");
}

bool contact_type_changes()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto target = fixture.box("Changing contact type", {}, {2, 2, 2});
    auto body =
        fixture.box("Kinematic", {}, {0.25, 0.25, 0.25}, engine::schema::BodyMotion::kinematic);
    if (!target || !body || !fixture.physics.tick(fixed_step))
    {
        return false;
    }
    auto collider = target.get_component<engine::Collider>();
    const auto target_id = object(target, PhysicsObjectKind::collider);
    const auto body_id = object(body, PhysicsObjectKind::collider);
    const auto started = events(fixture.physics, target_id, body_id);
    if (!expect(started.size() == 1 && started.front().phase == PhysicsContactPhase::begin &&
                    !started.front().is_trigger,
                "solid contact begins before its collider changes trigger type"))
    {
        return false;
    }
    auto previous = started.front();
    for (const bool trigger : {true, false})
    {
        auto settings = collider.settings();
        settings.is_trigger = trigger;
        if (!collider.set_settings(settings) || !fixture.physics.tick(fixed_step))
        {
            return false;
        }
        const auto changed = events(fixture.physics, target_id, body_id);
        if (!expect(
                same(object(target, PhysicsObjectKind::collider), target_id) &&
                    same(object(body, PhysicsObjectKind::collider), body_id) &&
                    changed.size() == 2 && fixture.physics.contacts().size() == changed.size() &&
                    changed[0].phase == PhysicsContactPhase::end &&
                    changed[0].is_trigger == previous.is_trigger &&
                    changed[0].point == previous.point && changed[0].normal == previous.normal &&
                    changed[1].phase == PhysicsContactPhase::begin &&
                    changed[1].is_trigger == trigger && changed[0].frame > previous.frame &&
                    changed[0].frame == changed[1].frame && changed[0].step == previous.step + 1 &&
                    changed[0].step == changed[1].step &&
                    fixture.physics.is_object_valid(target_id) &&
                    fixture.physics.is_object_valid(body_id),
                "changing trigger type preserves identities and emits old end before new begin"))
        {
            return false;
        }
        if (!fixture.physics.tick(fixed_step))
        {
            return false;
        }
        const auto continued = events(fixture.physics, target_id, body_id);
        if (!expect(continued.size() == 1 && continued.front().phase == PhysicsContactPhase::stay &&
                        continued.front().is_trigger == trigger &&
                        continued.front().step == changed.back().step + 1,
                    "the new contact type continues without duplicate end or begin events"))
        {
            return false;
        }
        previous = continued.front();
    }
    return true;
}

bool character_trigger_contacts()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto sensor = fixture.box("Character trigger", {0, 0.9, 0}, {0.5, 1, 1},
                              engine::schema::BodyMotion::static_body, true);
    auto character = fixture.character("Walker", {-2, 0, 0});
    if (!sensor || !character ||
        !character.get_component<engine::CharacterMotor>().set_velocity({2, 0, 0}))
    {
        return false;
    }
    const auto sensor_id = object(sensor, PhysicsObjectKind::collider);
    const auto character_id = object(character, PhysicsObjectKind::character);
    std::array<bool, 3> phases{};
    std::uint64_t prior_step = 0;
    for (unsigned int index = 0; index < 180; ++index)
    {
        if (!fixture.physics.tick(fixed_step))
        {
            return false;
        }
        for (const auto& event : events(fixture.physics, sensor_id, character_id))
        {
            if (!expect(event.is_trigger && event.step > prior_step &&
                            fixture.physics.is_object_valid(event.a) &&
                            fixture.physics.is_object_valid(event.b),
                        "character sensor events use live unified identities and increasing steps"))
            {
                return false;
            }
            prior_step = event.step;
            phases[static_cast<std::size_t>(event.phase)] = true;
        }
    }
    if (!expect(phases[static_cast<std::size_t>(PhysicsContactPhase::begin)] &&
                    phases[static_cast<std::size_t>(PhysicsContactPhase::stay)] &&
                    phases[static_cast<std::size_t>(PhysicsContactPhase::end)] &&
                    character.get_component<engine::Transform>().settings().position.x() > 3,
                "walking through a trigger gives begin/stay/end without blocking the motor"))
    {
        return false;
    }
    if (!position(character, {}) ||
        !character.get_component<engine::CharacterMotor>().set_velocity({}) ||
        !fixture.physics.tick(fixed_step))
    {
        return false;
    }
    if (!character.get_component<engine::CharacterMotor>().set_enabled(false))
    {
        std::abort();
    }
    if (!fixture.physics.tick(0))
    {
        return false;
    }
    const auto disabled = events(fixture.physics, sensor_id, character_id);
    return expect(disabled.size() == 1 && disabled.front().phase == PhysicsContactPhase::end &&
                      disabled.front().is_trigger && !fixture.physics.is_object_valid(character_id),
                  "disabling a contacting motor produces an end for its old character identity");
}

bool transient_contact_in_one_tick()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto sensor = fixture.box("Narrow trigger", {0, 0.9, 0}, {0.15, 1, 1},
                              engine::schema::BodyMotion::static_body, true);
    auto character = fixture.character("Fast crossing", {-1.2, 0, 0});
    if (!sensor || !character ||
        !character.get_component<engine::CharacterMotor>().set_velocity({24, 0, 0}) ||
        !fixture.physics.tick(0) || !fixture.physics.contacts().empty() ||
        !fixture.physics.tick(8 * fixed_step))
    {
        return false;
    }
    const auto current = events(fixture.physics, object(sensor, PhysicsObjectKind::collider),
                                object(character, PhysicsObjectKind::character));
    if (!expect(current.size() >= 2 && current.front().phase == PhysicsContactPhase::begin &&
                    current.back().phase == PhysicsContactPhase::end &&
                    current.front().step < current.back().step &&
                    character.get_component<engine::Transform>().settings().position.x() > 1.5,
                "one host tick preserves both entry and exit of a fast character trigger crossing"))
    {
        return false;
    }
    for (std::size_t index = 0; index < current.size(); ++index)
    {
        if (!expect(
                current[index].frame == current.front().frame && current[index].is_trigger &&
                    (index == 0 || current[index].step > current[index - 1].step),
                "multi-substep contacts share a frame and retain strictly ordered step identities"))
        {
            return false;
        }
    }
    return expect(fixture.physics.tick(0) && fixture.physics.contacts().empty(),
                  "reading the next zero-step snapshot cannot replay a transient contact");
}

bool persistent_world_objects()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto sensor = fixture.box("Persistent trigger", {}, {1, 1, 1},
                              engine::schema::BodyMotion::static_body, true);
    auto transient =
        fixture.box("Unloaded contact", {}, {0.5, 0.5, 0.5}, engine::schema::BodyMotion::dynamic);
    auto moving = fixture.box("Persistent body", {5, 0, 0}, {0.5, 0.5, 0.5},
                              engine::schema::BodyMotion::dynamic);
    auto character = fixture.character("Persistent motor", {10, 0, 0});
    if (!sensor || !transient || !moving || !character || !sensor.set_persistent(true) ||
        !moving.set_persistent(true) || !character.set_persistent(true))
    {
        return false;
    }
    const auto sensor_id = object(sensor, PhysicsObjectKind::collider);
    const auto transient_id = object(transient, PhysicsObjectKind::collider);
    const auto moving_id = object(moving, PhysicsObjectKind::collider);
    const auto character_id = object(character, PhysicsObjectKind::character);
    auto body = moving.get_component<engine::RigidBody>();
    auto motion = body.settings();
    motion.linear_velocity = {1, 0, 0};
    if (!body.set_settings(motion) ||
        !character.get_component<engine::CharacterMotor>().set_velocity({1, 0, 0}) ||
        !fixture.physics.tick(fixed_step) ||
        !expect(!events(fixture.physics, sensor_id, transient_id).empty(),
                "persistent receiver first contacts the ordinary scene object"))
    {
        return false;
    }
    const auto x = moving.get_component<engine::Transform>().settings().position.x();
    fixture.world.unload_scene();
    if (!expect(fixture.world.scene() == nullptr && fixture.physics.is_object_valid(moving_id) &&
                    fixture.physics.is_object_valid(character_id) &&
                    !fixture.physics.is_object_valid(transient_id) && fixture.physics.tick(0),
                "unloading a scene retains body and motor identities without an active scene"))
    {
        return false;
    }
    const auto ended = events(fixture.physics, sensor_id, transient_id);
    if (!expect(ended.size() == 1 && ended.front().phase == PhysicsContactPhase::end &&
                    fixture.physics.is_object_valid(sensor_id),
                "persistent receiver gets one exit for its unloaded contact") ||
        !fixture.physics.tick(fixed_step) ||
        !expect(moving.get_component<engine::Transform>().settings().position.x() > x &&
                    body.settings().linear_velocity.x() > 0.99 &&
                    character.get_component<engine::Transform>().settings().position.x() > 10,
                "retained physics advances and preserves velocity without an active scene") ||
        fixture.world.create_scene("New gameplay") == nullptr)
    {
        return false;
    }
    auto next = fixture.box("New object", {15, 0, 0});
    engine::PhysicsQueryHit hit;
    std::vector<PhysicsObject> overlaps;
    if (!next || !fixture.physics.tick(0) ||
        !expect(fixture.physics.query_ray({5, 3, 0}, {0, -6, 0}, {}, hit) && hit.hit &&
                    same(hit.object, moving_id) &&
                    fixture.physics.overlap_sphere({7, 0, 0}, 10, {}, overlaps) &&
                    std::any_of(overlaps.begin(), overlaps.end(),
                                [&](const auto& value) { return same(value, moving_id); }) &&
                    std::any_of(overlaps.begin(), overlaps.end(), [&](const auto& value)
                                { return same(value, object(next, PhysicsObjectKind::collider)); }),
                "queries return actual scene and component identities across published scenes"))
    {
        return false;
    }
    if (!moving.remove_component(engine::Collider::component_type) ||
        moving.add_component<engine::Collider>().valid() == false ||
        !expect(!fixture.physics.is_hit_valid(hit) && !fixture.physics.is_object_valid(moving_id),
                "same-node replacement cannot revive a retained collider identity"))
    {
        return false;
    }
    const auto old_scene = sensor_id.scene_id;
    return expect(fixture.world.destroy_scene(old_scene) &&
                      !fixture.physics.is_object_valid(sensor_id) &&
                      fixture.physics.is_object_valid(object(next, PhysicsObjectKind::collider)) &&
                      fixture.physics.tick(0) && fixture.physics.body_count() == 1 &&
                      fixture.physics.character_count() == 0,
                  "destroying one retained scene removes its physics without clearing new bodies");
}

bool cross_scene_impacts()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto floor = fixture.box("Persistent floor", {0, -0.5, 0}, {4, 0.5, 4});
    if (!floor || !floor.set_persistent(true) || !fixture.physics.tick(0))
    {
        return false;
    }
    const auto floor_id = object(floor, PhysicsObjectKind::collider);
    fixture.world.unload_scene();
    if (!fixture.world.create_scene("Impact world"))
    {
        return false;
    }
    auto falling = fixture.box("New falling body", {0, 0.6, 0}, {0.5, 0.5, 0.5},
                               engine::schema::BodyMotion::dynamic);
    if (!falling)
    {
        return false;
    }
    auto body = falling.get_component<engine::RigidBody>();
    auto motion = body.settings();
    motion.linear_velocity = {0, -6, 0};
    if (!body.set_settings(motion))
    {
        return false;
    }
    for (unsigned index = 0; index < 6; ++index)
    {
        if (!fixture.physics.tick(fixed_step))
        {
            return false;
        }
        for (const auto& impact : fixture.physics.impacts())
        {
            if (same(impact.a, floor_id) &&
                same(impact.b, object(falling, PhysicsObjectKind::collider)))
            {
                return expect(
                    impact.a.scene_id != impact.b.scene_id && impact.normal.y() > 0.99 &&
                        impact.closing_speed > 5.5,
                    "impact retains each participant's scene, component and A-to-B normal");
            }
        }
    }
    return expect(false, "cross-scene bodies produce an impact");
}

bool run_suite()
{
    return character_queries() && overlap_objects_and_filters() && overlap_failures() &&
           resting_contacts_and_substeps() && contact_lifecycle() && contact_type_changes() &&
           character_trigger_contacts() && transient_contact_in_one_tick() &&
           persistent_world_objects() && cross_scene_impacts();
}

} // namespace

int main()
{
    if (!run_suite())
    {
        return 1;
    }
    engine::ThreadSystem threads({.worker_limit = 2});
    engine::TaskSystem tasks(threads, {.name = "Physics interaction tests",
                                       .max_concurrency = 2,
                                       .priority = engine::TaskPriority::high});
    if (!expect(threads.initialize({}) && tasks.initialize({}),
                "initialize shared worker domain for identical contact regressions"))
    {
        return 1;
    }
    physics_tasks = &tasks;
    const bool result = run_suite();
    physics_tasks = nullptr;
    return result && tasks.wait() ? 0 : 1;
}
