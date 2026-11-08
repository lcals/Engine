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

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <semaphore>
#include <thread>
#include <utility>

namespace
{

engine::TaskSystem* physics_tasks = nullptr;

bool expect(bool condition, const char* description)
{
    if (!condition)
    {
        std::fprintf(stderr, "physics test: %s\n", description);
    }
    return condition;
}

struct Fixture
{
    engine::WorldSystem world;
    engine::PhysicsSystem physics{&world, physics_tasks};

    bool initialize()
    {
        const engine::EngineConfig config;
        return world.initialize(config) && physics.initialize(config) &&
               world.create_scene("Physics") != nullptr;
    }
};

engine::Node create(engine::Scene& scene, const char* name, const engine::Vec3& position,
                    engine::schema::ColliderShape shape, bool dynamic)
{
    auto node = scene.create_node(name);
    if (!node.valid())
    {
        return {};
    }
    auto transform = node.get_component<engine::Transform>();
    auto settings = test_support::read_settings(transform);
    settings.position = position;
    auto collider = node.add_component<engine::Collider>();
    if (!collider.valid() || !transform.set_settings(settings))
    {
        return {};
    }
    auto collision = test_support::read_settings(collider);
    collision.shape = shape;
    collision.restitution = 0.0F;
    if (!collider.set_settings(collision) ||
        (dynamic && node.add_component<engine::RigidBody>().valid() == false))
    {
        return {};
    }
    return node;
}

bool advance(engine::PhysicsSystem& physics, unsigned int frames)
{
    for (unsigned int frame = 0; frame < frames; ++frame)
    {
        if (!physics.tick(1.0 / 60.0))
        {
            return false;
        }
    }
    return true;
}

double height(engine::Node node)
{
    return test_support::read_settings(node.get_component<engine::Transform>()).position.y();
}

bool collision_and_lifecycle()
{
    Fixture fixture;
    if (!expect(fixture.initialize(), "initialize real collision world"))
    {
        return false;
    }
    auto& scene = *fixture.world.scene();
    auto floor = create(scene, "Floor", {0, -0.5, 0}, engine::schema::ColliderShape::box, false);
    auto box = create(scene, "Box", {-2, 4, 0}, engine::schema::ColliderShape::box, true);
    auto sphere = create(scene, "Sphere", {0, 4, 0}, engine::schema::ColliderShape::sphere, true);
    auto capsule =
        create(scene, "Capsule", {2, 4, 0}, engine::schema::ColliderShape::capsule, true);
    if (!expect(floor && box && sphere && capsule, "create schema-backed physical nodes"))
    {
        return false;
    }
    auto floor_settings = test_support::read_settings(floor.get_component<engine::Collider>());
    floor_settings.half_extents = {8, 0.5, 8};
    if (!expect(floor.get_component<engine::Collider>().set_settings(floor_settings),
                "configure floor") ||
        !expect(fixture.physics.tick(0.0) && fixture.physics.body_count() == 4,
                "zero delta creates physical bodies without advancing") ||
        !expect(height(box) == 4.0, "no implicit simulation during synchronization") ||
        !expect(advance(fixture.physics, 240), "simulate falling shapes") ||
        !expect(std::abs(height(box) - 0.5) < 0.08, "box rests on actual floor collision") ||
        !expect(std::abs(height(sphere) - 0.5) < 0.08, "sphere rests on floor") ||
        !expect(std::abs(height(capsule) - 1.0) < 0.1, "capsule rests on floor") ||
        !expect(height(floor) == -0.5, "collider without rigid body stays static"))
    {
        return false;
    }
    auto rigid = box.get_component<engine::RigidBody>();
    auto body_settings = test_support::read_settings(rigid);
    body_settings.mass = 2.0F;
    body_settings.linear_velocity = {};
    body_settings.angular_velocity = {};
    engine::schema::ComponentDataT before;
    engine::schema::ComponentDataT after;
    if (!expect(rigid.set_settings(body_settings) && rigid.build_data(before) &&
                    rigid.add_impulse({0, 4, 0}) && rigid.build_data(after) && before == after,
                "pending impulse is transient and mass remains schema data") ||
        !expect(fixture.physics.tick(0.0) && std::abs(rigid.linear_velocity().y() - 2.0) < 0.01,
                "impulse changes real body velocity according to mass") ||
        !expect(fixture.physics.tick(0.0) && std::abs(rigid.linear_velocity().y() - 2.0) < 0.01,
                "impulse is consumed exactly once") ||
        !expect(advance(fixture.physics, 5) && height(box) > 0.6, "impulse lifts the box"))
    {
        return false;
    }
    if (!rigid.set_enabled(false))
    {
        std::abort();
    }
    if (!expect(fixture.physics.tick(0.0) && fixture.physics.body_count() == 3,
                "disabled rigid body is removed"))
    {
        return false;
    }
    if (!rigid.set_enabled(true))
    {
        std::abort();
    }
    if (!sphere.get_component<engine::Collider>().set_enabled(false))
    {
        std::abort();
    }
    if (!expect(fixture.physics.tick(0.0) && fixture.physics.body_count() == 3,
                "enable rigid body and disable collider update the body set") ||
        !expect(scene.destroy_node(capsule.id()) && fixture.physics.tick(0.0) &&
                    fixture.physics.body_count() == 2,
                "deleted node removes its body without retaining Node pointers"))
    {
        return false;
    }
    fixture.physics.clear();
    fixture.physics.clear();
    if (!expect(fixture.physics.body_count() == 0 && fixture.physics.tick(0.0) &&
                    fixture.physics.body_count() == 2,
                "clear is idempotent and scene can be synchronized again"))
    {
        return false;
    }
    fixture.world.destroy_scene();
    if (!expect(fixture.physics.tick(0.0) && fixture.physics.body_count() == 0,
                "destroyed scene removes all bodies"))
    {
        return false;
    }
    auto* next = fixture.world.create_scene("Replacement");
    if (!expect(next &&
                    create(*next, "Next", {0, 1, 0}, engine::schema::ColliderShape::sphere, true) &&
                    fixture.physics.tick(0.0) && fixture.physics.body_count() == 1,
                "new scene identity starts a new physical world"))
    {
        return false;
    }
    fixture.physics.shutdown();
    fixture.physics.shutdown();
    return expect(fixture.physics.body_count() == 0, "shutdown releases physical bodies");
}

bool fixed_step_and_validation()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto node = create(*fixture.world.scene(), "Falling", {0, 4, 0},
                       engine::schema::ColliderShape::box, true);
    if (!node)
    {
        return false;
    }
    auto collider = node.get_component<engine::Collider>();
    auto collision = test_support::read_settings(collider);
    collision.radius = std::numeric_limits<float>::quiet_NaN();
    auto rigid = node.get_component<engine::RigidBody>();
    auto invalid = test_support::read_settings(rigid);
    invalid.mass = 0;
    if (!expect(!engine::Collider::valid_settings(collision) && !collider.set_settings(collision) &&
                    test_support::read_settings(collider).radius == 0.5F,
                "invalid collider settings preserve existing state") ||
        !expect(!engine::RigidBody::valid_settings(invalid) && !rigid.set_settings(invalid) &&
                    test_support::read_settings(rigid).mass == 1.0F,
                "invalid body settings preserve existing state") ||
        !expect(!rigid.add_impulse({std::numeric_limits<double>::infinity(), 0, 0}),
                "nonfinite impulse is rejected") ||
        !expect(fixture.physics.tick(1.0 / 120.0) && height(node) == 4,
                "partial fixed step does not move") ||
        !expect(fixture.physics.tick(1.0 / 120.0) && height(node) < 4,
                "two half steps produce one physical update") ||
        !expect(fixture.physics.tick(1000) && height(node) > 3.5,
                "long frame is bounded to eight physical substeps"))
    {
        return false;
    }
    auto settings = test_support::read_settings(rigid);
    settings.motion = engine::schema::BodyMotion::static_body;
    settings.linear_velocity = {};
    settings.angular_velocity = {};
    if (!expect(rigid.set_settings(settings) && !rigid.add_impulse({0, 1, 0}) &&
                    !rigid.set_linear_velocity({0, 1, 0}),
                "static bodies reject motion requests"))
    {
        return false;
    }
    const double previous = height(node);
    return expect(advance(fixture.physics, 20) && height(node) == previous,
                  "explicit static body remains stationary");
}

bool velocity_updates_at_capacity()
{
    Fixture fixture;
    if (!expect(fixture.initialize(), "initialize velocity update world"))
    {
        return false;
    }
    auto& scene = *fixture.world.scene();
    engine::RigidBody moving = {};
    engine::RigidBody kinematic = {};
    // The configured body pool is full: velocity-only changes must not allocate a replacement.
    constexpr unsigned int capacity = 4096;
    for (unsigned int index = 0; index < capacity; ++index)
    {
        auto node = create(scene, "Velocity capacity", {4.0 * (index % 64), 4, 4.0 * (index / 64)},
                           engine::schema::ColliderShape::box, true);
        if (!node)
        {
            return false;
        }
        auto body = node.get_component<engine::RigidBody>();
        if (index == 0)
        {
            moving = body;
        }
        else if (index == 1)
        {
            kinematic = body;
            auto settings = test_support::read_settings(body);
            settings.motion = engine::schema::BodyMotion::kinematic;
            if (!body.set_settings(settings))
            {
                return false;
            }
        }
    }
    if (!expect(moving && kinematic && fixture.physics.tick(0.0) &&
                    fixture.physics.body_count() == capacity,
                "fill physical body capacity without advancing simulation"))
    {
        return false;
    }
    auto settings = test_support::read_settings(moving);
    settings.linear_velocity = {2, 0, 0};
    settings.angular_velocity = {0, 1, 0};
    auto kinematic_settings = test_support::read_settings(kinematic);
    kinematic_settings.linear_velocity = {0, 0, 3};
    kinematic_settings.angular_velocity = {0, 0, 1};
    if (!expect(moving.set_settings(settings) && moving.add_impulse({1, 0, 0}) &&
                    kinematic.set_settings(kinematic_settings) && fixture.physics.tick(0.0) &&
                    fixture.physics.body_count() == capacity,
                "velocity-only sync succeeds at capacity and retains queued impulse") ||
        !expect(std::abs(moving.linear_velocity().x() - 3.0) < 0.001 &&
                    std::abs(test_support::read_settings(moving).angular_velocity.y() - 1.0) <
                        0.001 &&
                    test_support::read_settings(kinematic) == kinematic_settings,
                "dynamic and kinematic linear/angular velocities reach actual physical bodies") ||
        !expect(fixture.physics.tick(0.0) && std::abs(moving.linear_velocity().x() - 3.0) < 0.001,
                "repeated synchronization does not consume the impulse twice"))
    {
        return false;
    }
    settings = test_support::read_settings(moving);
    settings.linear_velocity = {};
    settings.angular_velocity = {};
    return expect(moving.set_settings(settings) && fixture.physics.tick(0.0) &&
                      test_support::read_settings(moving) == settings &&
                      fixture.physics.body_count() == capacity,
                  "setting velocity to zero updates in place at full capacity");
}

