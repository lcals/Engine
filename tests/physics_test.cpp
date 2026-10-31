#include "runtime/engine_config.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/rigid_body.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/schema/mesh_generated.h"
#include "runtime/world/world_system.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <utility>

namespace
{

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
    engine::PhysicsSystem physics{&world};

    bool initialize()
    {
        const engine::EngineConfig config;
        return world.initialize(config) && physics.initialize(config) &&
               world.create_scene("Physics") != nullptr;
    }
};

engine::Node* create(engine::Scene& scene, const char* name, const engine::Vec3& position,
                     engine::schema::ColliderShape shape, bool dynamic)
{
    auto* node = scene.create_node(name);
    if (node == nullptr)
    {
        return nullptr;
    }
    auto* transform = node->get_component<engine::Transform>();
    auto settings = transform->settings();
    settings.position = position;
    auto* collider = node->add_component<engine::Collider>();
    if (collider == nullptr || !transform->set_settings(settings))
    {
        return nullptr;
    }
    auto collision = collider->settings();
    collision.shape = shape;
    collision.restitution = 0.0F;
    if (!collider->set_settings(collision) ||
        (dynamic && node->add_component<engine::RigidBody>() == nullptr))
    {
        return nullptr;
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

double height(const engine::Node& node)
{
    return node.get_component<engine::Transform>()->settings().position.y();
}

bool collision_and_lifecycle()
{
    Fixture fixture;
    if (!expect(fixture.initialize(), "initialize real collision world"))
    {
        return false;
    }
    auto& scene = *fixture.world.scene();
    auto* floor = create(scene, "Floor", {0, -0.5, 0}, engine::schema::ColliderShape::box, false);
    auto* box = create(scene, "Box", {-2, 4, 0}, engine::schema::ColliderShape::box, true);
    auto* sphere = create(scene, "Sphere", {0, 4, 0}, engine::schema::ColliderShape::sphere, true);
    auto* capsule =
        create(scene, "Capsule", {2, 4, 0}, engine::schema::ColliderShape::capsule, true);
    if (!expect(floor && box && sphere && capsule, "create schema-backed physical nodes"))
    {
        return false;
    }
    auto floor_settings = floor->get_component<engine::Collider>()->settings();
    floor_settings.half_extents = {8, 0.5, 8};
    if (!expect(floor->get_component<engine::Collider>()->set_settings(floor_settings),
                "configure floor") ||
        !expect(fixture.physics.tick(0.0) && fixture.physics.body_count() == 4,
                "zero delta creates physical bodies without advancing") ||
        !expect(height(*box) == 4.0, "no implicit simulation during synchronization") ||
        !expect(advance(fixture.physics, 240), "simulate falling shapes") ||
        !expect(std::abs(height(*box) - 0.5) < 0.08, "box rests on actual floor collision") ||
        !expect(std::abs(height(*sphere) - 0.5) < 0.08, "sphere rests on floor") ||
        !expect(std::abs(height(*capsule) - 1.0) < 0.1, "capsule rests on floor") ||
        !expect(height(*floor) == -0.5, "collider without rigid body stays static"))
    {
        return false;
    }
    auto* rigid = box->get_component<engine::RigidBody>();
    auto body_settings = rigid->settings();
    body_settings.mass = 2.0F;
    body_settings.linear_velocity = {};
    body_settings.angular_velocity = {};
    engine::schema::ComponentDataT before;
    engine::schema::ComponentDataT after;
    if (!expect(rigid->set_settings(body_settings) && rigid->build_data(before) &&
                    rigid->add_impulse({0, 4, 0}) && rigid->build_data(after) && before == after,
                "pending impulse is transient and mass remains schema data") ||
        !expect(fixture.physics.tick(0.0) && std::abs(rigid->linear_velocity().y() - 2.0) < 0.01,
                "impulse changes real body velocity according to mass") ||
        !expect(fixture.physics.tick(0.0) && std::abs(rigid->linear_velocity().y() - 2.0) < 0.01,
                "impulse is consumed exactly once") ||
        !expect(advance(fixture.physics, 5) && height(*box) > 0.6, "impulse lifts the box"))
    {
        return false;
    }
    rigid->set_enabled(false);
    if (!expect(fixture.physics.tick(0.0) && fixture.physics.body_count() == 3,
                "disabled rigid body is removed"))
    {
        return false;
    }
    rigid->set_enabled(true);
    sphere->get_component<engine::Collider>()->set_enabled(false);
    if (!expect(fixture.physics.tick(0.0) && fixture.physics.body_count() == 3,
                "enable rigid body and disable collider update the body set") ||
        !expect(scene.destroy_node(capsule->id()) && fixture.physics.tick(0.0) &&
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
    auto* node = create(*fixture.world.scene(), "Falling", {0, 4, 0},
                        engine::schema::ColliderShape::box, true);
    if (!node)
    {
        return false;
    }
    auto* collider = node->get_component<engine::Collider>();
    auto collision = collider->settings();
    collision.radius = std::numeric_limits<float>::quiet_NaN();
    auto* rigid = node->get_component<engine::RigidBody>();
    auto invalid = rigid->settings();
    invalid.mass = 0;
    if (!expect(!engine::Collider::valid_settings(collision) &&
                    !collider->set_settings(collision) && collider->settings().radius == 0.5F,
                "invalid collider settings preserve existing state") ||
        !expect(!engine::RigidBody::valid_settings(invalid) && !rigid->set_settings(invalid) &&
                    rigid->settings().mass == 1.0F,
                "invalid body settings preserve existing state") ||
        !expect(!rigid->add_impulse({std::numeric_limits<double>::infinity(), 0, 0}),
                "nonfinite impulse is rejected") ||
        !expect(fixture.physics.tick(1.0 / 120.0) && height(*node) == 4,
                "partial fixed step does not move") ||
        !expect(fixture.physics.tick(1.0 / 120.0) && height(*node) < 4,
                "two half steps produce one physical update") ||
        !expect(fixture.physics.tick(1000) && height(*node) > 3.5,
                "long frame is bounded to eight physical substeps"))
    {
        return false;
    }
    auto settings = rigid->settings();
    settings.motion = engine::schema::BodyMotion::static_body;
    settings.linear_velocity = {};
    settings.angular_velocity = {};
    if (!expect(rigid->set_settings(settings) && !rigid->add_impulse({0, 1, 0}) &&
                    !rigid->set_linear_velocity({0, 1, 0}),
                "static bodies reject motion requests"))
    {
        return false;
    }
    const double previous = height(*node);
    return expect(advance(fixture.physics, 20) && height(*node) == previous,
                  "explicit static body remains stationary");
}

bool hierarchy_and_kinematic()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto& scene = *fixture.world.scene();
    auto* parent = scene.create_node("Parent");
    auto* platform =
        create(scene, "Platform", {0, 1, 0}, engine::schema::ColliderShape::box, false);
    auto* ball = create(scene, "Ball", {5, 5, 0}, engine::schema::ColliderShape::sphere, true);
    if (!parent || !platform || !ball)
    {
        return false;
    }
    auto parent_settings = parent->get_component<engine::Transform>()->settings();
    parent_settings.position = {5, 0, 0};
    parent_settings.rotation = {0, 90, 0};
    parent_settings.scale = {2, 2, 2};
    if (!parent->get_component<engine::Transform>()->set_settings(parent_settings) ||
        !platform->set_parent(parent) || !advance(fixture.physics, 240) ||
        !expect(std::abs(height(*ball) - 3.5) < 0.1,
                "static collision uses complete parent translation rotation and scale"))
    {
        return false;
    }
    ball->get_component<engine::Collider>()->set_enabled(false);
    auto* mover = create(scene, "Kinematic", {0, 3, 0}, engine::schema::ColliderShape::box, true);
    if (!mover || !mover->set_parent(parent))
    {
        return false;
    }
    auto motion = mover->get_component<engine::RigidBody>()->settings();
    motion.motion = engine::schema::BodyMotion::kinematic;
    motion.linear_velocity = {1, 0, 0};
    motion.linear_damping = 0;
    std::array<float, 16> start{};
    std::array<float, 16> end{};
    if (!mover->get_component<engine::RigidBody>()->set_settings(motion) ||
        !mover->get_component<engine::Transform>()->world_matrix(start.data()) ||
        !advance(fixture.physics, 60) ||
        !mover->get_component<engine::Transform>()->world_matrix(end.data()) ||
        !expect(std::abs((end[12] - start[12]) - 1.0F) < 0.05F &&
                    std::abs(end[13] - start[13]) < 0.01F,
                "kinematic world velocity writes correct local pose through parent"))
    {
        return false;
    }
    motion.motion = engine::schema::BodyMotion::dynamic;
    if (!mover->get_component<engine::RigidBody>()->set_settings(motion) ||
        !expect(!fixture.physics.tick(0.0) && fixture.physics.body_count() == 0,
                "unsupported dynamic parent fails explicitly and clears bodies"))
    {
        return false;
    }
    mover->get_component<engine::RigidBody>()->set_enabled(false);
    parent_settings.scale = {2, 1, 1};
    auto platform_settings = platform->get_component<engine::Transform>()->settings();
    platform_settings.rotation = {0, 45, 0};
    return parent->get_component<engine::Transform>()->set_settings(parent_settings) &&
           platform->get_component<engine::Transform>()->set_settings(platform_settings) &&
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
    auto* trigger = create(scene, "Trigger", {0, 0, 0}, engine::schema::ColliderShape::box, false);
    auto* ball = create(scene, "Ball", {0, 2, 0}, engine::schema::ColliderShape::sphere, true);
    if (!trigger || !ball)
    {
        return false;
    }
    auto settings = trigger->get_component<engine::Collider>()->settings();
    settings.is_trigger = true;
    if (!trigger->get_component<engine::Collider>()->set_settings(settings))
    {
        return false;
    }
    return expect(advance(second.physics, 90) && height(*ball) < -1.0,
                  "trigger has no physical response and another system shutdown preserves Jolt");
}

bool rotation_and_scale()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto* node = create(*fixture.world.scene(), "Rotating", {0, 4, 0},
                        engine::schema::ColliderShape::box, true);
    if (!node)
    {
        return false;
    }
    auto* transform = node->get_component<engine::Transform>();
    auto* rigid = node->get_component<engine::RigidBody>();
    auto motion = rigid->settings();
    motion.gravity_factor = 0;
    const std::array rotations{engine::Vec3(30, 90, 0),        engine::Vec3(30, 90, 20),
                               engine::Vec3(-7, -90, 0),       engine::Vec3(45, 89.999, 20),
                               engine::Vec3(-35, -89.999, 70), engine::Vec3(30, 45, 20)};
    for (const auto mode :
         {engine::schema::BodyMotion::dynamic, engine::schema::BodyMotion::kinematic})
    {
        motion.motion = mode;
        if (!rigid->set_settings(motion))
        {
            return false;
        }
        for (const auto& rotation : rotations)
        {
            fixture.physics.clear();
            auto pose = transform->settings();
            pose.rotation = rotation;
            pose.scale = {1, 2, 0.5};
            std::array<float, 16> before{};
            std::array<float, 16> after{};
            if (!transform->set_settings(pose) || !transform->world_matrix(before.data()) ||
                !advance(fixture.physics, 20) || !transform->world_matrix(after.data()))
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
    auto pose = transform->settings();
    pose.rotation = {};
    pose.scale = {-1, -1, 1};
    if (!expect(transform->set_settings(pose) && !fixture.physics.tick(0.0) &&
                    fixture.physics.body_count() == 0,
                "negative local scales are rejected even when world determinant is positive"))
    {
        return false;
    }
    auto* collider = node->get_component<engine::Collider>();
    auto collision = collider->settings();
    collision.shape = engine::schema::ColliderShape::sphere;
    collision.radius = 1000;
    pose.scale = {0.0001, 0.00019, 0.0001};
    if (!expect(collider->set_settings(collision) && transform->set_settings(pose) &&
                    !fixture.physics.tick(0.0),
                "small nonuniform sphere scale cannot pass an absolute tolerance"))
    {
        return false;
    }
    collision.shape = engine::schema::ColliderShape::capsule;
    collision.half_height = 1000;
    pose.scale = {0.0001, 0.0001, 0.00019};
    if (!expect(collider->set_settings(collision) && transform->set_settings(pose) &&
                    !fixture.physics.tick(0.0),
                "small unequal capsule transverse scales are rejected"))
    {
        return false;
    }
    pose.scale = {0.0001, 0.0001, 0.0001};
    return expect(transform->set_settings(pose) && fixture.physics.tick(0.0) &&
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
    auto* floor =
        create(*fixture.world.scene(), "Generated", {}, engine::schema::ColliderShape::box, false);
    if (floor == nullptr)
    {
        return false;
    }
    auto* collider = floor->get_component<engine::Collider>();
    auto mesh = collision_grid(false);
    auto cast = [&](double x, bool should_hit)
    {
        float fraction = 0;
        return fixture.physics.cast_sphere({x, 3, 0.25}, {0, -6, 0}, 0.1F, fraction) &&
               (should_hit ? fraction > 0.4F && fraction < 0.6F : fraction == 1.0F);
    };
    if (!expect(fixture.physics.set_mesh_data(*collider, mesh) &&
                    fixture.physics.body_count() == 1 && cast(0.25, true),
                "transient mesh commits immediately without resource system or extra tick"))
    {
        return false;
    }
    const auto* previous = collider->mesh_data();
    mesh.vertices.clear();
    if (!expect(previous != nullptr && previous->vertices.size() == 25 &&
                    collider->mesh_path().empty() && collider->settings().mesh_guid.empty(),
                "transient collision owns an immutable copy and has no fabricated resource GUID") ||
        !expect(!fixture.physics.set_mesh_data(*collider, mesh) &&
                    collider->mesh_data() == previous && cast(0.25, true),
                "invalid replacement preserves component and live collision"))
    {
        return false;
    }
    auto invalid = collision_grid(false);
    invalid.indices[0] = 1000;
    if (!expect(!fixture.physics.set_mesh_data(*collider, invalid) && cast(0.25, true),
                "out of range triangle preserves old shape"))
    {
        return false;
    }
    invalid = collision_grid(false);
    invalid.indices.assign({0, 0, 0});
    if (!expect(!fixture.physics.set_mesh_data(*collider, invalid) && cast(0.25, true),
                "Jolt rejected degenerate mesh leaves original shape intact"))
    {
        return false;
    }
    engine::schema::ComponentDataT serialized;
    serialized.payload.Set(engine::schema::ColliderDataT{});
    const auto original_data = collider->data();
    if (!expect(!collider->build_data(serialized) && serialized.payload.value == nullptr &&
                    !collider->restore_data(original_data) && collider->mesh_data() == previous,
                "transient geometry cannot silently serialize or restore without a GUID") ||
        !expect(!fixture.physics.set_mesh_data(*collider, invalid) && fixture.physics.tick(0.0),
                "failed mesh preparation does not poison subsequent physics ticks"))
    {
        return false;
    }
    auto* rigid = floor->add_component<engine::RigidBody>();
    if (rigid == nullptr)
    {
        return false;
    }
    auto moving = rigid->settings();
    if (!expect(!rigid->set_settings(moving), "mesh cannot switch to dynamic motion"))
    {
        return false;
    }
    moving.motion = engine::schema::BodyMotion::static_body;
    if (!rigid->set_settings(moving) ||
        !expect(fixture.physics.set_mesh_data(*collider, collision_grid(true)) &&
                    cast(0.25, false) && cast(1.5, true) && fixture.physics.tick(0.0) &&
                    cast(0.25, false),
                "hole replacement keeps surrounding collision and survives synchronization"))
    {
        return false;
    }
    collider->set_enabled(false);
    if (!expect(fixture.physics.tick(0.0) && fixture.physics.body_count() == 0 &&
                    fixture.physics.set_mesh_data(*collider, collision_grid(false)) &&
                    fixture.physics.body_count() == 0 && cast(0.25, false),
                "disabled collider accepts geometry without creating a body"))
    {
        return false;
    }
    collider->set_enabled(true);
    if (!expect(fixture.physics.tick(0.0) && cast(0.25, true),
                "reenabling collider restores its prepared geometry"))
    {
        return false;
    }
    auto* box = create(*fixture.world.scene(), "Supported", {1.5, 3, 0},
                       engine::schema::ColliderShape::box, true);
    if (!box || !advance(fixture.physics, 240) ||
        !expect(std::abs(height(*box) - 0.5) < 0.08, "dynamic body rests on generated mesh") ||
        !expect(fixture.physics.set_mesh_data(*collider, {}) && fixture.physics.body_count() == 1 &&
                    advance(fixture.physics, 60) && height(*box) < -1.0 && cast(0.25, false),
                "empty mesh removes support, wakes resting bodies and never creates a box"))
    {
        return false;
    }
    if (!expect(fixture.physics.set_mesh_data(*collider, collision_grid(false)) && cast(0.25, true),
                "nonempty replacement restores an emptied collider") ||
        !expect(floor->remove_component(engine::Collider::component_type) &&
                    fixture.physics.tick(0.0) && fixture.physics.body_count() == 1 &&
                    cast(0.25, false),
                "removed collider releases its live collision"))
    {
        return false;
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

bool compound_collision()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto& scene = *fixture.world.scene();
    auto* floor = create(scene, "Floor", {0, -0.5, 0}, engine::schema::ColliderShape::box, false);
    auto* island = create(scene, "Island", {0, 4, 0}, engine::schema::ColliderShape::box, true);
    if (floor == nullptr || island == nullptr)
    {
        return false;
    }
    auto floor_data = floor->get_component<engine::Collider>()->settings();
    floor_data.half_extents = {10, 0.5, 10};
    auto* collider = island->get_component<engine::Collider>();
    auto compound = collider->settings();
    compound.shape = engine::schema::ColliderShape::compound;
    compound.hulls.push_back(cube_hull(-1));
    compound.hulls.push_back(cube_hull(1));
    auto cast = [&](double x, bool hit)
    {
        float fraction = 0;
        return fixture.physics.cast_sphere({x, 8, 0}, {0, -6, 0}, 0.1F, fraction) &&
               (hit ? fraction > 0.4F && fraction < 0.7F : fraction == 1.0F);
    };
    if (!expect(fixture.physics.set_collider_data(*floor->get_component<engine::Collider>(),
                                                  floor_data) &&
                    fixture.physics.set_collider_data(*collider, compound) &&
                    fixture.physics.body_count() == 2 && cast(-1, true) && cast(1, true) &&
                    cast(0, false),
                "two convex parts publish as one dynamic body with a real gap"))
    {
        return false;
    }
    const auto original = collider->settings();
    auto invalid = compound;
    invalid.hulls.front()->vertices.assign(4, engine::Vec3{});
    if (!expect(!fixture.physics.set_collider_data(*collider, invalid) &&
                    collider->settings() == original && fixture.physics.body_count() == 2 &&
                    cast(-1, true) && cast(1, true),
                "degenerate hull preparation leaves live compound and settings unchanged"))
    {
        return false;
    }
    invalid = compound;
    invalid.hulls.front()->vertices.front().mutate_x(std::numeric_limits<double>::infinity());
    if (!expect(!fixture.physics.set_collider_data(*collider, invalid) &&
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
    auto* transform = island->get_component<engine::Transform>();
    auto pose = transform->settings();
    pose.scale = {2, 1, 1};
    if (!expect(transform->set_settings(pose) &&
                    !fixture.physics.set_collider_data(*collider, compound) &&
                    collider->settings() == original && cast(-1, true),
                "nonuniform compound scale fails without replacing collision"))
    {
        return false;
    }
    pose.scale = {1, 1, 1};
    auto uniform = pose;
    uniform.scale = {2, 2, 2};
    if (!expect(transform->set_settings(uniform) &&
                    fixture.physics.set_collider_data(*collider, compound) && cast(2, true) &&
                    cast(0, false) && transform->set_settings(pose) &&
                    fixture.physics.set_collider_data(*collider, compound),
                "positive uniform scale transforms every hull consistently"))
    {
        return false;
    }
    engine::schema::ComponentDataT saved;
    if (!expect(transform->set_settings(pose) && collider->build_data(saved) &&
                    collider->restore_data(saved) && fixture.physics.tick(0.0) &&
                    fixture.physics.body_count() == 2 && advance(fixture.physics, 240) &&
                    std::abs(height(*island) - 0.5) < 0.08,
                "schema compound round trip retains one rigid body and falls onto floor"))
    {
        return false;
    }
    compound.hulls.clear();
    compound.hulls.push_back(cube_hull(2));
    pose = transform->settings();
    pose.position = {0, 4, 0};
    pose.rotation = {};
    if (!expect(transform->set_settings(pose) &&
                    fixture.physics.set_collider_data(*collider, compound) && cast(2, true) &&
                    cast(0, false) && fixture.physics.body_count() == 2 &&
                    advance(fixture.physics, 240) && std::abs(height(*island) - 0.5) < 0.08 &&
                    std::abs(transform->settings().position.x()) < 0.05,
                "single off-center hull keeps node origin while Jolt computes center of mass"))
    {
        return false;
    }
    collider->set_enabled(false);
    invalid = compound;
    invalid.hulls.front()->vertices.assign(4, engine::Vec3{});
    if (!expect(fixture.physics.set_collider_data(*collider, invalid) &&
                    fixture.physics.set_collider_data(*collider, compound) &&
                    fixture.physics.body_count() == 1,
                "disabled compound removes its body immediately without cooking candidate hulls"))
    {
        return false;
    }
    collider->set_enabled(true);
    auto ordinary = collider->settings();
    ordinary.shape = engine::schema::ColliderShape::box;
    ordinary.hulls.clear();
    return expect(fixture.physics.set_collider_data(*collider, ordinary) &&
                      fixture.physics.body_count() == 2 && scene.destroy_node(island->id()) &&
                      fixture.physics.tick(0.0) && fixture.physics.body_count() == 1,
                  "compound can return to a basic shape and releases through node lifecycle");
}

} // namespace

int main()
{
    return collision_and_lifecycle() && fixed_step_and_validation() && hierarchy_and_kinematic() &&
                   trigger_and_registration() && rotation_and_scale() &&
                   transient_mesh_collision() && numeric_boundaries() && compound_collision()
               ? 0
               : 1;
}
