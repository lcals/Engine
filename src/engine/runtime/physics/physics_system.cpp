#include "runtime/physics/physics_system.hpp"

#include "runtime/scene/collider.hpp"
#include "runtime/scene/rigid_body.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/world/world_system.hpp"

// Jolt 要求先包含基础定义，不能按文件名字母顺序重排。
// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
// clang-format on

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <new>
#include <numbers>
#include <set>
#include <utility>
#include <vector>

namespace engine
{
namespace
{

constexpr double fixed_step = 1.0 / 60.0;
constexpr std::size_t maximum_bodies = 4096;
constexpr unsigned int maximum_steps = 8;
constexpr JPH::ObjectLayer static_layer = 0;
constexpr JPH::ObjectLayer moving_layer = 1;

std::mutex registration_mutex;
JPH::Factory* owned_factory = nullptr;
std::size_t factory_users = 0;

bool acquire_jolt()
{
    const std::lock_guard lock(registration_mutex);
    if (!JPH::VerifyJoltVersionID() ||
        (factory_users != 0 && JPH::Factory::sInstance != owned_factory) ||
        (JPH::Factory::sInstance != nullptr && JPH::Factory::sInstance != owned_factory))
    {
        return false;
    }
    if (factory_users == 0)
    {
#ifndef JPH_DISABLE_CUSTOM_ALLOCATOR
        if (JPH::Allocate == nullptr && JPH::Reallocate == nullptr && JPH::Free == nullptr &&
            JPH::AlignedAllocate == nullptr && JPH::AlignedFree == nullptr)
        {
            JPH::RegisterDefaultAllocator();
        }
        if (JPH::Allocate == nullptr || JPH::Reallocate == nullptr || JPH::Free == nullptr ||
            JPH::AlignedAllocate == nullptr || JPH::AlignedFree == nullptr)
        {
            return false;
        }
#endif
        owned_factory = new JPH::Factory();
        if (owned_factory == nullptr)
        {
            return false;
        }
        JPH::Factory::sInstance = owned_factory;
        JPH::RegisterTypes();
    }
    ++factory_users;
    return true;
}

void release_jolt() noexcept
{
    const std::lock_guard lock(registration_mutex);
    if (factory_users == 0 || --factory_users != 0)
    {
        return;
    }
    if (JPH::Factory::sInstance == owned_factory)
    {
        JPH::UnregisterTypes();
        JPH::Factory::sInstance = nullptr;
    }
    delete owned_factory;
    owned_factory = nullptr;
}

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
{
public:
    JPH::uint GetNumBroadPhaseLayers() const override
    {
        return 2;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(layer));
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        return layer.GetValue() == static_layer ? "Static" : "Moving";
    }
#endif
};

class BroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer object, JPH::BroadPhaseLayer broad) const override
    {
        return object == moving_layer || broad.GetValue() == moving_layer;
    }
};

class ObjectFilter final : public JPH::ObjectLayerPairFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer left, JPH::ObjectLayer right) const override
    {
        return left == moving_layer || right == moving_layer;
    }
};

JPH::Vec3 vector(const Vec3& value)
{
    return {static_cast<float>(value.x()), static_cast<float>(value.y()),
            static_cast<float>(value.z())};
}

Vec3 vector(JPH::Vec3Arg value)
{
    return {value.GetX(), value.GetY(), value.GetZ()};
}

bool finite(JPH::Vec3Arg value)
{
    return std::isfinite(value.GetX()) && std::isfinite(value.GetY()) &&
           std::isfinite(value.GetZ());
}

bool close(float left, float right)
{
    return std::abs(left - right) <= 0.0001F * std::max(std::abs(left), std::abs(right));
}

Vec3 euler_angles(JPH::QuatArg rotation)
{
    // 使用 double 重建正交矩阵，避免 float 在 pitch 接近 90 度时的相消误差。
    const double x = rotation.GetX();
    const double y = rotation.GetY();
    const double z = rotation.GetZ();
    const double w = rotation.GetW();
    const double norm = x * x + y * y + z * z + w * w;
    const double r00 = 1.0 - 2.0 * (y * y + z * z) / norm;
    const double r10 = 2.0 * (x * y + w * z) / norm;
    const double r20 = 2.0 * (x * z - w * y) / norm;
    const double cos_pitch = std::hypot(r00, r10);
    const double pitch = std::atan2(-r20, cos_pitch);
    double roll = 0.0;
    double yaw = 0.0;
    if (cos_pitch > 0.000001)
    {
        roll = std::atan2(2.0 * (y * z + w * x), norm - 2.0 * (x * x + y * y));
        yaw = std::atan2(r10, r00);
    }
    else
    {
        // Rz * Ry * Rx 在奇异点只有 roll/yaw 的组合可确定；固定 yaw 保留等价姿态。
        roll = std::atan2(2.0 * (w * x - y * z), norm - 2.0 * (x * x + z * z));
    }
    constexpr double degrees = 180.0 / std::numbers::pi;
    return {roll * degrees, pitch * degrees, yaw * degrees};
}