bool hierarchy_and_kinematic()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto& scene = *fixture.world.scene();
    auto parent = scene.create_node("Parent");
    auto platform = create(scene, "Platform", {0, 1, 0}, engine::schema::ColliderShape::box, false);
    auto ball = create(scene, "Ball", {5, 5, 0}, engine::schema::ColliderShape::sphere, true);
    if (!parent || !platform || !ball)
    {
        return false;
    }
    auto parent_settings = test_support::read_settings(parent.get_component<engine::Transform>());
    parent_settings.position = {5, 0, 0};
    parent_settings.rotation = {0, 90, 0};
    parent_settings.scale = {2, 2, 2};
    if (!parent.get_component<engine::Transform>().set_settings(parent_settings) ||
        !platform.set_parent(parent) || !advance(fixture.physics, 240) ||
        !expect(std::abs(height(ball) - 3.5) < 0.1,
                "static collision uses complete parent translation rotation and scale"))
    {
        return false;
    }
    if (!ball.get_component<engine::Collider>().set_enabled(false))
    {
        std::abort();
    }
    auto mover = create(scene, "Kinematic", {0, 3, 0}, engine::schema::ColliderShape::box, true);
    if (!mover || !mover.set_parent(parent))
    {
        return false;
    }
    auto motion = test_support::read_settings(mover.get_component<engine::RigidBody>());
    motion.motion = engine::schema::BodyMotion::kinematic;
    motion.linear_velocity = {1, 0, 0};
    motion.linear_damping = 0;
    std::array<float, 16> start{};
    std::array<float, 16> end{};
    if (!mover.get_component<engine::RigidBody>().set_settings(motion) ||
        !mover.get_component<engine::Transform>().world_matrix(start.data()) ||
        !advance(fixture.physics, 60) ||
        !mover.get_component<engine::Transform>().world_matrix(end.data()) ||
        !expect(std::abs((end[12] - start[12]) - 1.0F) < 0.05F &&
                    std::abs(end[13] - start[13]) < 0.01F,
                "kinematic world velocity writes correct local pose through parent"))
    {
        return false;
    }
    motion.motion = engine::schema::BodyMotion::dynamic;
    if (!mover.get_component<engine::RigidBody>().set_settings(motion) ||
        !expect(!fixture.physics.tick(0.0) && fixture.physics.body_count() == 0,
                "unsupported dynamic parent fails explicitly and clears bodies"))
    {
        return false;
    }
    if (!mover.get_component<engine::RigidBody>().set_enabled(false))
    {
        std::abort();
    }
    parent_settings.scale = {2, 1, 1};
    auto platform_settings =
        test_support::read_settings(platform.get_component<engine::Transform>());
    platform_settings.rotation = {0, 45, 0};
    return parent.get_component<engine::Transform>().set_settings(parent_settings) &&
           platform.get_component<engine::Transform>().set_settings(platform_settings) &&
           expect(!fixture.physics.tick(0.0), "sheared world transform fails explicitly");
}

bool trigger_and_registration()
{
    Fixture first;
    Fixture second;
    if (!expect(first.initialize() && second.initialize(),
                "independent worlds share own Jolt registration"))
    {
        return false;
    }
    first.physics.shutdown();
    auto& scene = *second.world.scene();
    auto trigger = create(scene, "Trigger", {0, 0, 0}, engine::schema::ColliderShape::box, false);
    auto ball = create(scene, "Ball", {0, 2, 0}, engine::schema::ColliderShape::sphere, true);
    if (!trigger || !ball)
    {
        return false;
    }
    auto settings = test_support::read_settings(trigger.get_component<engine::Collider>());
    settings.is_trigger = true;
    if (!trigger.get_component<engine::Collider>().set_settings(settings))
    {
        return false;
    }
    for (unsigned frame = 0; frame < 90; ++frame)
    {
        if (!expect(second.physics.tick(1.0 / 60.0) && second.physics.impacts().empty(),
                    "sensor contacts never publish rigid body impacts"))
        {
            return false;
        }
    }
    return expect(height(ball) < -1.0,
                  "trigger has no physical response and another system shutdown preserves Jolt");
}

bool targeted_ray()
{
    Fixture fixture;
    Fixture other;
    if (!fixture.initialize() || !other.initialize())
    {
        return false;
    }
    auto& scene = *fixture.world.scene();
    auto target = create(scene, "Target", {}, engine::schema::ColliderShape::box, false);
    auto blocker = create(scene, "Blocker", {0, 2, 0}, engine::schema::ColliderShape::box, false);
    auto foreign =
        create(*other.world.scene(), "Foreign", {}, engine::schema::ColliderShape::box, false);
    if (!target || !blocker || !foreign)
    {
        return false;
    }
    auto collider = target.get_component<engine::Collider>();
    float fraction = 0;
    if (!expect(fixture.physics.cast_ray(collider, {0, 4, 0}, {0, -8, 0}, fraction) &&
                    fraction == 1 && fixture.physics.tick(0),
                "ray against a valid unpublished collider reports no hit without creating it") ||
        !expect(fixture.physics.cast_ray(collider, {0, 4, 0}, {0, -8, 0}, fraction) &&
                    std::abs(fraction - 0.4375F) < 0.001F,
                "target ray ignores the closer unrelated collider") ||
        !expect(fixture.physics.cast_ray(collider, {3, 4, 0}, {0, -8, 0}, fraction) &&
                    fraction == 1,
                "target ray miss succeeds with fraction one"))
    {
        return false;
    }
    if (!collider.set_enabled(false))
    {
        std::abort();
    }
    if (!expect(fixture.physics.cast_ray(collider, {0, 4, 0}, {0, -8, 0}, fraction) &&
                    fraction == 1,
                "disabled target is ignored before the next physics synchronization"))
    {
        return false;
    }
    if (!collider.set_enabled(true))
    {
        std::abort();
    }
    if (!target.get_component<engine::Transform>().set_enabled(false))
    {
        std::abort();
    }
    if (!expect(fixture.physics.cast_ray(collider, {0, 4, 0}, {0, -8, 0}, fraction) &&
                    fraction == 1,
                "disabled target transform is ignored before synchronization"))
    {
        return false;
    }
    if (!target.get_component<engine::Transform>().set_enabled(true))
    {
        std::abort();
    }
    auto sensor = test_support::read_settings(collider);
    sensor.is_trigger = true;
    if (!expect(collider.set_settings(sensor) &&
                    fixture.physics.cast_ray(collider, {0, 4, 0}, {0, -8, 0}, fraction) &&
                    fraction == 1,
                "target sensor never supplies solid support") ||
        !expect(!fixture.physics.cast_ray(foreign.get_component<engine::Collider>(), {0, 4, 0},
                                          {0, -8, 0}, fraction) &&
                    fraction == 0,
                "foreign collider is an error and resets fraction") ||
        !expect(!fixture.physics.cast_ray(collider, {0, 4, 0},
                                          {0, std::numeric_limits<double>::quiet_NaN(), 0},
                                          fraction) &&
                    fraction == 0,
                "invalid ray input is an error even for a disabled physical target"))
    {
        return false;
    }
    return true;
}

bool empty_hit(const engine::PhysicsQueryHit& hit)
{
    return !hit.hit && hit.object.scene_id == 0 && hit.object.node_id == 0 &&
           hit.object.instance_id == 0 && hit.point == engine::Vec3{} &&
           hit.normal == engine::Vec3{} && hit.distance == 0 && hit.fraction == 0;
}

bool closest_queries()
{
    Fixture fixture;
    engine::PhysicsQueryHit hit;
    hit.hit = true;
    if (!expect(!fixture.physics.query_ray({0, 4, 0}, {0, -8, 0}, {}, hit) && empty_hit(hit),
                "uninitialized nearest query fails with a cleared result") ||
        !fixture.initialize() ||
        !expect(!fixture.physics.query_ray({0, 4, 0}, {0, -8, 0}, {}, hit) && empty_hit(hit),
                "nearest query never synchronizes a newly created scene"))
    {
        return false;
    }
    auto& scene = *fixture.world.scene();
    auto far = create(scene, "Far", {}, engine::schema::ColliderShape::box, false);
    auto near = create(scene, "Near", {0, 2, 0}, engine::schema::ColliderShape::box, false);
    if (!far || !near || !fixture.physics.tick(0))
    {
        return false;
    }
    if (!expect(
            fixture.physics.query_ray({0, 4, 0}, {0, -8, 0}, {}, hit) && hit.hit &&
                hit.object.scene_id == scene.id() && hit.object.node_id == near.id() &&
                hit.object.instance_id == near.get_component<engine::Collider>().instance_id() &&
                std::abs(hit.fraction - 0.1875F) < 0.001F &&
                std::abs(hit.distance - 1.5F) < 0.001F && std::abs(hit.point.y() - 2.5) < 0.001 &&
                hit.normal.y() > 0.99 && fixture.physics.is_hit_valid(hit),
            "nearest ray returns collider identity, surface point, normal and distance") ||
        !expect(fixture.physics.query_sphere({0, 4, 0}, {0, -8, 0}, 0.25F, {}, hit) && hit.hit &&
                    hit.object.node_id == near.id() && std::abs(hit.fraction - 0.15625F) < 0.001F &&
                    std::abs(hit.distance - 1.25F) < 0.01F &&
                    std::abs(hit.point.y() - 2.5) < 0.01 && hit.normal.y() > 0.99,
                "sphere result distinguishes target surface point from sphere center travel") ||
        !expect(fixture.physics.query_ray({0, 4, 0}, {0, -1.5, 0}, {}, hit) && hit.hit &&
                    std::abs(hit.fraction - 1.0F) < 0.001F,
                "nearest ray includes its end point") ||
        !expect(fixture.physics.query_ray({0, 2, 0}, {0, -8, 0}, {}, hit) && hit.hit &&
                    hit.object.node_id == near.id() && hit.fraction == 0 && hit.distance == 0 &&
                    hit.point == engine::Vec3(0, 2, 0) && hit.normal == engine::Vec3(0, 1, 0),
                "ray starting inside a convex collider returns a stable retreat direction") ||
        !expect(fixture.physics.query_sphere({0, 2.4, 0}, {0, -8, 0}, 0.25F, {}, hit) && hit.hit &&
                    hit.object.node_id == near.id() && hit.fraction == 0 && hit.distance == 0 &&
                    hit.normal.y() > 0.99 && std::abs(hit.point.y() - 2.5) < 0.01,
                "initial sphere overlap returns the target surface and outward contact normal") ||
        !expect(fixture.physics.query_ray({3, 4, 0}, {0, -8, 0}, {}, hit) && empty_hit(hit) &&
                    !fixture.physics.is_hit_valid(hit) &&
                    fixture.physics.query_sphere({3, 4, 0}, {0, -8, 0}, 0.25F, {}, hit) &&
                    empty_hit(hit),
                "valid ray and sphere misses succeed with empty values"))
    {
        return false;
    }
    hit.hit = true;
    hit.object.node_id = near.id();
    if (!expect(!fixture.physics.query_ray({}, {}, {}, hit) && empty_hit(hit) &&
                    !fixture.physics.query_sphere({}, {}, 0.25F, {}, hit) && empty_hit(hit) &&
                    !fixture.physics.query_sphere({}, {0, -8, 0}, 0, {}, hit) && empty_hit(hit) &&
                    !fixture.physics.query_ray({}, {0, std::numeric_limits<double>::quiet_NaN(), 0},
                                               {}, hit) &&
                    empty_hit(hit),
                "invalid nearest query inputs clear previous hit data"))
    {
        return false;
    }
    float fraction = 0;
    return expect(fixture.physics.cast_sphere({3, 4, 0}, {}, 0.25F, fraction) && fraction == 1,
                  "legacy sphere query retains its zero-displacement contract");
}

bool query_filters()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto& scene = *fixture.world.scene();
    auto parent = scene.create_node("Ignored hierarchy");
    auto sensor = create(scene, "Sensor", {0, 5, 0}, engine::schema::ColliderShape::box, false);
    auto solid = create(scene, "Static", {0, 3, 0}, engine::schema::ColliderShape::box, false);
    auto kinematic =
        create(scene, "Kinematic", {0, 1, 0}, engine::schema::ColliderShape::box, true);
    auto dynamic = create(scene, "Dynamic", {0, -1, 0}, engine::schema::ColliderShape::box, true);
    if (!parent || !sensor || !solid || !kinematic || !dynamic || !solid.set_parent(parent))
    {
        return false;
    }
    auto settings = test_support::read_settings(sensor.get_component<engine::Collider>());
    settings.is_trigger = true;
    auto motion = test_support::read_settings(kinematic.get_component<engine::RigidBody>());
    motion.motion = engine::schema::BodyMotion::kinematic;
    if (!sensor.get_component<engine::Collider>().set_settings(settings) ||
        !kinematic.get_component<engine::RigidBody>().set_settings(motion) ||
        !fixture.physics.tick(0))
    {
        return false;
    }
    engine::PhysicsQueryFilter filter;
    engine::PhysicsQueryHit hit;
    const auto query = [&](std::uint64_t expected)
    {
        return fixture.physics.query_ray({0, 7, 0}, {0, -10, 0}, filter, hit) &&
               hit.object.node_id == expected && hit.hit == (expected != 0) &&
               fixture.physics.query_sphere({0, 7, 0}, {0, -10, 0}, 0.2F, filter, hit) &&
               hit.object.node_id == expected && hit.hit == (expected != 0);
    };
    if (!expect(query(solid.id()), "default filters include all solid motion categories"))
    {
        return false;
    }
    filter.include_sensors = true;
    if (!expect(query(sensor.id()), "sensor inclusion applies to ray and sphere queries"))
    {
        return false;
    }
    filter.include_static = false;
    if (!expect(query(kinematic.id()), "sensor still obeys its motion category filter"))
    {
        return false;
    }
    filter.include_kinematic = false;
    if (!expect(query(dynamic.id()),
                "dynamic-only query ignores closer static and kinematic bodies"))
    {
        return false;
    }
    filter.include_dynamic = false;
    if (!expect(query(0) && empty_hit(hit),
                "excluding every motion category produces a valid miss"))
    {
        return false;
    }
    filter = {};
    filter.ignore_node_id = parent.id();
    if (!expect(query(kinematic.id()), "self filter can ignore a complete collider subtree"))
    {
        return false;
    }
    filter.ignore_descendants = false;
    if (!expect(query(solid.id()), "node-only self filter preserves descendant colliders"))
    {
        return false;
    }
    filter.ignore_node_id = solid.id();
    if (!expect(query(kinematic.id()), "self filter ignores the selected collider node"))
    {
        return false;
    }
    filter.ignore_node_id = std::numeric_limits<std::uint64_t>::max();
    return expect(!fixture.physics.query_ray({0, 7, 0}, {0, -10, 0}, filter, hit) &&
                      empty_hit(hit) &&
                      !fixture.physics.query_sphere({0, 7, 0}, {0, -10, 0}, 0.2F, filter, hit) &&
                      empty_hit(hit),
                  "an ignored node outside the current scene is an error with empty output");
}

bool query_lifecycle()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto& scene = *fixture.world.scene();
    auto far = create(scene, "Far", {}, engine::schema::ColliderShape::box, false);
    auto near = create(scene, "Near", {0, 2, 0}, engine::schema::ColliderShape::box, true);
    auto parent = scene.create_node("Parent");
    if (!far || !near || !parent || !fixture.physics.tick(0))
    {
        return false;
    }
    engine::PhysicsQueryHit hit;
    const auto query = [&](std::uint64_t expected)
    {
        return fixture.physics.query_ray({0, 4, 0}, {0, -8, 0}, {}, hit) && hit.hit &&
               hit.object.node_id == expected &&
               fixture.physics.query_sphere({0, 4, 0}, {0, -8, 0}, 0.25F, {}, hit) && hit.hit &&
               hit.object.node_id == expected;
    };
    if (!query(near.id()))
    {
        return false;
    }
    const auto saved = hit;
    auto collider = near.get_component<engine::Collider>();
    if (!collider.set_enabled(false))
    {
        std::abort();
    }
    if (!expect(!fixture.physics.is_hit_valid(saved) && query(far.id()),
                "disabled collider cannot obscure a live object before synchronization"))
    {
        return false;
    }
    if (!collider.set_enabled(true))
    {
        std::abort();
    }
    auto rigid = near.get_component<engine::RigidBody>();
    if (!rigid.set_enabled(false))
    {
        std::abort();
    }
    if (!expect(!fixture.physics.is_hit_valid(saved) && query(far.id()),
                "disabled rigid body invalidates hits before synchronization"))
    {
        return false;
    }
    if (!rigid.set_enabled(true))
    {
        std::abort();
    }
    if (!near.set_parent(parent))
    {
        return false;
    }
    if (!parent.get_component<engine::Transform>().set_enabled(false))
    {
        std::abort();
    }
    if (!expect(!fixture.physics.is_hit_valid(saved) && query(far.id()),
                "disabled ancestor transform filters its descendant's old body"))
    {
        return false;
    }
    if (!parent.get_component<engine::Transform>().set_enabled(true))
    {
        std::abort();
    }
    if (!near.set_parent({}))
    {
        return false;
    }
    auto transform = near.get_component<engine::Transform>();
    auto pose = test_support::read_settings(transform);
    pose.position = {3, pose.position.y(), pose.position.z()};
    if (!expect(transform.set_settings(pose) && fixture.physics.is_hit_valid(saved) &&
                    fixture.physics.tick(0) && query(far.id()) &&
                    fixture.physics.is_hit_valid(saved),
                "moving a collider changes new queries but preserves historical object identity"))
    {
        return false;
    }
    pose.position = {0, pose.position.y(), pose.position.z()};
    if (!transform.set_settings(pose) || !fixture.physics.tick(0) ||
        !near.remove_component(engine::Collider::component_type))
    {
        return false;
    }
    collider = near.add_component<engine::Collider>();
    if (!expect(collider.valid() && collider.instance_id() != saved.object.instance_id &&
                    !fixture.physics.is_hit_valid(saved) && query(far.id()) &&
                    fixture.physics.tick(0) && query(near.id()) &&
                    hit.object.instance_id == collider.instance_id() &&
                    !fixture.physics.is_hit_valid(saved),
                "same-node collider replacement cannot resurrect an old body or hit identity"))
    {
        return false;
    }
    const auto replacement = hit;
    if (!expect(scene.destroy_node(near.id()) && !fixture.physics.is_hit_valid(replacement) &&
                    query(far.id()),
                "a deleted foreground node never blocks a still-live background collider"))
    {
        return false;
    }
    const auto previous_scene = hit;
    fixture.world.destroy_scene();
    if (!expect(!fixture.physics.is_hit_valid(previous_scene) &&
                    !fixture.physics.query_ray({0, 4, 0}, {0, -8, 0}, {}, hit) && empty_hit(hit) &&
                    fixture.world.create_scene("Next") != nullptr &&
                    !fixture.physics.query_sphere({0, 4, 0}, {0, -8, 0}, 0.25F, {}, hit) &&
                    empty_hit(hit) && fixture.physics.tick(0) &&
                    fixture.physics.query_ray({0, 4, 0}, {0, -8, 0}, {}, hit) && empty_hit(hit),
                "scene replacement rejects stale physics until the new scene is synchronized"))
    {
        return false;
    }
    fixture.physics.clear();
    return expect(!fixture.physics.query_ray({0, 4, 0}, {0, -8, 0}, {}, hit) && empty_hit(hit),
                  "clear requires explicit physics synchronization before querying again");
}

bool impact_snapshots()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto& scene = *fixture.world.scene();
    auto floor = create(scene, "Floor", {0, -0.5, 0}, engine::schema::ColliderShape::box, false);
    auto sphere = create(scene, "Sphere", {0, 0.6, 0}, engine::schema::ColliderShape::sphere, true);
    if (!floor || !sphere)
    {
        return false;
    }
    const auto strike = [&](engine::PhysicsImpact& output)
    {
        auto transform = sphere.get_component<engine::Transform>();
        auto pose = test_support::read_settings(transform);
        pose.position = {0, 0.6, 0};
        pose.rotation = {};
        auto rigid = sphere.get_component<engine::RigidBody>();
        auto motion = test_support::read_settings(rigid);
        motion.mass = 2;
        motion.gravity_factor = 0;
        motion.linear_damping = 0;
        motion.angular_damping = 0;
        motion.linear_velocity = {0, -6, 0};
        motion.angular_velocity = {};
        if (!transform.set_settings(pose) || !rigid.set_settings(motion))
        {
            return false;
        }
        for (unsigned frame = 0; frame < 6; ++frame)
        {
            if (!fixture.physics.tick(3.0 / 60.0))
            {
                return false;
            }
            const auto impacts = fixture.physics.impacts();
            if (!impacts.empty())
            {
                if (impacts.size() != 1)
                {
                    return false;
                }
                output = impacts.front();
                return true;
            }
        }
        return false;
    };
    engine::PhysicsImpact first;
    if (!expect(strike(first) && first.frame > 0 && first.a.scene_id == scene.id() &&
                    first.b.scene_id == scene.id() && first.a.node_id == floor.id() &&
                    first.b.node_id == sphere.id() && first.normal.y() > 0.99 &&
                    std::abs(first.point.y()) < 0.1 && first.closing_speed > 5.5F &&
                    first.closing_speed < 6.5F && first.estimated_impulse > 10 &&
                    first.estimated_impulse < 14,
                "official Jolt contact estimate retains pre-solve speed, mass and world normal") ||
        !expect(fixture.physics.impacts().size() == 1 &&
                    fixture.physics.impacts().front().frame == first.frame,
                "multi-substep collision publishes one non-consuming snapshot per node pair") ||
        !expect(fixture.physics.tick(0) && fixture.physics.impacts().empty(),
                "zero-substep tick expires the previous impact snapshot") ||
        !expect(advance(fixture.physics, 4) && fixture.physics.impacts().empty(),
                "stationary contact without approach velocity produces no impact"))
    {
        return false;
    }
    engine::PhysicsImpact next;
    if (!expect(strike(next) && next.frame > first.frame,
                "subsequent impact frames increase monotonically"))
    {
        return false;
    }
    if (!sphere.get_component<engine::Collider>().set_enabled(false))
    {
        std::abort();
    }
    if (!expect(fixture.physics.impacts().empty(), "disabled collider invalidates stale impacts"))
    {
        return false;
    }
    if (!sphere.get_component<engine::Collider>().set_enabled(true))
    {
        std::abort();
    }
    if (!strike(next))
    {
        return false;
    }
    if (!sphere.get_component<engine::RigidBody>().set_enabled(false))
    {
        std::abort();
    }
    if (!expect(fixture.physics.impacts().empty(), "disabled rigid body invalidates stale impacts"))
    {
        return false;
    }
    if (!sphere.get_component<engine::RigidBody>().set_enabled(true))
    {
        std::abort();
    }
    if (!strike(next))
    {
        return false;
    }
    if (!sphere.get_component<engine::Transform>().set_enabled(false))
    {
        std::abort();
    }
    if (!expect(fixture.physics.impacts().empty(), "disabled transform invalidates stale impacts"))
    {
        return false;
    }
    if (!sphere.get_component<engine::Transform>().set_enabled(true))
    {
        std::abort();
    }
    if (!strike(next))
    {
        return false;
    }
    const auto old_object = next.b;
    if (!sphere.remove_component(engine::Collider::component_type))
    {
        return false;
    }
    auto replacement_collider = sphere.add_component<engine::Collider>();
    if (!expect(replacement_collider.valid() &&
                    replacement_collider.instance_id() != old_object.instance_id &&
                    fixture.physics.impacts().empty() &&
                    !fixture.physics.is_object_valid(old_object),
                "replacing a collider on the same node invalidates its previous impact"))
    {
        return false;
    }
    auto replacement_settings = test_support::read_settings(replacement_collider);
    replacement_settings.shape = engine::schema::ColliderShape::sphere;
    if (!replacement_collider.set_settings(replacement_settings) || !strike(next))
    {
        return false;
    }
    const auto prior_frame = next.frame;
    fixture.physics.clear();
    if (!expect(fixture.physics.impacts().empty() && strike(next) && next.frame > prior_frame,
                "clear removes snapshots without recycling the physics frame identity") ||
        !expect(scene.destroy_node(sphere.id()) && fixture.physics.impacts().empty(),
                "deleting a node invalidates stale impacts without retaining its pointer"))
    {
        return false;
    }
    sphere = create(scene, "Replacement", {0, 0.6, 0}, engine::schema::ColliderShape::sphere, true);
    if (!sphere || !strike(next))
    {
        return false;
    }
    fixture.world.destroy_scene();
    return expect(fixture.world.create_scene("Replacement scene") != nullptr &&
                      fixture.physics.impacts().empty() && fixture.physics.tick(0) &&
                      fixture.physics.impacts().empty(),
                  "scene replacement expires all old world collision events");
}