bool decompose(const JPH::Mat44& matrix, JPH::Vec3& position, JPH::Quat& rotation, JPH::Vec3& scale)
{
    const auto x = matrix.GetAxisX();
    const auto y = matrix.GetAxisY();
    const auto z = matrix.GetAxisZ();
    scale = {x.Length(), y.Length(), z.Length()};
    position = matrix.GetTranslation();
    if (!finite(scale) || !finite(position) || scale.ReduceMin() < 0.00001F ||
        scale.ReduceMax() > 100000.0F || position.Abs().ReduceMax() > 10000000.0F)
    {
        return false;
    }
    const auto nx = x / scale.GetX();
    const auto ny = y / scale.GetY();
    const auto nz = z / scale.GetZ();
    if (std::abs(nx.Dot(ny)) > 0.0001F || std::abs(nx.Dot(nz)) > 0.0001F ||
        std::abs(ny.Dot(nz)) > 0.0001F || nx.Cross(ny).Dot(nz) < 0.999F)
    {
        return false;
    }
    JPH::Mat44 orientation = JPH::Mat44::sIdentity();
    orientation.SetAxisX(nx);
    orientation.SetAxisY(ny);
    orientation.SetAxisZ(nz);
    rotation = orientation.GetQuaternion().Normalized();
    return true;
}

bool world_matrix(const Transform& transform, JPH::Mat44& output)
{
    std::array<float, 16> values{};
    if (!transform.world_matrix(values.data()))
    {
        return false;
    }
    output = JPH::Mat44(JPH::Vec4(values[0], values[1], values[2], values[3]),
                        JPH::Vec4(values[4], values[5], values[6], values[7]),
                        JPH::Vec4(values[8], values[9], values[10], values[11]),
                        JPH::Vec4(values[12], values[13], values[14], values[15]));
    return true;
}

bool make_shape(const schema::ColliderDataT& collider, JPH::Vec3Arg scale,
                JPH::RefConst<JPH::Shape>& output)
{
    output = nullptr;
    JPH::Shape::ShapeResult result;
    switch (collider.shape)
    {
    case schema::ColliderShape::box:
    {
        const auto extent = vector(collider.half_extents) * scale;
        if (!finite(extent) || extent.ReduceMin() < 0.0001F || extent.ReduceMax() > 100000.0F)
        {
            return false;
        }
        result = JPH::BoxShapeSettings(extent, std::min(0.05F, extent.ReduceMin() * 0.1F)).Create();
        break;
    }
    case schema::ColliderShape::sphere:
        if (!close(scale.GetX(), scale.GetY()) || !close(scale.GetX(), scale.GetZ()) ||
            collider.radius * scale.GetX() < 0.0001F || collider.radius * scale.GetX() > 100000.0F)
        {
            return false;
        }
        result = JPH::SphereShapeSettings(collider.radius * scale.GetX()).Create();
        break;
    case schema::ColliderShape::capsule:
        if (!close(scale.GetX(), scale.GetZ()) || collider.radius * scale.GetX() < 0.0001F ||
            collider.radius * scale.GetX() > 100000.0F ||
            collider.half_height * scale.GetY() > 100000.0F)
        {
            return false;
        }
        result = JPH::CapsuleShapeSettings(collider.half_height * scale.GetY(),
                                           collider.radius * scale.GetX())
                     .Create();
        break;
    default:
        return false;
    }
    if (result.HasError() || !result.IsValid())
    {
        return false;
    }
    const auto center = vector(collider.center) * scale;
    if (!finite(center) || center.Abs().ReduceMax() > 100000.0F)
    {
        return false;
    }
    if (center.LengthSq() > 0.0F)
    {
        result = JPH::RotatedTranslatedShapeSettings(center, JPH::Quat::sIdentity(),
                                                     result.Get().GetPtr())
                     .Create();
        if (result.HasError() || !result.IsValid())
        {
            return false;
        }
    }
    output = result.Get();
    return true;
}