bool rotation_and_scale()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto node = create(*fixture.world.scene(), "Rotating", {0, 4, 0},
                       engine::schema::ColliderShape::box, true);
    if (!node)
    {
        return false;
    }
    auto transform = node.get_component<engine::Transform>();
    auto rigid = node.get_component<engine::RigidBody>();
    auto motion = test_support::read_settings(rigid);
    motion.gravity_factor = 0;
    const std::array rotations{engine::Vec3(30, 90, 0),        engine::Vec3(30, 90, 20),
                               engine::Vec3(-7, -90, 0),       engine::Vec3(45, 89.999, 20),
                               engine::Vec3(-35, -89.999, 70), engine::Vec3(30, 45, 20)};
    for (const auto mode :
         {engine::schema::BodyMotion::dynamic, engine::schema::BodyMotion::kinematic})
    {
        motion.motion = mode;
        if (!rigid.set_settings(motion))
        {
            return false;
        }
        for (const auto& rotation : rotations)
        {
            fixture.physics.clear();
            auto pose = test_support::read_settings(transform);
            pose.rotation = rotation;
            pose.scale = {1, 2, 0.5};
            std::array<float, 16> before{};
            std::array<float, 16> after{};
            if (!transform.set_settings(pose) || !transform.world_matrix(before.data()) ||
                !advance(fixture.physics, 20) || !transform.world_matrix(after.data()))
            {
                return false;
            }
            for (std::size_t index = 0; index < before.size(); ++index)
            {
                if (!expect(std::abs(before[index] - after[index]) < 0.00005F,
                            "pose writeback preserves rotation at and near Euler singularities"))
                {
                    return false;
                }
            }
        }
    }
    auto pose = test_support::read_settings(transform);
    pose.rotation = {};
    pose.scale = {-1, -1, 1};
    if (!expect(transform.set_settings(pose) && !fixture.physics.tick(0.0) &&
                    fixture.physics.body_count() == 0,
                "negative local scales are rejected even when world determinant is positive"))
    {
        return false;
    }
    auto collider = node.get_component<engine::Collider>();
    auto collision = test_support::read_settings(collider);
    collision.shape = engine::schema::ColliderShape::sphere;
    collision.radius = 1000;
    pose.scale = {0.0001, 0.00019, 0.0001};
    if (!expect(collider.set_settings(collision) && transform.set_settings(pose) &&
                    !fixture.physics.tick(0.0),
                "small nonuniform sphere scale cannot pass an absolute tolerance"))
    {
        return false;
    }
    collision.shape = engine::schema::ColliderShape::capsule;
    collision.half_height = 1000;
    pose.scale = {0.0001, 0.0001, 0.00019};
    if (!expect(collider.set_settings(collision) && transform.set_settings(pose) &&
                    !fixture.physics.tick(0.0),
                "small unequal capsule transverse scales are rejected"))
    {
        return false;
    }
    pose.scale = {0.0001, 0.0001, 0.0001};
    return expect(transform.set_settings(pose) && fixture.physics.tick(0.0) &&
                      fixture.physics.body_count() == 1,
                  "small uniform scales still synchronize valid shapes after a rejected update");
}

engine::schema::MeshDataT collision_grid(bool hole)
{
    engine::schema::MeshDataT mesh;
    for (unsigned z = 0; z <= 4; ++z)
    {
        for (unsigned x = 0; x <= 4; ++x)
        {
            mesh.vertices.emplace_back(static_cast<float>(x) - 2.0F, 0.0F,
                                       static_cast<float>(z) - 2.0F, 0, 1, 0);
        }
    }
    for (unsigned z = 0; z < 4; ++z)
    {
        for (unsigned x = 0; x < 4; ++x)
        {
            if (hole && x >= 1 && x <= 2 && z >= 1 && z <= 2)
            {
                continue;
            }
            const auto a = z * 5 + x;
            mesh.indices.insert(mesh.indices.end(), {a, a + 5, a + 1, a + 1, a + 5, a + 6});
        }
    }
    return mesh;
}

bool transient_mesh_collision()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto floor =
        create(*fixture.world.scene(), "Generated", {}, engine::schema::ColliderShape::box, false);
    if (!floor.valid())
    {
        return false;
    }
    auto collider = floor.get_component<engine::Collider>();
    auto mesh = collision_grid(false);
    auto cast = [&](double x, bool should_hit)
    {
        float fraction = 0;
        return fixture.physics.cast_sphere({x, 3, 0.25}, {0, -6, 0}, 0.1F, fraction) &&
               (should_hit ? fraction > 0.4F && fraction < 0.6F : fraction == 1.0F);
    };
    if (!expect(fixture.physics.set_mesh_data(collider, mesh) &&
                    fixture.physics.body_count() == 1 && cast(0.25, true),
                "transient mesh commits immediately without resource system or extra tick"))
    {
        return false;
    }
    float ray_fraction = 0;
    engine::PhysicsQueryHit hit;
    if (!expect(fixture.physics.cast_ray(collider, {0.25, 3, 0.25}, {0, -6, 0}, ray_fraction) &&
                    std::abs(ray_fraction - 0.5F) < 0.001F &&
                    fixture.physics.cast_ray(collider, {0.25, -3, 0.25}, {0, 6, 0}, ray_fraction) &&
                    std::abs(ray_fraction - 0.5F) < 0.001F,
                "target mesh ray uses actual triangles from either side"))
    {
        return false;
    }
    if (!expect(fixture.physics.query_ray({0.25, 3, 0.25}, {0, -6, 0}, {}, hit) && hit.hit &&
                    hit.object.node_id == floor.id() && std::abs(hit.distance - 3.0F) < 0.001F &&
                    std::abs(hit.point.y()) < 0.001 && hit.normal.y() > 0.99 &&
                    fixture.physics.query_ray({0.25, -3, 0.25}, {0, 6, 0}, {}, hit) && hit.hit &&
                    std::abs(hit.distance - 3.0F) < 0.001F && hit.normal.y() > 0.99 &&
                    fixture.physics.query_sphere({0.25, 3, 0.25}, {0, -6, 0}, 0.1F, {}, hit) &&
                    hit.hit && std::abs(hit.distance - 2.9F) < 0.01F &&
                    std::abs(hit.point.y()) < 0.01 && hit.normal.y() > 0.99,
                "nearest mesh queries retain triangle surface normals and double-sided rays"))
    {
        return false;
    }
    const auto* previous = collider.mesh_data();
    mesh.vertices.clear();
    if (!expect(previous != nullptr && previous->vertices.size() == 25 &&
                    collider.mesh_path().empty() &&
                    test_support::read_settings(collider).mesh_guid.empty(),
                "transient collision owns an immutable copy and has no fabricated resource GUID") ||
        !expect(!fixture.physics.set_mesh_data(collider, mesh) &&
                    collider.mesh_data() == previous && cast(0.25, true),
                "invalid replacement preserves component and live collision"))
    {
        return false;
    }
    auto invalid = collision_grid(false);
    invalid.indices[0] = 1000;
    if (!expect(!fixture.physics.set_mesh_data(collider, invalid) && cast(0.25, true),
                "out of range triangle preserves old shape"))
    {
        return false;
    }
    invalid = collision_grid(false);
    invalid.indices.assign({0, 0, 0});
    if (!expect(!fixture.physics.set_mesh_data(collider, invalid) && cast(0.25, true),
                "Jolt rejected degenerate mesh leaves original shape intact"))
    {
        return false;
    }
    engine::schema::ComponentDataT serialized;
    serialized.payload.Set(engine::schema::ColliderDataT{});
    const auto original_data = test_support::read_data(collider);
    if (!expect(!collider.build_data(serialized) && serialized.payload.value == nullptr &&
                    !collider.restore_data(original_data) && collider.mesh_data() == previous,
                "transient geometry cannot silently serialize or restore without a GUID") ||
        !expect(!fixture.physics.set_mesh_data(collider, invalid) && fixture.physics.tick(0.0),
                "failed mesh preparation does not poison subsequent physics ticks"))
    {
        return false;
    }
    auto rigid = floor.add_component<engine::RigidBody>();
    if (!rigid.valid())
    {
        return false;
    }
    auto moving = test_support::read_settings(rigid);
    if (!expect(!rigid.set_settings(moving), "mesh cannot switch to dynamic motion"))
    {
        return false;
    }
    moving.motion = engine::schema::BodyMotion::static_body;
    if (!rigid.set_settings(moving) ||
        !expect(
            fixture.physics.set_mesh_data(collider, collision_grid(true)) && cast(0.25, false) &&
                cast(1.5, true) && fixture.physics.tick(0.0) && cast(0.25, false) &&
                fixture.physics.cast_ray(collider, {0.25, 3, 0.25}, {0, -6, 0}, ray_fraction) &&
                ray_fraction == 1 &&
                fixture.physics.query_ray({0.25, 3, 0.25}, {0, -6, 0}, {}, hit) && empty_hit(hit) &&
                fixture.physics.query_sphere({0.25, 3, 0.25}, {0, -6, 0}, 0.1F, {}, hit) &&
                empty_hit(hit),
            "hole replacement keeps surrounding collision and survives synchronization"))
    {
        return false;
    }
    if (!collider.set_enabled(false))
    {
        std::abort();
    }
    if (!expect(fixture.physics.tick(0.0) && fixture.physics.body_count() == 0 &&
                    fixture.physics.set_mesh_data(collider, collision_grid(false)) &&
                    fixture.physics.body_count() == 0 && cast(0.25, false),
                "disabled collider accepts geometry without creating a body"))
    {
        return false;
    }
    if (!collider.set_enabled(true))
    {
        std::abort();
    }
    if (!expect(fixture.physics.tick(0.0) && cast(0.25, true),
                "reenabling collider restores its prepared geometry"))
    {
        return false;
    }
    auto box = create(*fixture.world.scene(), "Supported", {1.5, 3, 0},
                      engine::schema::ColliderShape::box, true);
    if (!box || !advance(fixture.physics, 240) ||
        !expect(std::abs(height(box) - 0.5) < 0.08, "dynamic body rests on generated mesh") ||
        !expect(fixture.physics.set_mesh_data(collider, {}) && fixture.physics.body_count() == 1 &&
                    advance(fixture.physics, 60) && height(box) < -1.0 && cast(0.25, false) &&
                    fixture.physics.cast_ray(collider, {0.25, 3, 0.25}, {0, -6, 0}, ray_fraction) &&
                    ray_fraction == 1,
                "empty mesh removes support, wakes resting bodies and never creates a box"))
    {
        return false;
    }
    if (!expect(fixture.physics.set_mesh_data(collider, collision_grid(false)) && cast(0.25, true),
                "nonempty replacement restores an emptied collider") ||
        !expect(floor.remove_component(engine::Collider::component_type) &&
                    fixture.physics.tick(0.0) && fixture.physics.body_count() == 1 &&
                    cast(0.25, false),
                "removed collider releases its live collision"))
    {
        return false;
    }
    return true;
}

std::shared_ptr<const engine::schema::MeshDataT> collision_part(float x, float y = 0)
{
    auto mesh = collision_grid(false);
    for (auto& vertex : mesh.vertices)
    {
        vertex.mutate_x(vertex.x() + x);
        vertex.mutate_y(y);
    }
    return std::make_shared<const engine::schema::MeshDataT>(std::move(mesh));
}

bool segmented_mesh_collision()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto& scene = *fixture.world.scene();
    auto floor = create(scene, "Segmented", {}, engine::schema::ColliderShape::box, false);
    if (!floor.valid())
    {
        return false;
    }
    auto collider = floor.get_component<engine::Collider>();
    auto cast = [&](double x, bool hit, float y = 0)
    {
        float fraction = 0;
        return fixture.physics.cast_ray(collider, {x, 4, 0.25}, {0, -8, 0}, fraction) &&
               (hit ? std::abs(fraction - (4 - y) / 8) < 0.001F : fraction == 1);
    };
    engine::MeshCollisionData parts;
    parts.parts = {collision_part(-4), collision_part(0), collision_part(4)};
    if (!expect(
            fixture.physics.set_mesh_parts(collider, parts) && fixture.physics.body_count() == 1 &&
                test_support::scene_nodes(scene).size() == 1 && collider.mesh_data() == nullptr &&
                cast(-4, true) && cast(0, true) && cast(4, true),
            "mesh parts publish immediately as one collider and one physical body"))
    {
        return false;
    }
    auto updated = parts;
    parts.parts.clear();
    if (!expect(fixture.physics.tick(0) && cast(-4, true) && cast(4, true),
                "part publication owns its list after the caller releases it"))
    {
        return false;
    }
    updated.parts[0] = collision_part(-4, 1);
    if (!expect(fixture.physics.set_mesh_parts(collider, updated) && cast(-4, true, 1) &&
                    cast(0, true) && cast(4, true) && fixture.physics.body_count() == 1,
                "replacing one immutable part preserves the other parts"))
    {
        return false;
    }
    updated.parts[1].reset();
    if (!expect(fixture.physics.set_mesh_parts(collider, updated) && cast(0, false) &&
                    cast(-4, true, 1) && cast(4, true),
                "an empty middle slot removes only its collision"))
    {
        return false;
    }
    updated.parts[2] = collision_part(4, 2);
    if (!expect(fixture.physics.set_mesh_parts(collider, updated) && cast(4, true, 2) &&
                    cast(-4, true, 1) && cast(0, false),
                "part indices remain stable after removing a middle slot"))
    {
        return false;
    }
    const auto settings = test_support::read_settings(collider);
    auto invalid = updated;
    invalid.parts[0] = collision_part(-4, -2);
    auto invalid_mesh = collision_grid(false);
    invalid_mesh.indices[0] = static_cast<std::uint32_t>(invalid_mesh.vertices.size());
    invalid.parts[2] = std::make_shared<const engine::schema::MeshDataT>(std::move(invalid_mesh));
    if (!expect(!fixture.physics.set_mesh_parts(collider, invalid) &&
                    test_support::read_settings(collider) == settings && cast(-4, true, 1) &&
                    cast(4, true, 2) && cast(0, false) && fixture.physics.tick(0) &&
                    cast(-4, true, 1) && fixture.physics.body_count() == 1,
                "failure after a valid changed part preserves the whole live batch and next tick"))
    {
        return false;
    }
    engine::schema::ComponentDataT serialized;
    serialized.payload.Set(engine::schema::ColliderDataT{});
    const auto transient = test_support::read_data(collider);
    if (!expect(!collider.build_data(serialized) && serialized.payload.value == nullptr &&
                    !collider.restore_data(transient) && cast(-4, true, 1) && cast(4, true, 2),
                "failed serialization and restore preserve transient segmented collision"))
    {
        return false;
    }
    engine::MeshCollisionData empty;
    empty.parts.resize(3);
    if (!expect(fixture.physics.set_mesh_parts(collider, empty) &&
                    fixture.physics.body_count() == 0 && collider.enabled() && cast(-4, false) &&
                    cast(0, false) && cast(4, false) && fixture.physics.tick(0) &&
                    fixture.physics.body_count() == 0 &&
                    fixture.physics.set_mesh_parts(collider, updated) && cast(-4, true, 1) &&
                    cast(4, true, 2) && fixture.physics.body_count() == 1,
                "all-empty slots remove the body and a nonempty batch restores it"))
    {
        return false;
    }
    fixture.physics.clear();
    if (!expect(fixture.physics.tick(0) && cast(-4, true, 1) && cast(4, true, 2) &&
                    fixture.physics.body_count() == 1,
                "clearing the physics world reconstructs the component's retained parts"))
    {
        return false;
    }
    if (!collider.set_enabled(false))
    {
        std::abort();
    }
    updated.parts[0] = collision_part(-4, -1);
    if (!expect(fixture.physics.tick(0) && fixture.physics.set_mesh_parts(collider, updated) &&
                    fixture.physics.body_count() == 0 && cast(-4, false),
                "disabled segmented collider retains updates without creating a body"))
    {
        return false;
    }
    if (!collider.set_enabled(true))
    {
        std::abort();
    }
    if (!expect(fixture.physics.tick(0) && cast(-4, true, -1) && cast(4, true, 2) &&
                    fixture.physics.body_count() == 1,
                "reenabling reconstructs the latest immutable part snapshot"))
    {
        return false;
    }
    engine::schema::ColliderDataT box;
    if (!expect(fixture.physics.set_collider_data(collider, box) && cast(0, true, 0.5F) &&
                    cast(-4, false) && cast(4, false) && fixture.physics.tick(0) &&
                    fixture.physics.set_mesh_parts(collider, updated) && cast(0, false) &&
                    cast(-4, true, -1) && cast(4, true, 2),
                "segmented geometry can switch to a normal box and back without stale parts") ||
        !expect(fixture.physics.set_mesh_data(collider, collision_grid(false)) &&
                    collider.mesh_data() != nullptr && cast(0, true) && cast(-4, false) &&
                    cast(4, false) && !fixture.physics.set_mesh_parts(collider, invalid) &&
                    collider.mesh_data() != nullptr && cast(0, true) && fixture.physics.tick(0) &&
                    fixture.physics.set_mesh_parts(collider, updated) &&
                    collider.mesh_data() == nullptr && cast(0, false) && cast(-4, true, -1) &&
                    cast(4, true, 2),
                "single-mesh and segmented sources switch atomically without stale geometry"))
    {
        return false;
    }
    auto degenerate = collision_grid(false);
    degenerate.indices.assign({0, 0, 0});
    auto cleaned = updated;
    cleaned.parts[1] = std::make_shared<const engine::schema::MeshDataT>(std::move(degenerate));
    if (!expect(fixture.physics.set_mesh_parts(collider, cleaned) && cast(0, false) &&
                    cast(-4, true, -1) && cast(4, true, 2) && fixture.physics.tick(0),
                "Jolt may sanitize a whole leaf without discarding valid neighboring geometry"))
    {
        return false;
    }
    auto grouped = updated;
    grouped.parts.resize(130);
    grouped.parts[64] = collision_part(8);
    grouped.parts[129] = collision_part(12);
    if (!expect(fixture.physics.set_mesh_parts(collider, grouped) && cast(8, true) &&
                    cast(12, true) && cast(-4, true, -1),
                "nested mesh groups keep all leaves in collider-local coordinates"))
    {
        return false;
    }
    grouped.parts.resize(65);
    grouped.parts[64] = collision_part(8, 1);
    if (!expect(fixture.physics.set_mesh_parts(collider, grouped) && cast(8, true, 1) &&
                    cast(12, false) && cast(-4, true, -1),
                "shrinking a partial group drops removed leaves and rebuilds changed groups"))
    {
        return false;
    }
    grouped.parts.resize(64);
    if (!expect(fixture.physics.set_mesh_parts(collider, grouped) && cast(8, false) &&
                    cast(-4, true, -1) && fixture.physics.tick(0),
                "removing a final group preserves the first group's geometry"))
    {
        return false;
    }
    return expect(fixture.physics.set_mesh_parts(collider, {}) &&
                      fixture.physics.body_count() == 0 && fixture.physics.tick(0) &&
                      fixture.physics.body_count() == 0 &&
                      fixture.physics.set_mesh_parts(collider, updated) && cast(4, true, 2),
                  "an empty part list removes collision and later restores the same collider");
}

bool segmented_mesh_seams()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto& scene = *fixture.world.scene();
    auto floor = create(scene, "Segment seams", {}, engine::schema::ColliderShape::box, false);
    auto slider =
        create(scene, "Sliding box", {-4.5, 0.52, 0.25}, engine::schema::ColliderShape::box, true);
    if (!floor.valid() || !slider.valid())
    {
        return false;
    }
    auto collider = floor.get_component<engine::Collider>();
    auto ground = test_support::read_settings(collider);
    ground.friction = 0;
    auto sliding_collider = slider.get_component<engine::Collider>();
    auto collision = test_support::read_settings(sliding_collider);
    collision.friction = 0;
    auto rigid = slider.get_component<engine::RigidBody>();
    auto motion = test_support::read_settings(rigid);
    motion.linear_velocity = {3, 0, 0};
    motion.linear_damping = 0;
    motion.angular_damping = 0;
    engine::MeshCollisionData parts;
    parts.parts = {collision_part(-4), collision_part(0), collision_part(4)};
    if (!expect(collider.set_settings(ground) && sliding_collider.set_settings(collision) &&
                    rigid.set_settings(motion) && fixture.physics.set_mesh_parts(collider, parts),
                "prepare a frictionless body moving over two independent mesh seams"))
    {
        return false;
    }
    for (unsigned frame = 0; frame < 180; ++frame)
    {
        if (frame == 75)
        {
            parts.parts[1] = collision_part(0);
            if (!expect(fixture.physics.set_mesh_parts(collider, parts),
                        "refresh a part while a body is traversing its surface"))
            {
                return false;
            }
        }
        if (!expect(fixture.physics.tick(1.0 / 60.0) && height(slider) > 0.42 &&
                        height(slider) < 0.62,
                    "crossing a mesh-part seam keeps the body supported without a vertical hop"))
        {
            return false;
        }
    }
    auto position = test_support::read_settings(slider.get_component<engine::Transform>()).position;
    if (!expect(position.x() > 4.2 && position.x() < 4.8 && std::abs(position.z() - 0.25) < 0.08 &&
                    rigid.linear_velocity().x() > 2.8 && fixture.physics.body_count() == 2,
                "the sliding body crosses both seams without snagging or sideways deflection") ||
        !expect(rigid.set_linear_velocity({}) && advance(fixture.physics, 120) &&
                    std::abs(height(slider) - 0.5) < 0.08,
                "the sliding body comes to rest on the final part"))
    {
        return false;
    }
    parts.parts[2].reset();
    float remaining_fraction = 0;
    return expect(
        fixture.physics.set_mesh_parts(collider, parts) && advance(fixture.physics, 60) &&
            height(slider) < -1 && fixture.physics.body_count() == 2 &&
            fixture.physics.cast_ray(collider, {-4, 4, 0.25}, {0, -8, 0}, remaining_fraction) &&
            std::abs(remaining_fraction - 0.5F) < 0.001F,
        "removing one supporting part wakes the resting body and keeps distant support");
}