JPH::EMotionType motion_type(schema::BodyMotion motion)
{
    switch (motion)
    {
    case schema::BodyMotion::static_body:
        return JPH::EMotionType::Static;
    case schema::BodyMotion::kinematic:
        return JPH::EMotionType::Kinematic;
    case schema::BodyMotion::dynamic:
        return JPH::EMotionType::Dynamic;
    }
    return JPH::EMotionType::Static;
}

struct BodySnapshot
{
    std::uint64_t node_id = 0;
    schema::ColliderDataT collider;
    schema::RigidBodyDataT rigid_body;
    JPH::Vec3 position = JPH::Vec3::sZero();
    JPH::Quat rotation = JPH::Quat::sIdentity();
    JPH::Vec3 scale = JPH::Vec3::sReplicate(1.0F);
    JPH::RefConst<JPH::Shape> shape;
};

bool read_snapshot(const Node& node, const Collider& collider, const RigidBody* body,
                   BodySnapshot& output)
{
    for (const Node* ancestor = &node; ancestor != nullptr; ancestor = ancestor->parent())
    {
        const auto* ancestor_transform = ancestor->get_component<Transform>();
        const auto* ancestor_body = ancestor->get_component<RigidBody>();
        if (ancestor_transform == nullptr || ancestor_transform->settings().scale.x() <= 0.0 ||
            ancestor_transform->settings().scale.y() <= 0.0 ||
            ancestor_transform->settings().scale.z() <= 0.0 ||
            (ancestor != &node && ancestor_body != nullptr && ancestor_body->enabled() &&
             ancestor_body->settings().motion != schema::BodyMotion::static_body))
        {
            return false;
        }
    }
    const auto* transform = node.get_component<Transform>();
    JPH::Mat44 matrix;
    if (transform == nullptr || !world_matrix(*transform, matrix) ||
        !decompose(matrix, output.position, output.rotation, output.scale))
    {
        return false;
    }
    output.node_id = node.id();
    output.collider = collider.settings();
    if (body != nullptr)
    {
        output.rigid_body = body->settings();
    }
    else
    {
        output.rigid_body.motion = schema::BodyMotion::static_body;
    }
    return (output.rigid_body.motion != schema::BodyMotion::dynamic || node.parent() == nullptr) &&
           Collider::valid_settings(output.collider) &&
           RigidBody::valid_settings(output.rigid_body);
}

bool active_transform(const Node& node)
{
    for (const Node* current = &node; current != nullptr; current = current->parent())
    {
        const auto* transform = current->get_component<Transform>();
        if (transform == nullptr || !transform->enabled())
        {
            return false;
        }
    }
    return true;
}

bool write_transform(Node& node, JPH::Vec3Arg position, JPH::QuatArg rotation,
                     JPH::Vec3Arg world_scale)
{
    auto* transform = node.get_component<Transform>();
    if (transform == nullptr)
    {
        return false;
    }
    auto matrix =
        JPH::Mat44::sRotationTranslation(rotation, position) * JPH::Mat44::sScale(world_scale);
    if (const auto* parent = node.parent())
    {
        const auto* parent_transform = parent->get_component<Transform>();
        JPH::Mat44 parent_matrix;
        if (parent_transform == nullptr || !world_matrix(*parent_transform, parent_matrix))
        {
            return false;
        }
        matrix = parent_matrix.Inversed() * matrix;
    }
    JPH::Vec3 local_position;
    JPH::Quat local_rotation;
    JPH::Vec3 local_scale;
    if (!decompose(matrix, local_position, local_rotation, local_scale))
    {
        return false;
    }
    auto settings = transform->settings();
    settings.position = vector(local_position);
    settings.rotation = euler_angles(local_rotation);
    // 物理只写位置和旋转，保留调用方明确设置的局部缩放。
    return close(local_scale.GetX(), static_cast<float>(settings.scale.x())) &&
           close(local_scale.GetY(), static_cast<float>(settings.scale.y())) &&
           close(local_scale.GetZ(), static_cast<float>(settings.scale.z())) &&
           transform->set_settings(settings);
}

} // namespace

struct PhysicsSystem::State
{
    struct Entry
    {
        BodySnapshot snapshot;
        JPH::BodyID body;
    };