bool segmented_mesh_transforms()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto& scene = *fixture.world.scene();
    auto parent = scene.create_node("Segment parent");
    auto floor =
        create(scene, "Transformed segments", {}, engine::schema::ColliderShape::box, false);
    if (!parent.valid() || !floor.valid())
    {
        return false;
    }
    auto collider = floor.get_component<engine::Collider>();
    engine::MeshCollisionData parts;
    parts.parts = {collision_part(-4), collision_part(4)};
    if (!fixture.physics.set_mesh_parts(collider, parts))
    {
        return false;
    }
    auto transform = floor.get_component<engine::Transform>();
    auto pose = test_support::read_settings(transform);
    pose.position = {2, 1, 0};
    pose.rotation = {0, 90, 0};
    pose.scale = {2, 1.5, 0.75};
    auto parent_transform = parent.get_component<engine::Transform>();
    auto parent_pose = test_support::read_settings(parent_transform);
    parent_pose.position = {10, 2, -7};
    parent_pose.rotation = {0, 45, 0};
    parent_pose.scale = {1.5, 1.5, 1.5};
    auto collision = test_support::read_settings(collider);
    collision.center = {1, 0.5, -0.75};
    std::array<float, 16> matrix{};
    if (!expect(parent_transform.set_settings(parent_pose) && floor.set_parent(parent) &&
                    transform.set_settings(pose) && collider.set_settings(collision) &&
                    fixture.physics.set_mesh_parts(collider, parts) &&
                    transform.world_matrix(matrix.data()),
                "unchanged part pointers rebuild under a changed center, scale and parent pose"))
    {
        return false;
    }
    const auto point = [&](double x, double y)
    {
        x += collision.center.x();
        y += collision.center.y();
        const double z = 0.25 + collision.center.z();
        return engine::Vec3(matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12],
                            matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13],
                            matrix[2] * x + matrix[6] * y + matrix[10] * z + matrix[14]);
    };
    const auto cast = [&](double x, double y, bool hit)
    {
        const auto surface = point(x, y);
        float fraction = 0;
        return fixture.physics.cast_ray(collider, {surface.x(), surface.y() + 4, surface.z()},
                                        {0, -8, 0}, fraction) &&
               (hit ? std::abs(fraction - 0.5F) < 0.001F : fraction == 1);
    };
    // Sample triangle interiors: a rotated ray exactly on a shared edge is numerically ambiguous.
    constexpr double left_x = -3.875;
    constexpr double right_x = 4.125;
    const auto surface = point(left_x, 0);
    auto supported = create(scene, "Offset support", {surface.x(), surface.y() + 0.52, surface.z()},
                            engine::schema::ColliderShape::box, true);
    if (!expect(supported && cast(left_x, 0, true) && cast(right_x, 0, true) &&
                    advance(fixture.physics, 120) &&
                    std::abs(height(supported) - surface.y() - 0.5) < 0.08,
                "transformed segments support a resting body at their world-space surface"))
    {
        return false;
    }
    parts.parts[0] = collision_part(-4, -1);
    const auto lowered = point(left_x, -1);
    if (!expect(fixture.physics.set_mesh_parts(collider, parts) && cast(left_x, -1, true) &&
                    cast(right_x, 0, true) && advance(fixture.physics, 180) &&
                    std::abs(height(supported) - lowered.y() - 0.5) < 0.08,
                "lowering one transformed part wakes its old support region and preserves others"))
    {
        return false;
    }
    parts.parts[0].reset();
    return expect(fixture.physics.set_mesh_parts(collider, parts) && cast(left_x, -1, false) &&
                      cast(right_x, 0, true) && advance(fixture.physics, 60) &&
                      height(supported) < lowered.y() - 1 && fixture.physics.body_count() == 2,
                  "removing transformed support wakes the sleeping body at the correct COM bounds");
}

bool segmented_mesh_character_seams()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto& scene = *fixture.world.scene();
    auto floor =
        create(scene, "Character segment seams", {}, engine::schema::ColliderShape::box, false);
    auto player = scene.create_node("Segment walker");
    if (!floor.valid() || !player.valid())
    {
        return false;
    }
    auto motor = player.add_component<engine::CharacterMotor>();
    auto transform = player.get_component<engine::Transform>();
    auto pose = test_support::read_settings(transform);
    pose.position = {-4.5, 0.02, 0.25};
    auto collider = floor.get_component<engine::Collider>();
    engine::MeshCollisionData parts;
    parts.parts = {collision_part(-4), collision_part(0), collision_part(4)};
    if (!expect(motor && transform.set_settings(pose) &&
                    fixture.physics.set_mesh_parts(collider, parts) &&
                    advance(fixture.physics, 20) && motor.grounded() &&
                    motor.set_velocity({3, 0, 0}),
                "prepare a grounded CharacterVirtual on the first mesh segment"))
    {
        return false;
    }
    for (unsigned frame = 0; frame < 180; ++frame)
    {
        if (frame == 75)
        {
            parts.parts[1] = collision_part(0);
            if (!fixture.physics.set_mesh_parts(collider, parts))
            {
                return false;
            }
        }
        if (!expect(fixture.physics.tick(1.0 / 60.0) && motor.grounded() &&
                        height(player) > -0.06 && height(player) < 0.08,
                    "CharacterVirtual crosses mesh seams without losing ground or hopping"))
        {
            return false;
        }
    }
    const auto& position = test_support::read_settings(transform).position;
    return expect(
        position.x() > 4.2 && position.x() < 4.8 && std::abs(position.z() - 0.25) < 0.08 &&
            motor.velocity().x() > 2.8 && fixture.physics.character_count() == 1 &&
            fixture.physics.body_count() == 1,
        "the character crosses both mesh seams at the requested speed without deflection");
}

bool accumulated_shape_scale()
{
    // Cover ordinary shapes, a single mesh, and reusable mesh parts with the same world height.
    for (unsigned mode = 0; mode < 3; ++mode)
    {
        Fixture fixture;
        if (!fixture.initialize())
        {
            return false;
        }
        auto node = create(*fixture.world.scene(), "Gradually scaled collision", {},
                           engine::schema::ColliderShape::box, false);
        if (!node.valid())
        {
            return false;
        }
        auto collider = node.get_component<engine::Collider>();
        auto transform = node.get_component<engine::Transform>();
        auto pose = test_support::read_settings(transform);
        pose.scale = {1, 2, 1};
        auto settings = test_support::read_settings(collider);
        settings.center = {0, 2, 0};
        settings.half_extents = {6, 10, 2};
        engine::MeshCollisionData parts;
        parts.parts = {collision_part(-4, 10), collision_part(4, 10)};
        if (!transform.set_settings(pose) || !collider.set_settings(settings))
        {
            return false;
        }
        const bool published = mode == 0 ? fixture.physics.set_collider_data(collider, settings)
                               : mode == 1
                                   ? fixture.physics.set_mesh_data(collider, *collision_part(0, 10))
                                   : fixture.physics.set_mesh_parts(collider, parts);
        if (!published)
        {
            return false;
        }
        const auto surface_matches = [&]()
        {
            const double top = 12 * pose.scale.y();
            float fraction = 0;
            return fixture.physics.cast_ray(collider, {mode == 2 ? -3.875 : 0.125, top + 2, 0.25},
                                            {0, -4, 0}, fraction) &&
                   std::abs(fraction - 0.5F) < 0.0005F;
        };
        if (!expect(surface_matches(), "initial nonunit shape scale reaches the collider"))
        {
            return false;
        }
        for (unsigned step = 1; step <= 200; ++step)
        {
            // Each delta is below the cache tolerance; its cumulative change is not.
            pose.scale = {1, 2 + step * 0.00005, 1};
            if (mode == 2 && step == 1)
            {
                parts.parts[1] = collision_part(4, 10);
            }
            if (!transform.set_settings(pose) ||
                (mode == 2 && step % 2 != 0 && !fixture.physics.set_mesh_parts(collider, parts)) ||
                !fixture.physics.tick(0))
            {
                return false;
            }
            if (step % 50 == 0 &&
                !expect(surface_matches(),
                        "small scale steps accumulate against the baked shape, not the last frame"))
            {
                return false;
            }
        }
    }
    return true;
}

bool numeric_boundaries()
{
    engine::schema::ColliderDataT collider;
    engine::schema::RigidBodyDataT rigid;
    collider.radius = 0.0001F;
    rigid.mass = 0.0001F;
    if (!expect(engine::Collider::valid_settings(collider) &&
                    engine::RigidBody::valid_settings(rigid),
                "float fields accept their documented representable minimum"))
    {
        return false;
    }
    collider.radius = std::nextafter(collider.radius, 0.0F);
    rigid.mass = std::nextafter(rigid.mass, 0.0F);
    if (!expect(!engine::Collider::valid_settings(collider) &&
                    !engine::RigidBody::valid_settings(rigid),
                "one float step below the minimum is rejected"))
    {
        return false;
    }
    collider.radius = 0.5F;
    collider.half_extents = {0.0001, 0.0001, 0.0001};
    if (!expect(engine::Collider::valid_settings(collider),
                "double vector fields retain their own minimum precision"))
    {
        return false;
    }
    collider.half_extents.mutate_x(std::nextafter(0.0001, 0.0));
    return expect(!engine::Collider::valid_settings(collider),
                  "one double step below a vector minimum is rejected");
}

std::unique_ptr<engine::schema::ColliderHullDataT> cube_hull(double x)
{
    auto hull = std::make_unique<engine::schema::ColliderHullDataT>();
    for (const double dz : {-0.5, 0.5})
    {
        for (const double dy : {-0.5, 0.5})
        {
            for (const double dx : {-0.5, 0.5})
            {
                hull->vertices.emplace_back(x + dx, dy, dz);
            }
        }
    }
    return hull;
}

bool compound_box_geometry()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto node = create(*fixture.world.scene(), "Box hull geometry", {3, 4, -2},
                       engine::schema::ColliderShape::box, false);
    if (!node.valid())
    {
        return false;
    }
    auto transform = node.get_component<engine::Transform>();
    auto pose = test_support::read_settings(transform);
    pose.scale = {2, 2, 2};
    auto collider = node.get_component<engine::Collider>();
    auto compound = test_support::read_settings(collider);
    compound.shape = engine::schema::ColliderShape::compound;
    compound.center = {1, 0.5, -0.25};
    compound.hulls.push_back(cube_hull(2));
    float fraction = 0;
    if (!expect(transform.set_settings(pose) &&
                    fixture.physics.set_collider_data(collider, compound) &&
                    fixture.physics.cast_ray(collider, {9, 9, -2.5}, {0, -8, 0}, fraction) &&
                    std::abs(fraction - 0.375F) < 0.00001F,
                "single box hull retains its offset, collider center and uniform scale"))
    {
        return false;
    }
    compound.hulls.push_back(cube_hull(-2));
    if (!expect(fixture.physics.set_collider_data(collider, compound) &&
                    fixture.physics.body_count() == 1 &&
                    fixture.physics.cast_ray(collider, {1, 9, -2.5}, {0, -8, 0}, fraction) &&
                    std::abs(fraction - 0.375F) < 0.00001F &&
                    fixture.physics.cast_ray(collider, {9, 9, -2.5}, {0, -8, 0}, fraction) &&
                    std::abs(fraction - 0.375F) < 0.00001F &&
                    fixture.physics.cast_ray(collider, {5, 9, -2.5}, {0, -8, 0}, fraction) &&
                    fraction == 1,
                "multiple box hulls preserve both transformed surfaces and the gap"))
    {
        return false;
    }
    pose.position = {};
    pose.scale = {1, 1, 1};
    compound.center = {};
    compound.hulls.clear();
    compound.hulls.push_back(cube_hull(0));
    // 缺少 (+,+,+) 角后，真实凸包在此射线处的斜切面为 y = -0.3。
    compound.hulls.front()->vertices.pop_back();
    if (!expect(transform.set_settings(pose) &&
                    fixture.physics.set_collider_data(collider, compound) &&
                    fixture.physics.cast_ray(collider, {0.4, 2, 0.4}, {0, -4, 0}, fraction) &&
                    std::abs(fraction - 0.575F) < 0.00001F,
                "missing box corner retains the diagonal hull face instead of an AABB"))
    {
        return false;
    }
    compound.hulls.front() = cube_hull(0);
    for (auto& vertex : compound.hulls.front()->vertices)
    {
        if (vertex.x() > 0 && vertex.y() > 0)
        {
            vertex.mutate_y(0.499);
        }
    }
    // 顶面轻微倾斜，中心高度为 0.4995；其 AABB 顶面仍是 0.5。
    return expect(fixture.physics.set_collider_data(collider, compound) &&
                      fixture.physics.cast_ray(collider, {0, 2, 0}, {0, -4, 0}, fraction) &&
                      std::abs(fraction - 0.375125F) < 0.00001F,
                  "near-box sloped top retains its actual hull surface");
}

bool dynamic_pose_feedback()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto node = create(*fixture.world.scene(), "Spinning compound", {0, 4, 0},
                       engine::schema::ColliderShape::box, true);
    if (!node.valid())
    {
        return false;
    }
    auto transform = node.get_component<engine::Transform>();
    auto collider = node.get_component<engine::Collider>();
    auto rigid = node.get_component<engine::RigidBody>();
    auto pose = test_support::read_settings(transform);
    pose.rotation = {30, 89.999, 20};
    auto collision = test_support::read_settings(collider);
    collision.shape = engine::schema::ColliderShape::compound;
    collision.center = {1, 0.5, -0.25};
    collision.hulls.push_back(cube_hull(1));
    collision.hulls.push_back(cube_hull(3));
    auto motion = test_support::read_settings(rigid);
    motion.gravity_factor = 0;
    motion.linear_damping = 0;
    motion.angular_damping = 0;
    motion.angular_velocity = {40, 100, 30};
    if (!expect(transform.set_settings(pose) && collider.set_settings(collision) &&
                    rigid.set_settings(motion),
                "configure fast rotating compound with a nonzero center of mass"))
    {
        return false;
    }
    auto world_point = [&](double x, engine::Vec3& output)
    {
        std::array<float, 16> matrix{};
        if (!transform.world_matrix(matrix.data()))
        {
            return false;
        }
        x += collision.center.x();
        const double y = collision.center.y();
        const double z = collision.center.z();
        output = {matrix[12] + matrix[0] * x + matrix[4] * y + matrix[8] * z,
                  matrix[13] + matrix[1] * x + matrix[5] * y + matrix[9] * z,
                  matrix[14] + matrix[2] * x + matrix[6] * y + matrix[10] * z};
        return true;
    };
    engine::Vec3 initial_center;
    if (!world_point(2, initial_center))
    {
        return false;
    }
    for (unsigned frame = 0; frame < 180; ++frame)
    {
        engine::Vec3 center;
        engine::Vec3 hull_center;
        if (!fixture.physics.tick(1.0 / 60.0) || !world_point(2, center) ||
            !world_point(1, hull_center))
        {
            return false;
        }
        const auto& angular = test_support::read_settings(rigid).angular_velocity;
        const auto saved_pose = test_support::read_settings(transform);
        const engine::Vec3 origin(hull_center.x(), hull_center.y() + 3, hull_center.z());
        float before = 0;
        float after = 0;
        if (!expect(std::hypot(center.x() - initial_center.x(), center.y() - initial_center.y(),
                               center.z() - initial_center.z()) < 0.00005 &&
                        std::abs(angular.x() - 40) < 0.0001 &&
                        std::abs(angular.y() - 100) < 0.0001 && std::abs(angular.z() - 30) < 0.0001,
                    "free spinning compound preserves its center of mass and angular velocity") ||
            !expect(fixture.physics.cast_ray(collider, origin, {0, -6, 0}, before) && before > 0 &&
                        before < 1 && fixture.physics.tick(0) &&
                        fixture.physics.cast_ray(collider, origin, {0, -6, 0}, after) &&
                        before == after && test_support::read_settings(transform) == saved_pose,
                    "zero-step synchronization leaves the current rotating collision unchanged"))
        {
            return false;
        }
    }
    motion = test_support::read_settings(rigid);
    motion.angular_velocity = {};
    pose = test_support::read_settings(transform);
    pose.position = {10, 4, -2};
    pose.rotation = {0, 90, 0};
    pose.scale = {2, 2, 2};
    float fraction = 0;
    if (!expect(rigid.set_settings(motion) && transform.set_settings(pose) &&
                    fixture.physics.tick(0) &&
                    fixture.physics.cast_ray(collider, {9.5, 9, -6}, {0, -8, 0}, fraction) &&
                    std::abs(fraction - 0.375F) < 0.00001F &&
                    fixture.physics.cast_ray(collider, {9.5, 9, -8}, {0, -8, 0}, fraction) &&
                    fraction == 1 &&
                    test_support::read_settings(rigid).angular_velocity == engine::Vec3{},
                "caller pose, scale and stop requests replace the last simulated pose"))
    {
        return false;
    }
    if (!expect(node.remove_component(engine::ComponentType::RigidBodyData) &&
                    node.remove_component(engine::ComponentType::ColliderData),
                "replace body components between physical synchronization points"))
    {
        return false;
    }
    collider = node.add_component<engine::Collider>();
    rigid = node.add_component<engine::RigidBody>();
    if (!collider.valid() || !rigid.valid())
    {
        return false;
    }
    collision = test_support::read_settings(collider);
    collision.center = {-2, 0, 0};
    motion = test_support::read_settings(rigid);
    motion.gravity_factor = 0;
    motion.linear_damping = 0;
    motion.angular_damping = 0;
    motion.linear_velocity = {0, 2, 0};
    if (!expect(collider.set_settings(collision) && rigid.set_settings(motion) &&
                    fixture.physics.tick(0) && fixture.physics.body_count() == 1 &&
                    fixture.physics.cast_ray(collider, {10, 8, 2}, {0, -8, 0}, fraction) &&
                    std::abs(fraction - 0.375F) < 0.00001F &&
                    fixture.physics.cast_ray(collider, {9.5, 9, -6}, {0, -8, 0}, fraction) &&
                    fraction == 1 && fixture.physics.tick(1.0 / 60.0) &&
                    std::abs(height(node) - (4 + 2.0 / 60.0)) < 0.00001 &&
                    test_support::read_settings(rigid).angular_velocity == engine::Vec3{},
                "replacement components use their new shape and motion without stale spin"))
    {
        return false;
    }
    return true;
}

bool compound_collision()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto& scene = *fixture.world.scene();
    auto floor = create(scene, "Floor", {0, -0.5, 0}, engine::schema::ColliderShape::box, false);
    auto island = create(scene, "Island", {0, 4, 0}, engine::schema::ColliderShape::box, true);
    if (!floor.valid() || !island.valid())
    {
        return false;
    }
    auto floor_data = test_support::read_settings(floor.get_component<engine::Collider>());
    floor_data.half_extents = {10, 0.5, 10};
    auto collider = island.get_component<engine::Collider>();
    auto compound = test_support::read_settings(collider);
    compound.shape = engine::schema::ColliderShape::compound;
    compound.hulls.push_back(cube_hull(-1));
    compound.hulls.push_back(cube_hull(1));
    auto cast = [&](double x, bool hit)
    {
        float fraction = 0;
        return fixture.physics.cast_sphere({x, 8, 0}, {0, -6, 0}, 0.1F, fraction) &&
               (hit ? fraction > 0.4F && fraction < 0.7F : fraction == 1.0F);
    };
    if (!expect(fixture.physics.set_collider_data(floor.get_component<engine::Collider>(),
                                                  floor_data) &&
                    fixture.physics.set_collider_data(collider, compound) &&
                    fixture.physics.body_count() == 2 && cast(-1, true) && cast(1, true) &&
                    cast(0, false),
                "two convex parts publish as one dynamic body with a real gap"))
    {
        return false;
    }
    engine::PhysicsQueryHit hit;
    if (!expect(fixture.physics.query_ray({1, 8, 0}, {0, -6, 0}, {}, hit) && hit.hit &&
                    hit.object.node_id == island.id() &&
                    hit.object.instance_id == collider.instance_id() &&
                    std::abs(hit.point.y() - 4.5) < 0.01 && hit.normal.y() > 0.99 &&
                    fixture.physics.query_ray({0, 8, 0}, {0, -6, 0}, {}, hit) && empty_hit(hit) &&
                    fixture.physics.query_sphere({0, 8, 0}, {0, -6, 0}, 0.1F, {}, hit) &&
                    empty_hit(hit),
                "compound queries map exact hull hits to one collider and preserve real gaps"))
    {
        return false;
    }
    const auto original = test_support::read_settings(collider);
    auto invalid = compound;
    invalid.hulls.front()->vertices.assign(4, engine::Vec3{});
    if (!expect(!fixture.physics.set_collider_data(collider, invalid) &&
                    test_support::read_settings(collider) == original &&
                    fixture.physics.body_count() == 2 && cast(-1, true) && cast(1, true),
                "degenerate hull preparation leaves live compound and settings unchanged"))
    {
        return false;
    }
    invalid = compound;
    invalid.hulls.front()->vertices.front().mutate_x(std::numeric_limits<double>::infinity());
    if (!expect(!fixture.physics.set_collider_data(collider, invalid) &&
                    !engine::Collider::valid_settings(invalid),
                "nonfinite convex hull input is rejected"))
    {
        return false;
    }
    invalid = compound;
    invalid.hulls.front().reset();
    if (!expect(!engine::Collider::valid_settings(invalid), "null hull is rejected"))
    {
        return false;
    }
    invalid = compound;
    invalid.hulls.clear();
    if (!expect(!engine::Collider::valid_settings(invalid), "empty compound is rejected"))
    {
        return false;
    }
    invalid = compound;
    invalid.shape = engine::schema::ColliderShape::box;
    if (!expect(!engine::Collider::valid_settings(invalid), "ordinary shape rejects stale hulls"))
    {
        return false;
    }
    auto transform = island.get_component<engine::Transform>();
    auto pose = test_support::read_settings(transform);
    pose.scale = {2, 1, 1};
    if (!expect(transform.set_settings(pose) &&
                    !fixture.physics.set_collider_data(collider, compound) &&
                    test_support::read_settings(collider) == original && cast(-1, true),
                "nonuniform compound scale fails without replacing collision"))
    {
        return false;
    }
    pose.scale = {1, 1, 1};
    auto uniform = pose;
    uniform.scale = {2, 2, 2};
    if (!expect(transform.set_settings(uniform) &&
                    fixture.physics.set_collider_data(collider, compound) && cast(2, true) &&
                    cast(0, false) && transform.set_settings(pose) &&
                    fixture.physics.set_collider_data(collider, compound),
                "positive uniform scale transforms every hull consistently"))
    {
        return false;
    }
    engine::schema::ComponentDataT saved;
    if (!expect(transform.set_settings(pose) && collider.build_data(saved) &&
                    collider.restore_data(saved) && fixture.physics.tick(0.0) &&
                    fixture.physics.body_count() == 2 && advance(fixture.physics, 240) &&
                    std::abs(height(island) - 0.5) < 0.08,
                "schema compound round trip retains one rigid body and falls onto floor"))
    {
        return false;
    }
    compound.hulls.clear();
    compound.hulls.push_back(cube_hull(2));
    pose = test_support::read_settings(transform);
    pose.position = {0, 4, 0};
    pose.rotation = {};
    if (!expect(transform.set_settings(pose) &&
                    fixture.physics.set_collider_data(collider, compound) && cast(2, true) &&
                    cast(0, false) && fixture.physics.body_count() == 2 &&
                    advance(fixture.physics, 240) && std::abs(height(island) - 0.5) < 0.08 &&
                    std::abs(test_support::read_settings(transform).position.x()) < 0.05,
                "single off-center hull keeps node origin while Jolt computes center of mass"))
    {
        return false;
    }
    if (!collider.set_enabled(false))
    {
        std::abort();
    }
    invalid = compound;
    invalid.hulls.front()->vertices.assign(4, engine::Vec3{});
    if (!expect(fixture.physics.set_collider_data(collider, invalid) &&
                    fixture.physics.set_collider_data(collider, compound) &&
                    fixture.physics.body_count() == 1,
                "disabled compound removes its body immediately without cooking candidate hulls"))
    {
        return false;
    }
    if (!collider.set_enabled(true))
    {
        std::abort();
    }
    auto ordinary = test_support::read_settings(collider);
    ordinary.shape = engine::schema::ColliderShape::box;
    ordinary.hulls.clear();
    return expect(fixture.physics.set_collider_data(collider, ordinary) &&
                      fixture.physics.body_count() == 2 && scene.destroy_node(island.id()) &&
                      fixture.physics.tick(0.0) && fixture.physics.body_count() == 1,
                  "compound can return to a basic shape and releases through node lifecycle");
}