    BroadPhaseLayers layers;
    BroadPhaseFilter broad_filter;
    ObjectFilter object_filter;
    JPH::TempAllocatorMalloc allocator;
    JPH::JobSystemSingleThreaded jobs;
    JPH::PhysicsSystem physics;
    std::map<std::uint64_t, Entry> bodies;
    std::uint64_t scene_id = 0;
    double accumulated = 0.0;

    void clear() noexcept
    {
        auto& interface = physics.GetBodyInterface();
        for (const auto& [id, entry] : bodies)
        {
            (void)id;
            interface.RemoveBody(entry.body);
            interface.DestroyBody(entry.body);
        }
        bodies.clear();
        scene_id = 0;
        accumulated = 0.0;
    }

    bool synchronize(const std::vector<BodySnapshot>& snapshots)
    {
        auto& interface = physics.GetBodyInterface();
        std::set<std::uint64_t> retained;
        for (const auto& snapshot : snapshots)
        {
            retained.insert(snapshot.node_id);
        }
        for (auto iterator = bodies.begin(); iterator != bodies.end();)
        {
            if (!retained.contains(iterator->first))
            {
                interface.RemoveBody(iterator->second.body);
                interface.DestroyBody(iterator->second.body);
                iterator = bodies.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
        for (const auto& snapshot : snapshots)
        {
            auto found = bodies.find(snapshot.node_id);
            const bool rebuild = found == bodies.end() ||
                                 found->second.snapshot.collider != snapshot.collider ||
                                 found->second.snapshot.rigid_body != snapshot.rigid_body ||
                                 !found->second.snapshot.scale.IsClose(snapshot.scale, 1.0e-8F);
            if (rebuild)
            {
                if (found != bodies.end())
                {
                    interface.RemoveBody(found->second.body);
                    interface.DestroyBody(found->second.body);
                    bodies.erase(found);
                }
                const bool moving = snapshot.rigid_body.motion != schema::BodyMotion::static_body;
                JPH::BodyCreationSettings settings(
                    snapshot.shape.GetPtr(), JPH::RVec3(snapshot.position), snapshot.rotation,
                    motion_type(snapshot.rigid_body.motion), moving ? moving_layer : static_layer);
                settings.mIsSensor = snapshot.collider.is_trigger;
                settings.mFriction = snapshot.collider.friction;
                settings.mRestitution = snapshot.collider.restitution;
                settings.mLinearDamping = snapshot.rigid_body.linear_damping;
                settings.mAngularDamping = snapshot.rigid_body.angular_damping;
                settings.mGravityFactor = snapshot.rigid_body.gravity_factor;
                settings.mLinearVelocity = vector(snapshot.rigid_body.linear_velocity);
                settings.mAngularVelocity = vector(snapshot.rigid_body.angular_velocity);
                settings.mMaxLinearVelocity = 10000.0F;
                settings.mMaxAngularVelocity = 10000.0F;
                settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                settings.mMassPropertiesOverride.mMass = snapshot.rigid_body.mass;
                const auto body = interface.CreateAndAddBody(
                    settings, moving ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
                if (body.IsInvalid())
                {
                    return false;
                }
                bodies.emplace(snapshot.node_id, Entry{snapshot, body});
            }
            else
            {
                interface.SetPositionAndRotationWhenChanged(
                    found->second.body, JPH::RVec3(snapshot.position), snapshot.rotation,
                    JPH::EActivation::Activate);
                found->second.snapshot = snapshot;
            }
        }
        return true;
    }
};

PhysicsSystem::PhysicsSystem(WorldSystem* world) noexcept : world_(world)
{
}

PhysicsSystem::~PhysicsSystem() noexcept
{
    shutdown();
}

bool PhysicsSystem::on_initialize(const EngineConfig&)
{
    if (!acquire_jolt())
    {
        std::fputs("Cannot initialize Jolt: ABI mismatch or externally owned registration\n",
                   stderr);
        return false;
    }
    state_.reset(new (std::nothrow) State());
    if (!state_)
    {
        release_jolt();
        return false;
    }
    state_->jobs.Init(JPH::cMaxPhysicsJobs);
    state_->physics.Init(static_cast<JPH::uint>(maximum_bodies), 0, 65536, 16384, state_->layers,
                         state_->broad_filter, state_->object_filter);
    state_->physics.SetGravity(JPH::Vec3(0.0F, -9.81F, 0.0F));
    return true;
}

void PhysicsSystem::clear() noexcept
{
    if (state_)
    {
        state_->clear();
    }
}

std::size_t PhysicsSystem::body_count() const noexcept
{
    return state_ ? state_->bodies.size() : 0;
}

bool PhysicsSystem::on_tick(double delta_time)
{
    auto* scene = world_ != nullptr ? world_->scene() : nullptr;
    if (!state_)
    {
        return false;
    }
    if (scene == nullptr)
    {
        clear();
        return true;
    }
    if (state_->scene_id != scene->id())
    {
        clear();
        state_->scene_id = scene->id();
    }
    std::vector<BodySnapshot> snapshots;
    for (const auto& node : scene->nodes())
    {
        const auto* collider = node->get_component<Collider>();
        auto* rigid_body = node->get_component<RigidBody>();
        if (collider == nullptr || !collider->enabled() ||
            (rigid_body != nullptr && !rigid_body->enabled()) || !active_transform(*node))
        {
            if (rigid_body != nullptr)
            {
                rigid_body->pending_impulse_ = {};
            }
            continue;
        }
        BodySnapshot snapshot;
        if (!read_snapshot(*node, *collider, rigid_body, snapshot))
        {
            std::fputs("Physics rejects invalid settings, shear, reflection or a dynamic parent\n",
                       stderr);
            clear();
            return false;
        }
        const auto existing = state_->bodies.find(node->id());
        if (existing != state_->bodies.end() &&
            existing->second.snapshot.collider == snapshot.collider &&
            existing->second.snapshot.scale.IsClose(snapshot.scale, 1.0e-8F))
        {
            snapshot.shape = existing->second.snapshot.shape;
        }
        else if (!make_shape(snapshot.collider, snapshot.scale, snapshot.shape))
        {
            std::fputs("Physics cannot create collider with the requested world scale\n", stderr);
            clear();
            return false;
        }
        snapshots.push_back(std::move(snapshot));
    }
    if (snapshots.size() > maximum_bodies || !state_->synchronize(snapshots))
    {
        clear();
        return false;
    }
    auto& interface = state_->physics.GetBodyInterface();
    for (auto& [id, entry] : state_->bodies)
    {
        auto* node = scene->find_node(id);
        auto* body = node == nullptr ? nullptr : node->get_component<RigidBody>();
        if (body != nullptr && body->settings().motion == schema::BodyMotion::dynamic)
        {
            if (body->pending_impulse_ != Vec3{})
            {
                interface.AddImpulse(entry.body, vector(body->pending_impulse_));
                body->pending_impulse_ = {};
            }
        }
    }
    state_->accumulated = std::min(state_->accumulated + delta_time, fixed_step * maximum_steps);
    bool advanced = false;
    for (unsigned int step = 0; step < maximum_steps && state_->accumulated + 1.0e-12 >= fixed_step;
         ++step)
    {
        if (state_->physics.Update(static_cast<float>(fixed_step), 1, &state_->allocator,
                                   &state_->jobs) != JPH::EPhysicsUpdateError::None)
        {
            clear();
            return false;
        }
        state_->accumulated = std::max(0.0, state_->accumulated - fixed_step);
        advanced = true;
    }
    for (auto& [id, entry] : state_->bodies)
    {
        if (entry.snapshot.rigid_body.motion == schema::BodyMotion::static_body)
        {
            continue;
        }
        auto* node = scene->find_node(id);
        auto* body = node == nullptr ? nullptr : node->get_component<RigidBody>();
        if (body == nullptr)
        {
            clear();
            return false;
        }
        JPH::RVec3 position;
        JPH::Quat rotation;
        interface.GetPositionAndRotation(entry.body, position, rotation);
        if (advanced &&
            !write_transform(*node, JPH::Vec3(position), rotation, entry.snapshot.scale))
        {
            std::fputs("Cannot write physics pose through the node hierarchy\n", stderr);
            clear();
            return false;
        }
        auto settings = body->settings();
        settings.linear_velocity = vector(interface.GetLinearVelocity(entry.body));
        settings.angular_velocity = vector(interface.GetAngularVelocity(entry.body));
        if (!body->set_settings(settings))
        {
            clear();
            return false;
        }
        entry.snapshot.rigid_body = settings;
    }
    return true;
}

void PhysicsSystem::on_shutdown() noexcept
{
    if (state_)
    {
        clear();
        state_.reset();
        release_jolt();
    }
}

} // namespace engine