bool snapshot_reuse_lifecycle()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto& scene = *fixture.world.scene();
    auto node = create(scene, "Compound", {}, engine::schema::ColliderShape::box, false);
    if (!node.valid())
    {
        return false;
    }
    auto collider = node.get_component<engine::Collider>();
    auto compound = test_support::read_settings(collider);
    compound.shape = engine::schema::ColliderShape::compound;
    compound.hulls.push_back(cube_hull(-1));
    compound.hulls.push_back(cube_hull(1));
    auto cast = [&](double x, bool hit)
    {
        float fraction = 0;
        return fixture.physics.cast_sphere({x, 4, 0}, {0, -8, 0}, 0.1F, fraction) &&
               (hit ? fraction > 0.0F && fraction < 1.0F : fraction == 1.0F);
    };
    if (!expect(collider.set_settings(compound) && fixture.physics.tick(0.0) &&
                    fixture.physics.tick(0.0) && fixture.physics.tick(0.0) &&
                    fixture.physics.body_count() == 1 && cast(-1, true) && cast(1, true) &&
                    cast(0, false),
                "unchanged compound snapshots retain both hulls across zero-delta frames"))
    {
        return false;
    }
    // settings 返回独立快照；嵌套数据也只能经校验后的 setter 提交。
    auto edited = test_support::read_settings(collider);
    for (auto& vertex : edited.hulls.front()->vertices)
    {
        vertex.mutate_x(vertex.x() - 2.0);
    }
    if (!expect(fixture.physics.tick(0.0) && cast(-1, true) && cast(-3, false) &&
                    collider.set_settings(edited) && fixture.physics.tick(0.0) && cast(-3, true) &&
                    cast(-1, false) && cast(1, true),
                "nested snapshot edits stay isolated until their explicit collision commit"))
    {
        return false;
    }
    edited.hulls.front()->vertices.front().mutate_x(std::numeric_limits<double>::infinity());
    if (!expect(!collider.set_settings(edited) && fixture.physics.tick(0.0) &&
                    fixture.physics.body_count() == 1 && cast(-3, true) &&
                    collider.set_settings(compound) && fixture.physics.tick(0.0) && cast(-1, true),
                "invalid nested snapshot edits preserve the last valid collision state"))
    {
        return false;
    }
    compound.hulls.front() = cube_hull(-3);
    if (!expect(fixture.physics.set_collider_data(collider, compound) &&
                    fixture.physics.tick(0.0) && cast(-3, true) && cast(-1, false),
                "explicit collider publication remains visible after snapshot reuse"))
    {
        return false;
    }
    if (!node.remove_component(engine::Collider::component_type))
    {
        return false;
    }
    collider = node.add_component<engine::Collider>();
    if (!collider.valid())
    {
        return false;
    }
    auto replacement = test_support::read_settings(collider);
    replacement.center = {3, 0, 0};
    if (!expect(collider.set_settings(replacement) && fixture.physics.tick(0.0) &&
                    fixture.physics.body_count() == 1 && cast(3, true) && cast(-3, false) &&
                    cast(1, false),
                "a replacement component on the same node cannot inherit old hull geometry"))
    {
        return false;
    }
    auto parent = scene.create_node("Collision activation parent");
    if (!expect(parent && node.set_parent(parent), "parent a static collision node"))
    {
        return false;
    }
    if (!parent.set_visible(false))
    {
        std::abort();
    }
    if (!expect(fixture.physics.tick(0.0) && cast(3, true), "hidden geometry retains collision"))
    {
        return false;
    }
    if (!parent.set_enabled(false))
    {
        std::abort();
    }
    if (!expect(fixture.physics.tick(0.0) && fixture.physics.body_count() == 0 && cast(3, false) &&
                    collider.enabled(),
                "inactive parent suspends child collision without overwriting local enabled"))
    {
        return false;
    }
    if (!parent.set_enabled(true))
    {
        std::abort();
    }
    if (!expect(fixture.physics.tick(0.0) && cast(3, true),
                "reactivating parent restores collision even while hidden"))
    {
        return false;
    }
    if (!collider.set_enabled(false))
    {
        std::abort();
    }
    if (!expect(fixture.physics.tick(0.0) && fixture.physics.body_count() == 0 && cast(3, false),
                "disabled entries are removed despite retained snapshot array capacity"))
    {
        return false;
    }
    if (!collider.set_enabled(true))
    {
        std::abort();
    }
    return expect(fixture.physics.tick(0.0) && fixture.physics.body_count() == 1 && cast(3, true) &&
                      node.remove_component(engine::Collider::component_type) &&
                      fixture.physics.tick(0.0) && fixture.physics.body_count() == 0 &&
                      cast(3, false),
                  "reenabled and removed colliders follow the current frame membership");
}

bool world_pause_and_resume()
{
    engine::WorldSystem inactive;
    Fixture paused;
    Fixture reference;
    if (!expect(!inactive.paused() && !inactive.set_paused(true) && paused.initialize() &&
                    reference.initialize(),
                "pause requires an initialized world with an active scene"))
    {
        return false;
    }
    auto body = create(*paused.world.scene(), "Paused body", {0, 10, 0},
                       engine::schema::ColliderShape::box, true);
    auto other = create(*reference.world.scene(), "Reference body", {0, 10, 0},
                        engine::schema::ColliderShape::box, true);
    if (!expect(body && other && paused.physics.tick(1.0 / 60.0) &&
                    reference.physics.tick(1.0 / 60.0),
                "prepare identical Jolt bodies"))
    {
        return false;
    }
    const auto before = test_support::read_settings(body.get_component<engine::Transform>());
    const auto velocity = body.get_component<engine::RigidBody>().linear_velocity();
    if (!expect(paused.world.set_paused(true) && paused.world.set_paused(true) &&
                    paused.world.paused() && paused.world.tick(60) && paused.physics.tick(60) &&
                    test_support::read_settings(body.get_component<engine::Transform>()) ==
                        before &&
                    body.get_component<engine::RigidBody>().linear_velocity() == velocity &&
                    paused.physics.contacts().empty(),
                "world pause preserves pose and velocity without new contacts"))
    {
        return false;
    }
    if (!expect(paused.world.set_paused(false) && paused.physics.tick(1.0 / 60.0) &&
                    reference.physics.tick(1.0 / 60.0) &&
                    std::abs(height(body) - height(other)) < 1.0e-6,
                "resume consumes one new step without accumulating paused time"))
    {
        return false;
    }
    if (!paused.world.set_paused(true))
    {
        return false;
    }
    paused.world.destroy_scene();
    if (!expect(!paused.world.paused() && !paused.world.set_paused(true) &&
                    paused.world.create_scene("Replacement") && !paused.world.paused(),
                "destroy and replacement reset transient world pause"))
    {
        return false;
    }
    paused.world.shutdown();
    return expect(!paused.world.paused() && !paused.world.set_paused(true),
                  "shutdown clears world pause");
}

bool run_suite()
{
    return world_pause_and_resume() && collision_and_lifecycle() && fixed_step_and_validation() &&
           velocity_updates_at_capacity() && hierarchy_and_kinematic() &&
           trigger_and_registration() && rotation_and_scale() && transient_mesh_collision() &&
           segmented_mesh_collision() && segmented_mesh_seams() && segmented_mesh_transforms() &&
           segmented_mesh_character_seams() && accumulated_shape_scale() && numeric_boundaries() &&
           compound_box_geometry() && dynamic_pose_feedback() && compound_collision() &&
           snapshot_reuse_lifecycle() && targeted_ray() && closest_queries() && query_filters() &&
           query_lifecycle() && impact_snapshots();
}

bool occupied_workers(engine::ThreadSystem& threads, bool reject_submissions)
{
    engine::TaskConfig task_config;
    task_config.name = "Physics occupied worker";
    task_config.max_concurrency = 1;
    task_config.max_pending = reject_submissions ? 1 : 8192;
    engine::TaskSystem tasks(threads, task_config);
    engine::WorldSystem world;
    engine::PhysicsSystem physics(&world, &tasks);
    if (!expect(tasks.initialize({}) && world.initialize({}) && physics.initialize({}) &&
                    world.create_scene("Occupied worker"),
                "initialize physics with an independently occupied task domain"))
    {
        return false;
    }
    auto body = create(*world.scene(), "Falling body", {0, 1000, 0},
                       engine::schema::ColliderShape::box, true);
    if (!expect(body.valid(), "create body for queued-wrapper regression"))
    {
        return false;
    }

    std::binary_semaphore started{0};
    std::binary_semaphore release_worker{0};
    auto blocker = tasks.submit("Occupy physics worker",
                                [&]
                                {
                                    started.release();
                                    release_worker.acquire();
                                    return true;
                                });
    if (!expect(blocker.valid(), "accept the worker gate"))
    {
        return false;
    }
    if (!started.try_acquire_for(std::chrono::seconds(5)))
    {
        release_worker.release();
        const bool drained = tasks.wait();
        return expect(drained && blocker.get(), "release delayed worker gate") &&
               expect(false, "worker gate started before physics steps");
    }

    std::binary_semaphore stepped{0};
    std::binary_semaphore cleared{0};
    std::atomic<bool> step_timeout{false};
    std::atomic<bool> cleared_before_workers{false};
    std::jthread release(
        [&]
        {
            // A broken adapter must fail the test instead of hanging its caller forever.
            if (!stepped.try_acquire_for(std::chrono::seconds(5)))
            {
                step_timeout = true;
            }
            else if (!reject_submissions)
            {
                cleared_before_workers = cleared.try_acquire_for(std::chrono::milliseconds(50));
            }
            release_worker.release();
        });
    // Enough updates to exhaust the job free-list if stale wrapper references are unbounded.
    const bool advanced = advance(physics, 512);
    const bool published =
        height(body) < 900 && body.get_component<engine::RigidBody>().linear_velocity().y() < -50;
    stepped.release();
    physics.clear();
    cleared.release();
    release.join();
    const bool drained = tasks.wait();
    if (!expect(advanced && published && !step_timeout,
                "official barriers finish repeated simulation while workers are occupied") ||
        !expect(!cleared_before_workers,
                "clear drains accepted wrapper references before releasing the Jolt world") ||
        !expect(drained && blocker.get(), "all accepted tasks finish after opening the gate") ||
        !expect(physics.body_count() == 0 && physics.tick(0) && physics.body_count() == 1,
                "clear and recreation retain no stale wrapper or body state"))
    {
        return false;
    }
    physics.shutdown();
    return expect(physics.initialize({}) && physics.tick(1.0 / 60.0),
                  "physics can restart with the same task domain after draining jobs");
}

} // namespace

int main()
{
    if (!run_suite())
    {
        return 1;
    }

    engine::ThreadSystem threads({.worker_limit = 2});
    engine::TaskSystem tasks(
        threads,
        {.name = "Physics tests", .max_concurrency = 2, .priority = engine::TaskPriority::high});
    engine::PhysicsSystem invalid(nullptr, &tasks);
    if (!expect(!invalid.initialize({}), "physics rejects an uninitialized borrowed task domain") ||
        !expect(threads.initialize({}) && tasks.initialize({}), "initialize shared Jolt workers"))
    {
        return 1;
    }
    physics_tasks = &tasks;
    const bool parallel_ok = run_suite();
    physics_tasks = nullptr;
    if (!parallel_ok || !expect(tasks.wait(), "all Jolt wrappers complete before teardown"))
    {
        return 1;
    }
    tasks.shutdown();
    if (!occupied_workers(threads, false) || !occupied_workers(threads, true))
    {
        return 1;
    }

    engine::TaskConfig limited_config;
    limited_config.name = "Physics limited";
    limited_config.max_concurrency = 1;
    limited_config.max_pending = 1;
    engine::TaskSystem limited(threads, limited_config);
    if (!expect(limited.initialize({}), "initialize one-job physics task budget"))
    {
        return 1;
    }
    physics_tasks = &limited;
    const bool limited_ok = collision_and_lifecycle() && impact_snapshots();
    physics_tasks = nullptr;
    return limited_ok && expect(limited.wait(), "saturated task domain never drops physics jobs")
               ? 0
               : 1;
}
