#include "runtime/physics/physics_system.hpp"

#include "runtime/scene/character_motor_runtime.hpp"
#include "runtime/scene/collider_runtime.hpp"
#include "runtime/scene/rigid_body_runtime.hpp"

#include "runtime/performance/profiling.hpp"
#include "runtime/physics/mesh_collision_data.hpp"
#include "runtime/physics/physics_jobs.hpp"
#include "runtime/resource/resource.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/character_motor.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include "runtime/scene/rigid_body.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/world/world_system.hpp"

// Jolt 要求先包含基础定义，不能按文件名字母顺序重排。
// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/EstimateCollisionResponse.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/TransformedShape.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
// clang-format on

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <numbers>
#include <optional>
#include <span>
#include <tuple>
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
constexpr JPH::ObjectLayer character_layer = 2;

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
        return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(layer == static_layer ? 0 : 1));
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
        return object != static_layer || broad.GetValue() == moving_layer;
    }
};

class ObjectFilter final : public JPH::ObjectLayerPairFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer left, JPH::ObjectLayer right) const override
    {
        return (left != static_layer || right != static_layer) &&
               !(left == character_layer && right == character_layer);
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

void canonicalize_box_hull(std::span<const JPH::Vec3> points, JPH::Shape::ShapeResult& result)
{
    if (points.size() != 8)
    {
        return;
    }
    JPH::AABox bounds;
    for (const auto& point : points)
    {
        bounds.Encapsulate(point);
    }
    const auto half_extent = bounds.GetExtent();
    const auto center = bounds.GetCenter();
    if (half_extent.ReduceMin() <= 0.0F || center - half_extent != bounds.mMin ||
        center + half_extent != bounds.mMax)
    {
        return;
    }
    unsigned corners = 0;
    for (const auto& point : points)
    {
        unsigned corner = 0;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            if (point[axis] == bounds.mMax[axis])
            {
                corner |= 1U << axis;
            }
            else if (point[axis] != bounds.mMin[axis])
            {
                return;
            }
        }
        if ((corners & (1U << corner)) != 0)
        {
            return;
        }
        corners |= 1U << corner;
    }
    if (corners != 0xffU)
    {
        return;
    }
    // 调用方已成功构建官方凸包；沿用其实际圆角，不能套用普通 Box 的圆角策略。
    const auto* hull = static_cast<const JPH::ConvexHullShape*>(result.Get().GetPtr());
    JPH::BoxShapeSettings settings(half_extent, hull->GetConvexRadius(), hull->GetMaterial());
    settings.mDensity = hull->GetDensity();
    auto candidate = settings.Create();
    if (candidate.HasError() || !candidate.IsValid())
    {
        return;
    }
    if (center != JPH::Vec3::sZero())
    {
        candidate = JPH::RotatedTranslatedShapeSettings(center, JPH::Quat::sIdentity(),
                                                        candidate.Get().GetPtr())
                        .Create();
        if (candidate.HasError() || !candidate.IsValid())
        {
            return;
        }
    }
    ENGINE_PROFILE_ZONE("Physics.box_hull");
    result = std::move(candidate);
}

bool make_shape(const schema::ColliderDataT& collider, const schema::MeshDataT* mesh,
                JPH::Vec3Arg scale, JPH::RefConst<JPH::Shape>& output, bool mesh_part = false)
{
    ENGINE_PROFILE_ZONE("Physics.create_shape");
    output = nullptr;
    const auto center = vector(collider.center) * scale;
    if (!finite(center) || center.Abs().ReduceMax() > 100000.0F)
    {
        return false;
    }
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
    case schema::ColliderShape::compound:
    {
        if (!close(scale.GetX(), scale.GetY()) || !close(scale.GetX(), scale.GetZ()) ||
            scale.GetX() <= 0.0F || collider.hulls.empty())
        {
            return false;
        }
        // Jolt 5.4.0 官方凸包/不可变复合形状，MIT。StaticCompound 可用于动态刚体。
        JPH::StaticCompoundShapeSettings compound;
        for (const auto& hull : collider.hulls)
        {
            JPH::Array<JPH::Vec3> points;
            points.reserve(hull->vertices.size());
            for (const auto& vertex : hull->vertices)
            {
                const auto point = vector(vertex) * scale;
                if (!finite(point) || point.Abs().ReduceMax() > 100000.0F)
                {
                    return false;
                }
                points.push_back(point);
            }
            result = JPH::ConvexHullShapeSettings(points).Create();
            if (result.HasError() || !result.IsValid() ||
                !std::isfinite(result.Get()->GetVolume()) || result.Get()->GetVolume() <= 0.0F)
            {
                return false;
            }
            canonicalize_box_hull(std::span(points.data(), points.size()), result);
            compound.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), result.Get().GetPtr());
        }
        // 单个 hull 已完整表达其局部坐标，无需再套一个复合形状。
        if (collider.hulls.size() > 1)
        {
            result = compound.Create();
        }
        break;
    }
    case schema::ColliderShape::mesh:
    {
        if (mesh == nullptr || mesh->vertices.empty() || mesh->indices.empty() ||
            mesh->indices.size() % 3 != 0 ||
            mesh->vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
            mesh->indices.size() / 3 > std::numeric_limits<std::uint32_t>::max())
        {
            return false;
        }
        JPH::VertexList vertices;
        vertices.reserve(mesh->vertices.size());
        for (const auto& vertex : mesh->vertices)
        {
            const JPH::Vec3 position = JPH::Vec3(vertex.x(), vertex.y(), vertex.z()) * scale;
            if (!finite(position) || position.Abs().ReduceMax() > 100000.0F)
            {
                return false;
            }
            vertices.emplace_back(position.GetX(), position.GetY(), position.GetZ());
        }
        JPH::IndexedTriangleList triangles;
        triangles.reserve(mesh->indices.size() / 3);
        for (std::size_t index = 0; index < mesh->indices.size(); index += 3)
        {
            const auto a = mesh->indices[index];
            const auto b = mesh->indices[index + 1];
            const auto c = mesh->indices[index + 2];
            if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size())
            {
                return false;
            }
            triangles.emplace_back(a, b, c, 0U);
        }
        // Jolt 清理退化三角形并建立三角形树；保留源 Mesh 的逆时针绕序。
        JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
        if (mesh_part)
        {
            settings.mBuildQuality = JPH::MeshShapeSettings::EBuildQuality::FavorBuildSpeed;
        }
        if (settings.mIndexedTriangles.empty())
        {
            // 细分后某叶可能只含被 Jolt 清理掉的退化三角；整网格原来也会丢弃它们。
            // 分段输入保留空形状槽，不能因此拒绝其余有效几何。
            return mesh_part;
        }
        result = settings.Create();
        break;
    }
    default:
        return false;
    }
    if (result.HasError() || !result.IsValid())
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

// CharacterVirtual 已经排除自身 inner body；这里也排除其他角色的 inner body，
// 角色互撞统一交给 CharacterVsCharacterCollision，sensor 仍进入官方接触监听器。
class CharacterBodyFilter final : public JPH::BodyFilter
{
public:
    bool ShouldCollideLocked(const JPH::Body& body) const override
    {
        return body.GetObjectLayer() != character_layer;
    }
};

class TargetBodyFilter final : public JPH::BodyFilter
{
public:
    explicit TargetBodyFilter(JPH::BodyID body) noexcept : body_(body)
    {
    }

    bool ShouldCollide(const JPH::BodyID& body) const override
    {
        return body == body_;
    }

    bool ShouldCollideLocked(const JPH::Body& body) const override
    {
        return body.GetID() == body_ && !body.IsSensor();
    }

private:
    JPH::BodyID body_;
};

struct CharacterSnapshot
{
    std::uint64_t scene_id = 0;
    std::uint64_t node_id = 0;
    std::uint64_t instance_id = 0;
    schema::CharacterMotorDataT settings;
    JPH::Vec3 position = JPH::Vec3::sZero();
    bool reset_velocity = false;
};

bool read_character(Node node, CharacterMotor motor, CharacterSnapshot& output)
{
    auto transform = node.get_component<Transform>();
    auto collider = node.get_component<Collider>();
    auto rigid = node.get_component<RigidBody>();
    if (node.parent().valid() || !transform.valid() || (collider.valid() && collider.active()) ||
        (rigid.valid() && rigid.active()) || !CharacterMotor::valid_settings(motor.settings()))
    {
        return false;
    }
    const auto& pose = transform.settings();
    if (pose.scale != Vec3(1, 1, 1) || pose.rotation.x() != 0.0 || pose.rotation.z() != 0.0 ||
        !finite(vector(pose.position)) || vector(pose.position).Abs().ReduceMax() > 10000000.0F)
    {
        return false;
    }
    output.scene_id = node.scene()->id();
    output.node_id = node.id();
    output.settings = motor.settings();
    output.position = vector(pose.position);
    return true;
}

using MeshPartShapes = std::vector<JPH::RefConst<JPH::Shape>>;

struct BodySnapshot
{
    std::uint64_t scene_id = 0;
    std::uint64_t node_id = 0;
    std::uint64_t collider_id = 0;
    std::shared_ptr<const schema::ColliderDataT> collider;
    schema::RigidBodyDataT rigid_body;
    JPH::Vec3 position = JPH::Vec3::sZero();
    JPH::Quat rotation = JPH::Quat::sIdentity();
    JPH::Vec3 scale = JPH::Vec3::sReplicate(1.0F);
    // scale 是本帧请求；shape_scale 是实际烘入形状的值，复用不能推进此基准。
    // 分段叶子共享同一烘焙缩放，容差内的局部重建也沿用该值，避免混合缩放。
    JPH::Vec3 shape_scale = JPH::Vec3::sReplicate(1.0F);
    // 只用于根动态刚体：调用方未改写展示 TRS 时，保留 Jolt 原始姿态。
    // 不能把自身 Euler 回写的舍入或 q/-q 翻号当成下一帧的 teleport。
    std::optional<TransformSettings> written_transform;
    ResourcePtr mesh;
    std::shared_ptr<const schema::MeshDataT> transient_mesh;
    std::shared_ptr<const MeshCollisionData> mesh_parts;
    std::shared_ptr<const MeshPartShapes> part_shapes;
    std::shared_ptr<const MeshPartShapes> part_groups;
    JPH::RefConst<JPH::Shape> shape;
};

bool read_snapshot(Node node, const schema::ColliderDataT& collider, const schema::MeshDataT* mesh,
                   RigidBody body, BodySnapshot& output, const BodySnapshot* previous = nullptr)
{
    auto component = node.get_component<Collider>();
    if (!component.valid())
    {
        return false;
    }
    const bool same_instance =
        previous != nullptr && previous->collider_id == component.instance_id();
    for (Node ancestor = node; ancestor; ancestor = ancestor.parent())
    {
        auto ancestor_transform = ancestor.get_component<Transform>();
        auto ancestor_body = ancestor.get_component<RigidBody>();
        if (!ancestor_transform.valid() || ancestor_transform.settings().scale.x() <= 0.0 ||
            ancestor_transform.settings().scale.y() <= 0.0 ||
            ancestor_transform.settings().scale.z() <= 0.0 ||
            (ancestor != node && ancestor_body.valid() && ancestor_body.active() &&
             ancestor_body.settings().motion != schema::BodyMotion::static_body))
        {
            return false;
        }
    }
    auto transform = node.get_component<Transform>();
    if (!transform.valid())
    {
        return false;
    }
    const bool unchanged_output = same_instance && previous->written_transform &&
                                  node.parent().valid() == false && body.valid() &&
                                  body.settings().motion == schema::BodyMotion::dynamic &&
                                  transform.settings() == *previous->written_transform;
    if (unchanged_output)
    {
        output.position = previous->position;
        output.rotation = previous->rotation;
        output.scale = previous->scale;
        output.written_transform = previous->written_transform;
    }
    else
    {
        JPH::Mat44 matrix;
        if (!world_matrix(transform, matrix) ||
            !decompose(matrix, output.position, output.rotation, output.scale))
        {
            return false;
        }
    }
    output.scene_id = node.scene()->id();
    output.node_id = node.id();
    output.collider_id = component.instance_id();
    // 比较完整描述，不能用组件地址判断：FlatBuffers 的嵌套凸包仍可能被修改。
    const bool same_collider =
        same_instance && previous->collider && *previous->collider == collider;
    const bool previously_valid = same_collider && previous->mesh == output.mesh &&
                                  previous->transient_mesh == output.transient_mesh &&
                                  previous->mesh_parts == output.mesh_parts;
    if (body.valid())
    {
        output.rigid_body = body.settings();
    }
    else
    {
        output.rigid_body.motion = schema::BodyMotion::static_body;
    }
    if ((output.rigid_body.motion == schema::BodyMotion::dynamic && node.parent().valid()) ||
        (collider.shape == schema::ColliderShape::mesh &&
         output.rigid_body.motion != schema::BodyMotion::static_body) ||
        (!previously_valid && !Collider::valid_settings(collider, mesh, output.mesh_parts.get())) ||
        !RigidBody::valid_settings(output.rigid_body))
    {
        return false;
    }
    output.collider = same_collider ? previous->collider
                                    : std::make_shared<const schema::ColliderDataT>(collider);
    return true;
}

bool make_mesh_compound(std::span<const JPH::RefConst<JPH::Shape>> shapes,
                        JPH::RefConst<JPH::Shape>& output)
{
    JPH::StaticCompoundShapeSettings compound;
    std::size_t count = 0;
    output = nullptr;
    for (const auto& shape : shapes)
    {
        if (shape != nullptr)
        {
            compound.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), shape.GetPtr());
            output = shape;
            ++count;
        }
    }
    if (count > 1)
    {
        ENGINE_PROFILE_ZONE("Physics.create_mesh_compound");
        const auto result = compound.Create();
        if (result.HasError() || !result.IsValid())
        {
            output = nullptr;
            return false;
        }
        output = result.Get();
    }
    return true;
}

bool prepare_mesh_parts(BodySnapshot& snapshot, const BodySnapshot* previous)
{
    ENGINE_PROFILE_ZONE("Physics.prepare_mesh_parts");
    const bool reuse = previous != nullptr && previous->mesh_parts && previous->part_shapes &&
                       previous->part_groups &&
                       previous->shape_scale.IsClose(snapshot.scale, 1.0e-8F) &&
                       previous->collider->center == snapshot.collider->center;
    snapshot.shape_scale = reuse ? previous->shape_scale : snapshot.scale;
    if (reuse && previous->mesh_parts == snapshot.mesh_parts)
    {
        snapshot.part_shapes = previous->part_shapes;
        snapshot.part_groups = previous->part_groups;
        snapshot.shape = previous->shape;
        return true;
    }
    auto shapes = std::make_shared<MeshPartShapes>(snapshot.mesh_parts->parts.size());
    for (std::size_t slot = 0; slot < shapes->size(); ++slot)
    {
        const auto& mesh = snapshot.mesh_parts->parts[slot];
        if (!mesh)
        {
            continue;
        }
        auto& shape = (*shapes)[slot];
        if (reuse && slot < previous->mesh_parts->parts.size() &&
            previous->mesh_parts->parts[slot] == mesh)
        {
            shape = (*previous->part_shapes)[slot];
        }
        else if (mesh->schema_version != 1 ||
                 !make_shape(*snapshot.collider, mesh.get(), snapshot.shape_scale, shape, true))
        {
            return false;
        }
    }
    // 连续槽位分组复用官方复合树；核心不理解地形尺寸或体素坐标。
    constexpr std::size_t group_size = 64;
    auto groups = std::make_shared<MeshPartShapes>((shapes->size() + group_size - 1) / group_size);
    const std::span<const JPH::RefConst<JPH::Shape>> leaves(*shapes);
    for (std::size_t group = 0; group < groups->size(); ++group)
    {
        const auto first = group * group_size;
        const auto last = std::min(first + group_size, shapes->size());
        const bool same_group =
            reuse && group < previous->part_groups->size() &&
            std::min(first + group_size, previous->part_shapes->size()) == last &&
            std::equal(shapes->begin() + first, shapes->begin() + last,
                       previous->part_shapes->begin() + first);
        if (same_group)
        {
            (*groups)[group] = (*previous->part_groups)[group];
        }
        else if (!make_mesh_compound(leaves.subspan(first, last - first), (*groups)[group]))
        {
            return false;
        }
    }
    if (reuse && *groups == *previous->part_groups)
    {
        snapshot.shape = previous->shape;
        snapshot.part_groups = previous->part_groups;
    }
    else
    {
        if (!make_mesh_compound(*groups, snapshot.shape))
        {
            return false;
        }
        snapshot.part_groups = std::move(groups);
    }
    snapshot.part_shapes = std::move(shapes);
    return true;
}

JPH::AABox part_bounds(const BodySnapshot& snapshot, const JPH::Shape& shape)
{
    // Shape 的包围盒以质心为原点；顶点缩放及 Collider.center 已烘入子形状。
    const auto transform = JPH::Mat44::sRotationTranslation(snapshot.rotation, snapshot.position)
                               .PreTranslated(shape.GetCenterOfMass());
    return shape.GetWorldSpaceBounds(transform, JPH::Vec3::sOne());
}

JPH::AABox changed_bounds(const BodySnapshot& before, const BodySnapshot& after)
{
    JPH::AABox bounds;
    const auto size = std::max(before.part_shapes->size(), after.part_shapes->size());
    for (std::size_t slot = 0; slot < size; ++slot)
    {
        const auto* old_shape =
            slot < before.part_shapes->size() ? (*before.part_shapes)[slot].GetPtr() : nullptr;
        const auto* new_shape =
            slot < after.part_shapes->size() ? (*after.part_shapes)[slot].GetPtr() : nullptr;
        if (old_shape == new_shape)
        {
            continue;
        }
        if (old_shape != nullptr)
        {
            bounds.Encapsulate(part_bounds(before, *old_shape));
        }
        if (new_shape != nullptr)
        {
            bounds.Encapsulate(part_bounds(after, *new_shape));
        }
    }
    return bounds;
}

bool active_transform(Node node)
{
    for (Node current = node; current; current = current.parent())
    {
        auto transform = current.get_component<Transform>();
        if (!transform.valid() || !transform.active())
        {
            return false;
        }
    }
    return true;
}

bool active_collision(Node node)
{
    auto collider = node.get_component<Collider>();
    auto body = node.get_component<RigidBody>();
    return collider.valid() && collider.active() && !collider.settings().is_trigger &&
           (!body.valid() || body.active()) && active_transform(node);
}

bool active_query_collision(Node node, std::uint64_t collider_id)
{
    auto collider = node.get_component<Collider>();
    auto body = node.get_component<RigidBody>();
    return collider.valid() && collider.instance_id() == collider_id && collider.active() &&
           (!body.valid() || body.active()) && active_transform(node);
}

bool active_physics_object(const WorldSystem& world, const PhysicsObject& object)
{
    if (object.instance_id == 0)
    {
        return false;
    }
    auto node = world.find_node(object.node_id);
    if (!node.valid() || node.scene()->id() != object.scene_id)
    {
        return false;
    }
    if (object.kind == PhysicsObjectKind::collider)
    {
        return active_query_collision(node, object.instance_id);
    }
    if (object.kind != PhysicsObjectKind::character)
    {
        return false;
    }
    auto motor = node.get_component<CharacterMotor>();
    auto collider = node.get_component<Collider>();
    auto body = node.get_component<RigidBody>();
    return motor.valid() && motor.instance_id() == object.instance_id && motor.active() &&
           active_transform(node) && (!collider.valid() || !collider.active()) &&
           (!body.valid() || !body.active());
}

bool object_less(const PhysicsObject& left, const PhysicsObject& right)
{
    return std::tie(left.scene_id, left.node_id, left.kind, left.instance_id) <
           std::tie(right.scene_id, right.node_id, right.kind, right.instance_id);
}

bool same_object(const PhysicsObject& left, const PhysicsObject& right)
{
    return !object_less(left, right) && !object_less(right, left);
}

struct ContactPair
{
    PhysicsObject a;
    PhysicsObject b;

    bool operator<(const ContactPair& other) const
    {
        return object_less(a, other.a) || (same_object(a, other.a) && object_less(b, other.b));
    }
};

struct ContactSample
{
    ContactPair pair;
    Vec3 point;
    Vec3 normal;
    bool is_trigger = false;
};

ContactSample contact_sample(const PhysicsObject& a, const PhysicsObject& b, JPH::RVec3Arg point,
                             JPH::Vec3Arg normal, bool is_trigger)
{
    const bool reverse = object_less(b, a);
    return {{reverse ? b : a, reverse ? a : b},
            vector(JPH::Vec3(point)),
            vector(reverse ? -normal : normal),
            is_trigger};
}

bool valid_overlap(const WorldSystem* world, JPH::Vec3Arg center, const PhysicsQueryFilter& filter)
{
    return world != nullptr && world->is_initialized() && finite(center) &&
           center.Abs().ReduceMax() <= 10000000.0F &&
           (filter.ignore_node_id == 0 || world->find_node(filter.ignore_node_id).valid());
}

bool valid_query(const WorldSystem* world, JPH::Vec3Arg position, JPH::Vec3Arg direction,
                 const PhysicsQueryFilter& filter)
{
    return world != nullptr && world->is_initialized() && finite(position) && finite(direction) &&
           position.Abs().ReduceMax() <= 10000000.0F && direction.Abs().ReduceMax() <= 100000.0F &&
           direction.LengthSq() > 0.0F &&
           (filter.ignore_node_id == 0 || world->find_node(filter.ignore_node_id).valid());
}

JPH::Vec3 query_normal(JPH::Vec3Arg normal, JPH::Vec3Arg direction)
{
    return finite(normal) && normal.LengthSq() > 0.0F ? normal.Normalized()
                                                      : -direction.Normalized();
}

bool write_transform(Node node, JPH::Vec3Arg position, JPH::QuatArg rotation,
                     JPH::Vec3Arg world_scale)
{
    auto transform = node.get_component<Transform>();
    if (!transform.valid())
    {
        return false;
    }
    auto matrix =
        JPH::Mat44::sRotationTranslation(rotation, position) * JPH::Mat44::sScale(world_scale);
    if (auto parent = node.parent())
    {
        auto parent_transform = parent.get_component<Transform>();
        JPH::Mat44 parent_matrix;
        if (!parent_transform.valid() || !world_matrix(parent_transform, parent_matrix))
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
    auto settings = transform.settings();
    settings.position = vector(local_position);
    settings.rotation = euler_angles(local_rotation);
    // 物理只写位置和旋转，保留调用方明确设置的局部缩放。
    return close(local_scale.GetX(), static_cast<float>(settings.scale.x())) &&
           close(local_scale.GetY(), static_cast<float>(settings.scale.y())) &&
           close(local_scale.GetZ(), static_cast<float>(settings.scale.z())) &&
           transform.set_settings(settings);
}

} // namespace

struct PhysicsSystem::State final : JPH::ContactListener, JPH::CharacterContactListener
{
    // Queries belong to ECS entities, so world shutdown may safely precede this system.
    const ecs_query_t* nodes = nullptr;
    const ecs_query_t* motors = nullptr;
    WorldSystem* query_world = nullptr;
    std::uint64_t query_epoch = 0;

    void release_queries() noexcept
    {
        if (query_world && query_world->native_world() && query_world->epoch() == query_epoch)
        {
            if (nodes)
            {
                ecs_query_fini(const_cast<ecs_query_t*>(nodes));
            }
            if (motors)
            {
                ecs_query_fini(const_cast<ecs_query_t*>(motors));
            }
        }
        nodes = nullptr;
        motors = nullptr;
        query_world = nullptr;
        query_epoch = 0;
    }

    void bind_queries(WorldSystem& world)
    {
        if (!world.is_initialized() || (nodes && query_epoch == world.epoch()))
        {
            return;
        }
        release_queries();
        query_world = &world;
        query_epoch = world.epoch();
        flecs::world ecs(world.native_world());
        nodes = ecs.query_builder<>(ecs.entity())
                    .with<ecs_detail::Published>()
                    .with<ecs_detail::ComponentStorage<Collider::Data>>()
                    .oper(flecs::Or)
                    .with<ecs_detail::ComponentStorage<CharacterMotor::Data>>()
                    .build()
                    .c_ptr();
        motors = ecs.query_builder<CharacterMotorRuntime>(ecs.entity())
                     .with<ecs_detail::Published>()
                     .build()
                     .c_ptr();
    }

    struct Entry
    {
        BodySnapshot snapshot;
        JPH::BodyID body;
        std::uint64_t seen_frame = 0;
    };

    struct CharacterEntry
    {
        CharacterSnapshot snapshot;
        JPH::Ref<JPH::CharacterVirtual> character;
        JPH::Vec3 external_velocity = JPH::Vec3::sZero();
        std::uint64_t seen_frame = 0;
    };

    BroadPhaseLayers layers;
    BroadPhaseFilter broad_filter;
    ObjectFilter object_filter;
    // 官方临时栈分配器，超过预留空间仍用官方 malloc fallback，不限制场景规模。
    JPH::TempAllocatorImplWithMallocFallback allocator{10U * 1024U * 1024U};
    physics_detail::PhysicsJobs jobs;
    JPH::PhysicsSystem physics;
    std::map<std::uint64_t, Entry> bodies;
    JPH::CharacterVsCharacterCollisionSimple character_collisions;
    std::map<std::uint64_t, CharacterEntry> characters;
    std::vector<BodySnapshot> body_snapshots;
    std::vector<CharacterSnapshot> character_snapshots;
    std::vector<std::uint64_t> synchronized_scenes;
    std::uint64_t frame = 0;
    std::uint64_t step = 0;
    double accumulated = 0.0;
    std::mutex impact_mutex;
    std::map<ContactPair, PhysicsImpact> pending_impacts;
    std::vector<PhysicsImpact> impacts;
    // BodyID 包含 Jolt 代际；公开身份另外保留组件类型及实例代际。
    std::map<JPH::BodyID, PhysicsObject> body_objects;
    std::map<JPH::CharacterID, PhysicsObject> character_objects;
    std::mutex contact_mutex;
    std::map<JPH::SubShapeIDPair, ContactSample> body_contacts;
    using CharacterContactKey = std::tuple<std::uint32_t, bool, std::uint32_t, std::uint32_t>;
    std::map<CharacterContactKey, ContactSample> character_contacts;
    std::map<ContactPair, ContactSample> previous_contacts;
    std::map<ContactPair, ContactSample> touched_contacts;
    std::vector<PhysicsContactEvent> contacts;

    ~State() override
    {
        // Member destruction releases the Jolt world before its job allocator.
        jobs.wait_for_wrappers();
        release_queries();
    }

    bool can_query(const WorldSystem* world) const noexcept
    {
        return world != nullptr &&
               std::any_of(world->scenes().begin(), world->scenes().end(),
                           [&](const auto& scene)
                           {
                               return std::find(synchronized_scenes.begin(),
                                                synchronized_scenes.end(),
                                                scene->id()) != synchronized_scenes.end();
                           });
    }

    void mark_synchronized(std::uint64_t scene)
    {
        if (std::find(synchronized_scenes.begin(), synchronized_scenes.end(), scene) ==
            synchronized_scenes.end())
        {
            synchronized_scenes.push_back(scene);
        }
    }

    class QueryBodyFilter final : public JPH::BodyFilter
    {
    public:
        QueryBodyFilter(const State& state, const WorldSystem& world,
                        const PhysicsQueryFilter& filter) noexcept
            : state_(state), world_(world), filter_(filter)
        {
        }

        bool ShouldCollideLocked(const JPH::Body& body) const override
        {
            const auto found = state_.body_objects.find(body.GetID());
            auto node = world_.find_node(body.GetUserData());
            if (found == state_.body_objects.end() || !node.valid() ||
                !active_physics_object(world_, found->second))
            {
                return false;
            }
            if (found->second.kind == PhysicsObjectKind::character)
            {
                if (!filter_.include_characters)
                {
                    return false;
                }
            }
            else if ((!filter_.include_sensors && body.IsSensor()) ||
                     (body.IsStatic() && !filter_.include_static) ||
                     (body.IsKinematic() && !filter_.include_kinematic) ||
                     (body.IsDynamic() && !filter_.include_dynamic))
            {
                return false;
            }
            if (filter_.ignore_node_id != 0)
            {
                for (Node current = node; current; current = current.parent())
                {
                    if (current.id() == filter_.ignore_node_id)
                    {
                        return false;
                    }
                    if (!filter_.ignore_descendants)
                    {
                        break;
                    }
                }
            }
            return true;
        }

    private:
        const State& state_;
        const WorldSystem& world_;
        const PhysicsQueryFilter& filter_;
    };

    bool query_hit(const WorldSystem& world, const JPH::Body& body, float fraction,
                   JPH::Vec3Arg direction, JPH::RVec3Arg point, JPH::Vec3Arg normal,
                   PhysicsQueryHit& output) const
    {
        const auto found = body_objects.find(body.GetID());
        if (found == body_objects.end() || !active_physics_object(world, found->second) ||
            !std::isfinite(fraction) || !finite(JPH::Vec3(point)))
        {
            return false;
        }
        output.hit = true;
        output.object = found->second;
        output.point = {point.GetX(), point.GetY(), point.GetZ()};
        output.normal = vector(query_normal(normal, direction));
        output.fraction = std::clamp(fraction, 0.0F, 1.0F);
        output.distance = direction.Length() * output.fraction;
        return true;
    }

    bool overlap(const WorldSystem& world, const JPH::Shape& shape, JPH::RVec3Arg center,
                 const PhysicsQueryFilter& filter, std::vector<PhysicsObject>& objects) const
    {
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        JPH::CollideShapeSettings settings;
        settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
        physics.GetNarrowPhaseQuery().CollideShape(
            &shape, JPH::Vec3::sOne(), JPH::RMat44::sTranslation(center), settings, center,
            collector, physics.GetDefaultBroadPhaseLayerFilter(moving_layer),
            physics.GetDefaultLayerFilter(moving_layer), QueryBodyFilter{*this, world, filter});
        for (const auto& hit : collector.mHits)
        {
            const auto found = body_objects.find(hit.mBodyID2);
            if (found == body_objects.end())
            {
                objects.clear();
                return false;
            }
            objects.push_back(found->second);
        }
        std::sort(objects.begin(), objects.end(), object_less);
        objects.erase(std::unique(objects.begin(), objects.end(), same_object), objects.end());
        return true;
    }

    void collect_impact(const JPH::Body& first, const JPH::Body& second,
                        const JPH::ContactManifold& manifold, const JPH::ContactSettings& settings)
    {
        ENGINE_PROFILE_ZONE("Physics.collect_impact");
        const auto first_node = first.GetUserData();
        const auto second_node = second.GetUserData();
        if (settings.mIsSensor || first.IsSensor() || second.IsSensor() ||
            first.GetObjectLayer() == character_layer ||
            second.GetObjectLayer() == character_layer || first_node == 0 || second_node == 0 ||
            first_node == second_node || manifold.mRelativeContactPointsOn1.empty() ||
            manifold.mRelativeContactPointsOn1.size() !=
                manifold.mRelativeContactPointsOn2.size() ||
            !finite(manifold.mWorldSpaceNormal))
        {
            return;
        }
        float closing_speed = 0;
        JPH::RVec3 point = JPH::RVec3::sZero();
        for (JPH::uint index = 0; index < manifold.mRelativeContactPointsOn1.size(); ++index)
        {
            const auto a = manifold.GetWorldSpaceContactPointOn1(index);
            const auto b = manifold.GetWorldSpaceContactPointOn2(index);
            const float speed = (first.GetPointVelocity(a) - second.GetPointVelocity(b))
                                    .Dot(manifold.mWorldSpaceNormal);
            if (std::isfinite(speed) && speed > closing_speed)
            {
                closing_speed = speed;
                point = (a + b) * 0.5F;
            }
        }
        if (closing_speed <= 0 || !finite(JPH::Vec3(point)))
        {
            return;
        }
        // Jolt 5.4.0 官方碰撞估算，MIT；回调在求解前，不能称为实际求解冲量。
        // 这里只读传入 body，不调用带锁 BodyInterface，不访问场景或执行业务。
        JPH::CollisionEstimationResult estimate;
        const auto& configuration = physics.GetPhysicsSettings();
        {
            ENGINE_PROFILE_ZONE("Physics.estimate_impact");
            JPH::EstimateCollisionResponse(
                first, second, manifold, estimate, settings.mCombinedFriction,
                settings.mCombinedRestitution, configuration.mMinVelocityForRestitution,
                configuration.mNumVelocitySteps);
        }
        float impulse = 0;
        for (const auto item : estimate.mContactImpulse)
        {
            impulse += std::max(0.0F, item);
        }
        if (!std::isfinite(impulse) || impulse <= 0)
        {
            return;
        }
        const auto first_object = body_objects.find(first.GetID());
        const auto second_object = body_objects.find(second.GetID());
        if (first_object == body_objects.end() || second_object == body_objects.end())
        {
            return;
        }
        const bool reverse = object_less(second_object->second, first_object->second);
        PhysicsImpact impact;
        impact.frame = frame;
        impact.a = reverse ? second_object->second : first_object->second;
        impact.b = reverse ? first_object->second : second_object->second;
        impact.point = {point.GetX(), point.GetY(), point.GetZ()};
        impact.normal = vector(reverse ? -manifold.mWorldSpaceNormal : manifold.mWorldSpaceNormal);
        impact.closing_speed = closing_speed;
        impact.estimated_impulse = impulse;
        const std::lock_guard lock(impact_mutex);
        auto [entry, inserted] =
            pending_impacts.try_emplace(ContactPair{impact.a, impact.b}, impact);
        if (!inserted && (impact.estimated_impulse > entry->second.estimated_impulse ||
                          (impact.estimated_impulse == entry->second.estimated_impulse &&
                           impact.closing_speed > entry->second.closing_speed)))
        {
            entry->second = impact;
        }
    }

    void OnContactAdded(const JPH::Body& first, const JPH::Body& second,
                        const JPH::ContactManifold& manifold,
                        JPH::ContactSettings& settings) override
    {
        collect_contact(first, second, manifold, settings);
        collect_impact(first, second, manifold, settings);
    }

    void OnContactPersisted(const JPH::Body& first, const JPH::Body& second,
                            const JPH::ContactManifold& manifold,
                            JPH::ContactSettings& settings) override
    {
        collect_contact(first, second, manifold, settings);
        collect_impact(first, second, manifold, settings);
    }

    void collect_contact(const JPH::Body& first, const JPH::Body& second,
                         const JPH::ContactManifold& manifold, const JPH::ContactSettings& settings)
    {
        const auto a = body_objects.find(first.GetID());
        const auto b = body_objects.find(second.GetID());
        if (a == body_objects.end() || b == body_objects.end() ||
            manifold.mRelativeContactPointsOn1.empty() ||
            manifold.mRelativeContactPointsOn2.empty() || !finite(manifold.mWorldSpaceNormal))
        {
            return;
        }
        const auto point =
            (manifold.GetWorldSpaceContactPointOn1(0) + manifold.GetWorldSpaceContactPointOn2(0)) *
            0.5F;
        const auto sample = contact_sample(a->second, b->second, point, manifold.mWorldSpaceNormal,
                                           settings.mIsSensor);
        const JPH::SubShapeIDPair key(first.GetID(), manifold.mSubShapeID1, second.GetID(),
                                      manifold.mSubShapeID2);
        const std::lock_guard lock(contact_mutex);
        body_contacts.insert_or_assign(key, sample);
        touched_contacts.insert_or_assign(sample.pair, sample);
    }

    void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
    {
        // Removed 回调时物体可能已经销毁；只处理此前缓存的数值与身份。
        const std::lock_guard lock(contact_mutex);
        body_contacts.erase(pair);
    }

    void collect_character_contact(const JPH::CharacterVirtual* character, const JPH::BodyID& body,
                                   const JPH::SubShapeID& subshape, JPH::RVec3Arg point,
                                   JPH::Vec3Arg normal)
    {
        const auto a = character_objects.find(character->GetID());
        const auto b = body_objects.find(body);
        if (a == character_objects.end() || b == body_objects.end())
        {
            return;
        }
        const auto entry = bodies.find(b->second.node_id);
        const bool sensor = entry != bodies.end() && entry->second.snapshot.collider->is_trigger;
        const auto sample = contact_sample(a->second, b->second, point, normal, sensor);
        const CharacterContactKey key{character->GetID().GetValue(), false,
                                      body.GetIndexAndSequenceNumber(), subshape.GetValue()};
        const std::lock_guard lock(contact_mutex);
        character_contacts.insert_or_assign(key, sample);
        touched_contacts.insert_or_assign(sample.pair, sample);
    }

    void collect_character_contact(const JPH::CharacterVirtual* character,
                                   const JPH::CharacterVirtual* other,
                                   const JPH::SubShapeID& subshape, JPH::RVec3Arg point,
                                   JPH::Vec3Arg normal)
    {
        const auto a = character_objects.find(character->GetID());
        const auto b = character_objects.find(other->GetID());
        if (a == character_objects.end() || b == character_objects.end())
        {
            return;
        }
        const auto sample = contact_sample(a->second, b->second, point, normal, false);
        const CharacterContactKey key{character->GetID().GetValue(), true,
                                      other->GetID().GetValue(), subshape.GetValue()};
        const std::lock_guard lock(contact_mutex);
        character_contacts.insert_or_assign(key, sample);
        touched_contacts.insert_or_assign(sample.pair, sample);
    }

    void OnContactAdded(const JPH::CharacterVirtual* character,
                        const JPH::CharacterContact& contact,
                        JPH::CharacterContactSettings&) override
    {
        // Jolt 5.6 stores the normal towards the character; our event points to the other body.
        collect_character_contact(character, contact.mBodyB, contact.mSubShapeIDB,
                                  contact.mPosition, -contact.mContactNormal);
    }

    void OnContactPersisted(const JPH::CharacterVirtual* character,
                            const JPH::CharacterContact& contact,
                            JPH::CharacterContactSettings&) override
    {
        collect_character_contact(character, contact.mBodyB, contact.mSubShapeIDB,
                                  contact.mPosition, -contact.mContactNormal);
    }

    void OnContactRemoved(const JPH::CharacterVirtual* character, const JPH::BodyID& body,
                          const JPH::SubShapeID& subshape) override
    {
        const std::lock_guard lock(contact_mutex);
        character_contacts.erase({character->GetID().GetValue(), false,
                                  body.GetIndexAndSequenceNumber(), subshape.GetValue()});
    }

    void OnCharacterContactAdded(const JPH::CharacterVirtual* character,
                                 const JPH::CharacterContact& contact,
                                 JPH::CharacterContactSettings&) override
    {
        collect_character_contact(character, contact.mCharacterB, contact.mSubShapeIDB,
                                  contact.mPosition, -contact.mContactNormal);
    }

    void OnCharacterContactPersisted(const JPH::CharacterVirtual* character,
                                     const JPH::CharacterContact& contact,
                                     JPH::CharacterContactSettings&) override
    {
        collect_character_contact(character, contact.mCharacterB, contact.mSubShapeIDB,
                                  contact.mPosition, -contact.mContactNormal);
    }

    void OnCharacterContactRemoved(const JPH::CharacterVirtual* character,
                                   const JPH::CharacterID& other,
                                   const JPH::SubShapeID& subshape) override
    {
        const std::lock_guard lock(contact_mutex);
        character_contacts.erase(
            {character->GetID().GetValue(), true, other.GetValue(), subshape.GetValue()});
    }

    JPH::BodyID object_body(const PhysicsObject& object) const
    {
        if (object.kind == PhysicsObjectKind::collider)
        {
            const auto found = bodies.find(object.node_id);
            if (found != bodies.end() && found->second.snapshot.collider_id == object.instance_id &&
                found->second.snapshot.scene_id == object.scene_id)
            {
                return found->second.body;
            }
        }
        else if (object.kind == PhysicsObjectKind::character)
        {
            const auto found = characters.find(object.node_id);
            if (found != characters.end() &&
                found->second.snapshot.instance_id == object.instance_id &&
                found->second.snapshot.scene_id == object.scene_id)
            {
                return found->second.character->GetInnerBodyID();
            }
        }
        return {};
    }

    void append_contact(const ContactSample& sample, PhysicsContactPhase phase)
    {
        contacts.push_back({frame, step, phase, sample.pair.a, sample.pair.b, sample.point,
                            sample.normal, sample.is_trigger});
    }

    void synchronize_contacts(const WorldSystem& world)
    {
        const auto removed = [&](const ContactSample& sample)
        {
            return object_body(sample.pair.a).IsInvalid() || object_body(sample.pair.b).IsInvalid();
        };
        std::erase_if(body_contacts,
                      [&](const auto& item)
                      {
                          return !body_objects.contains(item.first.GetBody1ID()) ||
                                 !body_objects.contains(item.first.GetBody2ID()) ||
                                 removed(item.second);
                      });
        std::erase_if(character_contacts,
                      [&](const auto& item)
                      {
                          const auto& [actor, other_character, target, subshape] = item.first;
                          return !character_objects.contains(JPH::CharacterID(actor)) ||
                                 (other_character
                                      ? !character_objects.contains(JPH::CharacterID(target))
                                      : !body_objects.contains(JPH::BodyID(target))) ||
                                 removed(item.second);
                      });
        std::erase_if(touched_contacts, [&](const auto& item) { return removed(item.second); });
        for (auto iterator = previous_contacts.begin(); iterator != previous_contacts.end();)
        {
            if (removed(iterator->second))
            {
                const auto& pair = iterator->second.pair;
                const bool retained_scene = std::any_of(
                    world.scenes().begin(), world.scenes().end(), [&](const auto& scene)
                    { return scene->id() == pair.a.scene_id || scene->id() == pair.b.scene_id; });
                if (retained_scene)
                {
                    append_contact(iterator->second, PhysicsContactPhase::end);
                }
                iterator = previous_contacts.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
    }

    bool sleeping_contact(const ContactSample& sample) const
    {
        const auto a = object_body(sample.pair.a);
        const auto b = object_body(sample.pair.b);
        if (a.IsInvalid() || b.IsInvalid())
        {
            return false;
        }
        const JPH::BodyID ids[]{a, b};
        JPH::BodyLockMultiRead lock(physics.GetBodyLockInterface(), ids, 2);
        const auto* first = lock.GetBody(0);
        const auto* second = lock.GetBody(1);
        if (first == nullptr || second == nullptr || first->IsActive() || second->IsActive())
        {
            return false;
        }
        // Jolt 休眠会移除求解接触；用上游窄相位复核已有对象对，不扫描整个世界。
        const auto shape = first->GetTransformedShape();
        const auto other = second->GetTransformedShape();
        JPH::CollideShapeSettings settings;
        settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
        settings.mMaxSeparationDistance =
            sample.is_trigger ? 0.0F : physics.GetPhysicsSettings().mSpeculativeContactDistance;
        JPH::AnyHitCollisionCollector<JPH::CollideShapeCollector> collector;
        other.CollideShape(first->GetShape(), shape.GetShapeScale(),
                           shape.GetCenterOfMassTransform(), settings,
                           first->GetCenterOfMassPosition(), collector);
        return collector.HadHit();
    }

    void publish_contacts()
    {
        std::map<ContactPair, ContactSample> current;
        for (const auto& [key, sample] : body_contacts)
        {
            current.insert_or_assign(sample.pair, sample);
        }
        for (const auto& [key, sample] : character_contacts)
        {
            current.insert_or_assign(sample.pair, sample);
        }
        for (const auto& [pair, sample] : previous_contacts)
        {
            if (!current.contains(pair) && sleeping_contact(sample))
            {
                current.emplace(pair, sample);
            }
        }
        for (const auto& [pair, sample] : previous_contacts)
        {
            const auto next = current.find(pair);
            if (next == current.end() || next->second.is_trigger != sample.is_trigger)
            {
                append_contact(sample, PhysicsContactPhase::end);
            }
        }
        for (const auto& [pair, sample] : current)
        {
            const auto previous = previous_contacts.find(pair);
            const bool continuing = previous != previous_contacts.end() &&
                                    previous->second.is_trigger == sample.is_trigger;
            append_contact(sample,
                           continuing ? PhysicsContactPhase::stay : PhysicsContactPhase::begin);
        }
        // 一次角色操作内产生又消失的接触仍保留 begin/end，不漏掉穿过窄触发区。
        for (const auto& [pair, sample] : touched_contacts)
        {
            const auto previous = previous_contacts.find(pair);
            const auto next = current.find(pair);
            if ((previous == previous_contacts.end() ||
                 previous->second.is_trigger != sample.is_trigger) &&
                (next == current.end() || next->second.is_trigger != sample.is_trigger))
            {
                append_contact(sample, PhysicsContactPhase::begin);
                append_contact(sample, PhysicsContactPhase::end);
            }
        }
        previous_contacts = std::move(current);
        touched_contacts.clear();
    }

    void clear() noexcept
    {
        jobs.wait_for_wrappers();
        character_collisions.mCharacters.clear();
        characters.clear();
        auto& interface = physics.GetBodyInterface();
        for (const auto& [id, entry] : bodies)
        {
            (void)id;
            interface.RemoveBody(entry.body);
            interface.DestroyBody(entry.body);
        }
        bodies.clear();
        body_snapshots.clear();
        character_snapshots.clear();
        synchronized_scenes.clear();
        accumulated = 0.0;
        pending_impacts.clear();
        impacts.clear();
        body_objects.clear();
        character_objects.clear();
        body_contacts.clear();
        character_contacts.clear();
        previous_contacts.clear();
        touched_contacts.clear();
        contacts.clear();
    }

    void remove_body(std::uint64_t node_id)
    {
        const auto found = bodies.find(node_id);
        if (found == bodies.end())
        {
            return;
        }
        auto& interface = physics.GetBodyInterface();
        auto affected = interface.GetTransformedShape(found->second.body).GetWorldSpaceBounds();
        affected.ExpandBy(JPH::Vec3::sReplicate(0.1F));
        body_objects.erase(found->second.body);
        interface.RemoveBody(found->second.body);
        interface.DestroyBody(found->second.body);
        bodies.erase(found);
        interface.ActivateBodiesInAABox(affected,
                                        physics.GetDefaultBroadPhaseLayerFilter(static_layer),
                                        physics.GetDefaultLayerFilter(static_layer));
    }

    bool publish_body(const BodySnapshot& snapshot)
    {
        auto& interface = physics.GetBodyInterface();
        auto found = bodies.find(snapshot.node_id);
        const bool moving = snapshot.rigid_body.motion != schema::BodyMotion::static_body;
        // Velocity is mutable motion state, not a reason to replace the shape or BodyID.
        auto previous_settings =
            found != bodies.end() ? found->second.snapshot.rigid_body : snapshot.rigid_body;
        previous_settings.linear_velocity = snapshot.rigid_body.linear_velocity;
        previous_settings.angular_velocity = snapshot.rigid_body.angular_velocity;
        const bool rebuild = found == bodies.end() ||
                             found->second.snapshot.collider_id != snapshot.collider_id ||
                             (found->second.snapshot.collider != snapshot.collider &&
                              *found->second.snapshot.collider != *snapshot.collider) ||
                             found->second.snapshot.mesh != snapshot.mesh ||
                             found->second.snapshot.transient_mesh != snapshot.transient_mesh ||
                             found->second.snapshot.mesh_parts != snapshot.mesh_parts ||
                             previous_settings != snapshot.rigid_body ||
                             found->second.snapshot.shape != snapshot.shape;
        if (!rebuild)
        {
            const bool moved_static =
                !moving && (found->second.snapshot.position != snapshot.position ||
                            found->second.snapshot.rotation != snapshot.rotation);
            JPH::AABox affected;
            if (moved_static)
            {
                affected = interface.GetTransformedShape(found->second.body).GetWorldSpaceBounds();
            }
            interface.SetPositionAndRotationWhenChanged(
                found->second.body, JPH::RVec3(snapshot.position), snapshot.rotation,
                JPH::EActivation::Activate);
            const auto& previous = found->second.snapshot.rigid_body;
            if (previous.linear_velocity != snapshot.rigid_body.linear_velocity ||
                previous.angular_velocity != snapshot.rigid_body.angular_velocity)
            {
                interface.SetLinearAndAngularVelocity(found->second.body,
                                                      vector(snapshot.rigid_body.linear_velocity),
                                                      vector(snapshot.rigid_body.angular_velocity));
            }
            found->second.snapshot = snapshot;
            if (moved_static)
            {
                affected.Encapsulate(
                    interface.GetTransformedShape(found->second.body).GetWorldSpaceBounds());
                activate_bounds(affected);
            }
            return true;
        }
        ENGINE_PROFILE_ZONE("Physics.publish_body");
        JPH::AABox affected;
        bool local_change = false;
        if (found != bodies.end())
        {
            const auto& previous = found->second.snapshot;
            local_change = !moving && previous.part_shapes && snapshot.part_shapes &&
                           *previous.collider == *snapshot.collider &&
                           previous.rigid_body == snapshot.rigid_body &&
                           previous.position == snapshot.position &&
                           previous.rotation == snapshot.rotation &&
                           previous.shape_scale == snapshot.shape_scale;
            affected =
                local_change
                    ? changed_bounds(previous, snapshot)
                    : interface.GetTransformedShape(found->second.body).GetWorldSpaceBounds();
        }
        if (found != bodies.end() && !moving &&
            found->second.snapshot.collider_id == snapshot.collider_id &&
            found->second.snapshot.rigid_body == snapshot.rigid_body &&
            found->second.snapshot.collider->is_trigger == snapshot.collider->is_trigger)
        {
            // 候选形状已完整准备；静态形状替换不会销毁原 BodyID。
            interface.SetShape(found->second.body, snapshot.shape.GetPtr(), false,
                               JPH::EActivation::DontActivate);
            const auto& previous = found->second.snapshot;
            if (previous.position != snapshot.position || previous.rotation != snapshot.rotation)
            {
                interface.SetPositionAndRotation(found->second.body, JPH::RVec3(snapshot.position),
                                                 snapshot.rotation, JPH::EActivation::DontActivate);
            }
            if (previous.collider->friction != snapshot.collider->friction)
            {
                interface.SetFriction(found->second.body, snapshot.collider->friction);
            }
            if (previous.collider->restitution != snapshot.collider->restitution)
            {
                interface.SetRestitution(found->second.body, snapshot.collider->restitution);
            }
            if (static_cast<bool>(found->second.snapshot.mesh_parts) !=
                static_cast<bool>(snapshot.mesh_parts))
            {
                JPH::BodyLockWrite lock(physics.GetBodyLockInterface(), found->second.body);
                if (lock.Succeeded())
                {
                    lock.GetBody().SetEnhancedInternalEdgeRemoval(snapshot.mesh_parts != nullptr);
                }
            }
            found->second.snapshot = snapshot;
        }
        else
        {
            JPH::BodyCreationSettings settings(
                snapshot.shape.GetPtr(), JPH::RVec3(snapshot.position), snapshot.rotation,
                motion_type(snapshot.rigid_body.motion), moving ? moving_layer : static_layer);
            settings.mIsSensor = snapshot.collider->is_trigger;
            settings.mCollideKinematicVsNonDynamic =
                snapshot.rigid_body.motion == schema::BodyMotion::kinematic;
            // Jolt 官方 MeshShapeUserDataTest：复合网格边界需要增强内部边消除。
            // 只标记分段表面；Jolt 对接触双方取 OR，不增加普通刚体间的求解负担。
            settings.mEnhancedInternalEdgeRemoval = snapshot.mesh_parts != nullptr;
            settings.mUserData = snapshot.node_id;
            settings.mFriction = snapshot.collider->friction;
            settings.mRestitution = snapshot.collider->restitution;
            settings.mLinearDamping = snapshot.rigid_body.linear_damping;
            settings.mAngularDamping = snapshot.rigid_body.angular_damping;
            settings.mGravityFactor = snapshot.rigid_body.gravity_factor;
            settings.mLinearVelocity = vector(snapshot.rigid_body.linear_velocity);
            settings.mAngularVelocity = vector(snapshot.rigid_body.angular_velocity);
            settings.mMaxLinearVelocity = 10000.0F;
            settings.mMaxAngularVelocity = 10000.0F;
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = snapshot.rigid_body.mass;
            JPH::BodyID body;
            {
                ENGINE_PROFILE_ZONE("Physics.create_body");
                body = interface.CreateAndAddBody(
                    settings, moving ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
            }
            if (body.IsInvalid())
            {
                return false;
            }
            // 创建失败时旧物体仍存在；成功后才移除旧物体。
            if (found != bodies.end())
            {
                body_objects.erase(found->second.body);
                interface.RemoveBody(found->second.body);
                interface.DestroyBody(found->second.body);
                found->second = Entry{snapshot, body};
            }
            else
            {
                found = bodies.emplace(snapshot.node_id, Entry{snapshot, body}).first;
            }
        }
        body_objects.insert_or_assign(
            found->second.body, PhysicsObject{snapshot.scene_id, snapshot.node_id,
                                              PhysicsObjectKind::collider, snapshot.collider_id});
        if (!moving)
        {
            if (!local_change)
            {
                affected.Encapsulate(
                    interface.GetTransformedShape(found->second.body).GetWorldSpaceBounds());
            }
            activate_bounds(affected);
        }
        return true;
    }

    void activate_bounds(JPH::AABox bounds)
    {
        if (bounds.IsValid())
        {
            bounds.ExpandBy(JPH::Vec3::sReplicate(0.1F));
            physics.GetBodyInterface().ActivateBodiesInAABox(
                bounds, physics.GetDefaultBroadPhaseLayerFilter(static_layer),
                physics.GetDefaultLayerFilter(static_layer));
        }
    }

    bool synchronize(const std::vector<BodySnapshot>& snapshots)
    {
        for (const auto& snapshot : snapshots)
        {
            if (auto found = bodies.find(snapshot.node_id); found != bodies.end())
            {
                found->second.seen_frame = frame;
            }
        }
        for (auto iterator = bodies.begin(); iterator != bodies.end();)
        {
            if (iterator->second.seen_frame != frame)
            {
                const auto id = iterator->first;
                ++iterator;
                remove_body(id);
            }
            else
            {
                ++iterator;
            }
        }
        for (const auto& snapshot : snapshots)
        {
            if (!publish_body(snapshot))
            {
                return false;
            }
        }
        return true;
    }
    bool synchronize_characters(const std::vector<CharacterSnapshot>& snapshots)
    {
        for (const auto& snapshot : snapshots)
        {
            if (auto found = characters.find(snapshot.node_id); found != characters.end())
            {
                found->second.seen_frame = frame;
            }
        }
        for (auto iterator = characters.begin(); iterator != characters.end();)
        {
            if (iterator->second.seen_frame != frame)
            {
                body_objects.erase(iterator->second.character->GetInnerBodyID());
                character_objects.erase(iterator->second.character->GetID());
                character_collisions.Remove(iterator->second.character.GetPtr());
                iterator = characters.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
        for (const auto& snapshot : snapshots)
        {
            auto found = characters.find(snapshot.node_id);
            const bool recreate =
                found == characters.end() ||
                found->second.snapshot.instance_id != snapshot.instance_id ||
                found->second.snapshot.settings.radius != snapshot.settings.radius ||
                found->second.snapshot.settings.half_height != snapshot.settings.half_height ||
                found->second.snapshot.settings.max_slope_degrees !=
                    snapshot.settings.max_slope_degrees;
            if (recreate)
            {
                if (found != characters.end())
                {
                    body_objects.erase(found->second.character->GetInnerBodyID());
                    character_objects.erase(found->second.character->GetID());
                    character_collisions.Remove(found->second.character.GetPtr());
                    characters.erase(found);
                }
                const auto& settings = snapshot.settings;
                auto capsule =
                    JPH::CapsuleShapeSettings(settings.half_height, settings.radius).Create();
                if (capsule.HasError() || !capsule.IsValid())
                {
                    return false;
                }
                auto shape = JPH::RotatedTranslatedShapeSettings(
                                 JPH::Vec3(0, settings.radius + settings.half_height, 0),
                                 JPH::Quat::sIdentity(), capsule.Get().GetPtr())
                                 .Create();
                if (shape.HasError() || !shape.IsValid())
                {
                    return false;
                }
                JPH::CharacterVirtualSettings configuration;
                configuration.mShape = shape.Get();
                configuration.mInnerBodyShape = shape.Get();
                configuration.mInnerBodyLayer = character_layer;
                configuration.mEnhancedInternalEdgeRemoval = true;
                configuration.mMaxSlopeAngle =
                    settings.max_slope_degrees * std::numbers::pi_v<float> / 180.0F;
                configuration.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -settings.radius);
                CharacterEntry entry;
                entry.snapshot = snapshot;
                entry.character =
                    new JPH::CharacterVirtual(&configuration, JPH::RVec3(snapshot.position),
                                              JPH::Quat::sIdentity(), snapshot.node_id, &physics);
                if (entry.character->GetInnerBodyID().IsInvalid())
                {
                    return false;
                }
                const PhysicsObject object{snapshot.scene_id, snapshot.node_id,
                                           PhysicsObjectKind::character, snapshot.instance_id};
                body_objects.insert_or_assign(entry.character->GetInnerBodyID(), object);
                character_objects.insert_or_assign(entry.character->GetID(), object);
                entry.character->SetListener(this);
                entry.character->SetCharacterVsCharacterCollision(&character_collisions);
                character_collisions.Add(entry.character.GetPtr());
                found = characters.emplace(snapshot.node_id, std::move(entry)).first;
                found->second.character->RefreshContacts(
                    physics.GetDefaultBroadPhaseLayerFilter(moving_layer),
                    physics.GetDefaultLayerFilter(moving_layer), CharacterBodyFilter{}, {},
                    allocator);
            }
            else if (snapshot.reset_velocity || !found->second.character->GetPosition().IsClose(
                                                    JPH::RVec3(snapshot.position), 1.0e-10F))
            {
                found->second.character->SetPosition(JPH::RVec3(snapshot.position));
                found->second.character->SetLinearVelocity(JPH::Vec3::sZero());
                found->second.external_velocity = JPH::Vec3::sZero();
                found->second.character->RefreshContacts(
                    physics.GetDefaultBroadPhaseLayerFilter(moving_layer),
                    physics.GetDefaultLayerFilter(moving_layer), CharacterBodyFilter{}, {},
                    allocator);
            }
            found->second.snapshot = snapshot;
        }
        return true;
    }
};

PhysicsSystem::PhysicsSystem(WorldSystem* world, TaskSystem* tasks) noexcept
    : world_(world), tasks_(tasks)
{
}

PhysicsSystem::~PhysicsSystem() noexcept
{
    shutdown();
}

bool PhysicsSystem::on_initialize(const EngineConfig&)
{
    if (tasks_ != nullptr && !tasks_->is_initialized())
    {
        return false;
    }
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
    state_->jobs.initialize(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, tasks_);
    state_->physics.Init(static_cast<JPH::uint>(maximum_bodies), 0, 65536, 16384, state_->layers,
                         state_->broad_filter, state_->object_filter);
    state_->physics.SetGravity(JPH::Vec3(0.0F, -9.81F, 0.0F));
    state_->physics.SetContactListener(state_.get());
    if (world_)
    {
        state_->bind_queries(*world_);
    }
    return true;
}

void PhysicsSystem::clear() noexcept
{
    if (state_ && world_ && world_->is_initialized())
    {
        state_->bind_queries(*world_);
    }
    if (state_ && state_->motors && world_ && world_->is_initialized())
    {
        flecs::query<CharacterMotorRuntime>(state_->motors)
            .each(
                [](CharacterMotorRuntime& motor)
                {
                    motor.desired_velocity_ = {};
                    motor.velocity_ = {};
                    motor.pending_velocity_ = {};
                    motor.pending_jump_ = 0.0F;
                    motor.grounded_ = false;
                    motor.reset_pending_ = false;
                });
    }
    if (state_)
    {
        state_->clear();
    }
}

std::size_t PhysicsSystem::body_count() const noexcept
{
    return state_ ? state_->bodies.size() : 0;
}

std::size_t PhysicsSystem::character_count() const noexcept
{
    return state_ ? state_->characters.size() : 0;
}

bool PhysicsSystem::cast_sphere(const Vec3& origin, const Vec3& displacement, float radius,
                                float& fraction) const
{
    fraction = 0.0F;
    const auto position = vector(origin);
    const auto direction = vector(displacement);
    if (!state_ || !world_ || !world_->is_initialized() || !finite(position) ||
        !finite(direction) || position.Abs().ReduceMax() > 10000000.0F ||
        direction.Abs().ReduceMax() > 100000.0F || !std::isfinite(radius) || radius < 0.0001F ||
        radius > 1000.0F)
    {
        return false;
    }
    const auto shape = JPH::SphereShapeSettings(radius).Create();
    if (shape.HasError() || !shape.IsValid())
    {
        return false;
    }
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    JPH::ShapeCastSettings settings;
    settings.mReturnDeepestPoint = true;
    settings.SetBackFaceMode(JPH::EBackFaceMode::CollideWithBackFaces);
    const auto cast = JPH::RShapeCast::sFromWorldTransform(
        shape.Get().GetPtr(), JPH::Vec3::sOne(), JPH::RMat44::sTranslation(JPH::RVec3(position)),
        direction);
    const PhysicsQueryFilter filter{.include_characters = false};
    state_->physics.GetNarrowPhaseQuery().CastShape(
        cast, settings, JPH::RVec3::sZero(), collector,
        state_->physics.GetDefaultBroadPhaseLayerFilter(moving_layer),
        state_->physics.GetDefaultLayerFilter(moving_layer),
        State::QueryBodyFilter{*state_, *world_, filter});
    fraction = collector.HadHit() ? std::clamp(collector.mHit.mFraction, 0.0F, 1.0F) : 1.0F;
    return true;
}

bool PhysicsSystem::cast_ray(Collider collider, const Vec3& origin, const Vec3& displacement,
                             float& fraction) const
{
    fraction = 0.0F;
    if (!collider)
    {
        return false;
    }
    fraction = 0;
    const auto node = collider.node();
    const auto position = vector(origin);
    const auto direction = vector(displacement);
    if (!state_ || !world_ || world_->find_node(node.id()) != node ||
        node.get_component<Collider>() != collider || !finite(position) || !finite(direction) ||
        position.Abs().ReduceMax() > 10000000.0F || direction.Abs().ReduceMax() > 100000.0F)
    {
        return false;
    }
    fraction = 1;
    const auto body = state_->bodies.find(node.id());
    if (body == state_->bodies.end() ||
        body->second.snapshot.collider_id != collider.instance_id() || !active_collision(node))
    {
        return true;
    }
    JPH::RayCastResult hit;
    const JPH::RRayCast ray{JPH::RVec3(position), direction};
    if (state_->physics.GetNarrowPhaseQuery().CastRay(
            ray, hit, state_->physics.GetDefaultBroadPhaseLayerFilter(moving_layer),
            state_->physics.GetDefaultLayerFilter(moving_layer),
            TargetBodyFilter{body->second.body}))
    {
        fraction = std::clamp(hit.mFraction, 0.0F, 1.0F);
    }
    return true;
}

bool PhysicsSystem::query_ray(const Vec3& origin, const Vec3& displacement,
                              const PhysicsQueryFilter& filter, PhysicsQueryHit& hit) const
{
    hit = {};
    const auto position = vector(origin);
    const auto direction = vector(displacement);
    if (!state_ || !state_->can_query(world_) || !valid_query(world_, position, direction, filter))
    {
        return false;
    }
    JPH::RayCastResult result;
    const JPH::RRayCast ray{JPH::RVec3(position), direction};
    if (!state_->physics.GetNarrowPhaseQuery().CastRay(
            ray, result, state_->physics.GetDefaultBroadPhaseLayerFilter(moving_layer),
            state_->physics.GetDefaultLayerFilter(moving_layer),
            State::QueryBodyFilter{*state_, *world_, filter}))
    {
        return true;
    }
    JPH::BodyLockRead lock(state_->physics.GetBodyLockInterface(), result.mBodyID);
    if (!lock.Succeeded())
    {
        return false;
    }
    const auto point = ray.GetPointOnRay(result.mFraction);
    const auto normal = result.mFraction > 0.0F
                            ? lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point)
                            : -direction.Normalized();
    return state_->query_hit(*world_, lock.GetBody(), result.mFraction, direction, point, normal,
                             hit);
}

bool PhysicsSystem::query_sphere(const Vec3& origin, const Vec3& displacement, float radius,
                                 const PhysicsQueryFilter& filter, PhysicsQueryHit& hit) const
{
    hit = {};
    const auto position = vector(origin);
    const auto direction = vector(displacement);
    if (!state_ || !state_->can_query(world_) ||
        !valid_query(world_, position, direction, filter) || !std::isfinite(radius) ||
        radius < 0.0001F || radius > 1000.0F)
    {
        return false;
    }
    const auto shape = JPH::SphereShapeSettings(radius).Create();
    if (shape.HasError() || !shape.IsValid())
    {
        return false;
    }
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    JPH::ShapeCastSettings settings;
    settings.mReturnDeepestPoint = true;
    settings.SetBackFaceMode(JPH::EBackFaceMode::CollideWithBackFaces);
    const auto base = JPH::RVec3(position);
    const auto cast = JPH::RShapeCast::sFromWorldTransform(
        shape.Get().GetPtr(), JPH::Vec3::sOne(), JPH::RMat44::sTranslation(base), direction);
    state_->physics.GetNarrowPhaseQuery().CastShape(
        cast, settings, base, collector,
        state_->physics.GetDefaultBroadPhaseLayerFilter(moving_layer),
        state_->physics.GetDefaultLayerFilter(moving_layer),
        State::QueryBodyFilter{*state_, *world_, filter});
    if (!collector.HadHit())
    {
        return true;
    }
    const auto& result = collector.mHit;
    JPH::BodyLockRead lock(state_->physics.GetBodyLockInterface(), result.mBodyID2);
    if (!lock.Succeeded())
    {
        return false;
    }
    return state_->query_hit(*world_, lock.GetBody(), result.mFraction, direction,
                             base + result.mContactPointOn2, -result.mPenetrationAxis, hit);
}

bool PhysicsSystem::overlap_sphere(const Vec3& center, float radius,
                                   const PhysicsQueryFilter& filter,
                                   std::vector<PhysicsObject>& objects) const
{
    objects.clear();
    const auto position = vector(center);
    if (!state_ || !state_->can_query(world_) || !valid_overlap(world_, position, filter) ||
        !std::isfinite(radius) || radius < 0.0001F || radius > 1000.0F)
    {
        return false;
    }
    const auto shape = JPH::SphereShapeSettings(radius).Create();
    return !shape.HasError() && shape.IsValid() &&
           state_->overlap(*world_, *shape.Get(), JPH::RVec3(position), filter, objects);
}

bool PhysicsSystem::overlap_box(const Vec3& center, const Vec3& half_extents,
                                const PhysicsQueryFilter& filter,
                                std::vector<PhysicsObject>& objects) const
{
    objects.clear();
    const auto position = vector(center);
    const auto extents = vector(half_extents);
    if (!state_ || !state_->can_query(world_) || !valid_overlap(world_, position, filter) ||
        !finite(extents) || extents.ReduceMin() < 0.0001F || extents.ReduceMax() > 1000.0F)
    {
        return false;
    }
    const auto shape = JPH::BoxShapeSettings(extents, 0.0F).Create();
    return !shape.HasError() && shape.IsValid() &&
           state_->overlap(*world_, *shape.Get(), JPH::RVec3(position), filter, objects);
}

bool PhysicsSystem::is_object_valid(const PhysicsObject& object) const noexcept
{
    return state_ != nullptr && world_ != nullptr && active_physics_object(*world_, object);
}

bool PhysicsSystem::is_hit_valid(const PhysicsQueryHit& hit) const noexcept
{
    return hit.hit && is_object_valid(hit.object);
}

std::span<const PhysicsContactEvent> PhysicsSystem::contacts() const noexcept
{
    if (!state_ || !state_->can_query(world_))
    {
        return {};
    }
    return state_->contacts;
}

std::span<const PhysicsImpact> PhysicsSystem::impacts() const noexcept
{
    if (!state_)
    {
        return {};
    }
    std::erase_if(state_->impacts,
                  [&](const auto& impact)
                  {
                      return !is_object_valid(impact.a) || !is_object_valid(impact.b) ||
                             state_->object_body(impact.a).IsInvalid() ||
                             state_->object_body(impact.b).IsInvalid();
                  });
    return state_->impacts;
}

bool PhysicsSystem::set_collider_data(Collider collider, const schema::ColliderDataT& settings)
{
    if (!collider || !world_ || !world_->can_modify())
    {
        return false;
    }
    ENGINE_PROFILE_ZONE("Physics.set_collider");
    auto node = collider.node();
    if (state_ == nullptr || world_ == nullptr || world_->find_node(node.id()) != node ||
        node.get_component<Collider>() != collider || !Collider::valid_settings(settings))
    {
        return false;
    }
    auto rigid = node.get_component<RigidBody>();
    auto motor = node.get_component<CharacterMotor>();
    if (motor.valid() && motor.active() && collider.active())
    {
        return false;
    }
    BodySnapshot snapshot;
    if (settings.shape == schema::ColliderShape::mesh)
    {
        auto* resources = ResourceSystem::instance();
        if (resources == nullptr || !resources->loader().load(settings.mesh_guid, snapshot.mesh) ||
            !snapshot.mesh || snapshot.mesh->mesh() == nullptr)
        {
            return false;
        }
    }
    const auto* mesh = snapshot.mesh ? snapshot.mesh->mesh() : nullptr;
    if (!read_snapshot(node, settings, mesh, rigid, snapshot))
    {
        return false;
    }
    const bool active =
        collider.active() && active_transform(node) && (!rigid.valid() || rigid.active());
    snapshot.shape_scale = snapshot.scale;
    if (active && !make_shape(*snapshot.collider, mesh, snapshot.shape_scale, snapshot.shape))
    {
        return false;
    }
    if (active && !state_->publish_body(snapshot))
    {
        return false;
    }
    if (!active)
    {
        state_->remove_body(node.id());
    }
    state_->mark_synchronized(node.scene()->id());
    *collider.write_data<Collider::Data>() = *snapshot.collider;
    collider.modified();
    engine::ecs_detail::component_runtime(collider)->mesh_ = std::move(snapshot.mesh);
    engine::ecs_detail::component_runtime(collider)->transient_mesh_.reset();
    engine::ecs_detail::component_runtime(collider)->mesh_parts_.reset();
    return true;
}

bool PhysicsSystem::set_mesh_data(Collider collider, const schema::MeshDataT& mesh)
{
    if (!collider || !world_ || !world_->can_modify())
    {
        return false;
    }
    ENGINE_PROFILE_ZONE("Physics.set_mesh");
    auto node = collider.node();
    if (state_ == nullptr || world_ == nullptr || world_->find_node(node.id()) != node ||
        node.get_component<Collider>() != collider || mesh.schema_version != 1)
    {
        return false;
    }
    auto rigid = node.get_component<RigidBody>();
    auto motor = node.get_component<CharacterMotor>();
    if ((rigid.valid() && rigid.settings().motion != schema::BodyMotion::static_body) ||
        (motor.valid() && motor.active() && collider.active()))
    {
        return false;
    }
    BodySnapshot snapshot;
    snapshot.mesh_parts = engine::ecs_detail::component_runtime(collider)->mesh_parts_;
    if (!read_snapshot(node, collider.settings(), collider.mesh_data(), rigid, snapshot))
    {
        return false;
    }
    auto candidate = *snapshot.collider;
    candidate.shape = schema::ColliderShape::mesh;
    candidate.mesh_guid.clear();
    candidate.hulls.clear();
    snapshot.collider = std::make_shared<const schema::ColliderDataT>(std::move(candidate));
    snapshot.mesh_parts.reset();
    // 拷贝隔离调用方后续修改；不发布未完成的 Jolt Shape。
    snapshot.transient_mesh = std::make_shared<const schema::MeshDataT>(mesh);
    const bool empty = mesh.vertices.empty() && mesh.indices.empty();
    snapshot.shape_scale = snapshot.scale;
    if (!empty && !make_shape(*snapshot.collider, snapshot.transient_mesh.get(),
                              snapshot.shape_scale, snapshot.shape))
    {
        return false;
    }
    const bool active =
        !empty && collider.active() && active_transform(node) && (!rigid.valid() || rigid.active());
    if (active && !state_->publish_body(snapshot))
    {
        return false;
    }
    if (!active)
    {
        state_->remove_body(node.id());
    }
    state_->mark_synchronized(node.scene()->id());
    *collider.write_data<Collider::Data>() = *snapshot.collider;
    collider.modified();
    engine::ecs_detail::component_runtime(collider)->mesh_.reset();
    engine::ecs_detail::component_runtime(collider)->transient_mesh_ =
        std::move(snapshot.transient_mesh);
    engine::ecs_detail::component_runtime(collider)->mesh_parts_.reset();
    return true;
}

bool PhysicsSystem::set_mesh_parts(Collider collider, const MeshCollisionData& parts)
{
    if (!collider || !world_ || !world_->can_modify())
    {
        return false;
    }
    ENGINE_PROFILE_ZONE("Physics.set_mesh_parts");
    auto node = collider.node();
    if (state_ == nullptr || world_ == nullptr || world_->find_node(node.id()) != node ||
        node.get_component<Collider>() != collider)
    {
        return false;
    }
    auto rigid = node.get_component<RigidBody>();
    auto motor = node.get_component<CharacterMotor>();
    if ((rigid.valid() && rigid.settings().motion != schema::BodyMotion::static_body) ||
        (motor.valid() && motor.active() && collider.active()))
    {
        return false;
    }
    BodySnapshot snapshot;
    snapshot.mesh_parts = engine::ecs_detail::component_runtime(collider)->mesh_parts_;
    if (!read_snapshot(node, collider.settings(), collider.mesh_data(), rigid, snapshot))
    {
        return false;
    }
    auto settings = *snapshot.collider;
    settings.shape = schema::ColliderShape::mesh;
    settings.mesh_guid.clear();
    settings.hulls.clear();
    snapshot.collider = std::make_shared<const schema::ColliderDataT>(std::move(settings));
    if (!snapshot.mesh_parts || snapshot.mesh_parts->parts != parts.parts)
    {
        snapshot.mesh_parts = std::make_shared<const MeshCollisionData>(parts);
    }
    const auto found = state_->bodies.find(node.id());
    const auto* previous = found != state_->bodies.end() ? &found->second.snapshot : nullptr;
    if (!prepare_mesh_parts(snapshot, previous))
    {
        return false;
    }
    const bool active = snapshot.shape != nullptr && collider.active() && active_transform(node) &&
                        (!rigid.valid() || rigid.active());
    if (active && !state_->publish_body(snapshot))
    {
        return false;
    }
    if (!active)
    {
        state_->remove_body(node.id());
    }
    state_->mark_synchronized(node.scene()->id());
    *collider.write_data<Collider::Data>() = *snapshot.collider;
    collider.modified();
    engine::ecs_detail::component_runtime(collider)->mesh_.reset();
    engine::ecs_detail::component_runtime(collider)->transient_mesh_.reset();
    engine::ecs_detail::component_runtime(collider)->mesh_parts_ = std::move(snapshot.mesh_parts);
    return true;
}

bool PhysicsSystem::on_tick(double delta_time)
{
    ENGINE_PROFILE_ZONE("Physics.tick");
    if (!state_)
    {
        return false;
    }
    state_->impacts.clear();
    state_->pending_impacts.clear();
    state_->contacts.clear();
    state_->touched_contacts.clear();
    if (state_->frame == std::numeric_limits<std::uint64_t>::max())
    {
        clear();
        return false;
    }
    ++state_->frame;
    if (world_ == nullptr || !world_->is_initialized())
    {
        clear();
        return true;
    }
    if (!world_->refresh_scene_state())
    {
        return false;
    }
    state_->bind_queries(*world_);
    if (world_ != nullptr && world_->paused())
    {
        // 菜单期间不把新时间加入固定步累计器；一次性角色意图不能恢复后重放。
        flecs::query<CharacterMotorRuntime>(state_->motors)
            .each(
                [](CharacterMotorRuntime& motor)
                {
                    motor.desired_velocity_ = {};
                    motor.pending_jump_ = 0.0F;
                });
        return true;
    }
    auto& snapshots = state_->body_snapshots;
    auto& character_snapshots = state_->character_snapshots;
    snapshots.clear();
    character_snapshots.clear();
    auto& interface = state_->physics.GetBodyInterface();
    {
        ENGINE_PROFILE_ZONE("Physics.prepare");
        bool prepared = true;
        flecs::query<>(state_->nodes)
            .each(
                [&](flecs::entity entity)
                {
                    if (!prepared)
                    {
                        return;
                    }
                    const auto node = world_->find_node(entity.id());
                    if (auto motor = node.get_component<CharacterMotor>())
                    {
                        if (motor.active() && active_transform(node))
                        {
                            CharacterSnapshot snapshot;
                            if (!read_character(node, motor, snapshot))
                            {
                                std::fputs("Physics rejects character pose or conflicting body "
                                           "components\n",
                                           stderr);
                                prepared = false;
                                return;
                            }
                            snapshot.instance_id = motor.instance_id();
                            snapshot.reset_velocity =
                                engine::ecs_detail::component_runtime(motor)->reset_pending_;
                            character_snapshots.push_back(std::move(snapshot));
                        }
                        else
                        {
                            engine::ecs_detail::component_runtime(motor)->desired_velocity_ = {};
                            engine::ecs_detail::component_runtime(motor)->velocity_ = {};
                            engine::ecs_detail::component_runtime(motor)->pending_velocity_ = {};
                            engine::ecs_detail::component_runtime(motor)->pending_jump_ = 0.0F;
                            engine::ecs_detail::component_runtime(motor)->grounded_ = false;
                            engine::ecs_detail::component_runtime(motor)->reset_pending_ = false;
                        }
                    }
                    auto collider = node.get_component<Collider>();
                    auto rigid_body = node.get_component<RigidBody>();
                    if (!collider.valid() || !collider.active() ||
                        (rigid_body.valid() && !rigid_body.active()) || !active_transform(node))
                    {
                        if (rigid_body.valid())
                        {
                            engine::ecs_detail::component_runtime(rigid_body)
                                ->pending_impulse_ = {};
                        }
                        return;
                    }
                    BodySnapshot snapshot;
                    snapshot.mesh = engine::ecs_detail::component_runtime(collider)->mesh_;
                    snapshot.transient_mesh =
                        engine::ecs_detail::component_runtime(collider)->transient_mesh_;
                    snapshot.mesh_parts =
                        engine::ecs_detail::component_runtime(collider)->mesh_parts_;
                    const auto existing = state_->bodies.find(node.id());
                    const auto* previous =
                        existing != state_->bodies.end() ? &existing->second.snapshot : nullptr;
                    if (!read_snapshot(node, collider.settings(), collider.mesh_data(), rigid_body,
                                       snapshot, previous))
                    {
                        std::fputs("Physics rejects invalid settings, shear, reflection or a "
                                   "dynamic parent\n",
                                   stderr);
                        prepared = false;
                        return;
                    }
                    if (snapshot.transient_mesh && snapshot.transient_mesh->vertices.empty() &&
                        snapshot.transient_mesh->indices.empty())
                    {
                        return;
                    }
                    if (snapshot.mesh_parts)
                    {
                        if (!prepare_mesh_parts(snapshot, previous))
                        {
                            std::fputs("Physics cannot create segmented mesh collision\n", stderr);
                            prepared = false;
                            return;
                        }
                        if (snapshot.shape == nullptr)
                        {
                            return;
                        }
                    }
                    else if (existing != state_->bodies.end() &&
                             existing->second.snapshot.collider == snapshot.collider &&
                             existing->second.snapshot.mesh == snapshot.mesh &&
                             existing->second.snapshot.transient_mesh == snapshot.transient_mesh &&
                             existing->second.snapshot.shape_scale.IsClose(snapshot.scale, 1.0e-8F))
                    {
                        snapshot.shape = existing->second.snapshot.shape;
                        snapshot.shape_scale = existing->second.snapshot.shape_scale;
                    }
                    else
                    {
                        snapshot.shape_scale = snapshot.scale;
                        if (!make_shape(*snapshot.collider, collider.mesh_data(),
                                        snapshot.shape_scale, snapshot.shape))
                        {
                            std::fputs(
                                "Physics cannot create collider with the requested world scale\n",
                                stderr);
                            prepared = false;
                            return;
                        }
                    }
                    snapshots.push_back(std::move(snapshot));
                });
        if (!prepared)
        {
            clear();
            return false;
        }
        if (snapshots.size() + character_snapshots.size() > maximum_bodies ||
            !state_->synchronize(snapshots) || !state_->synchronize_characters(character_snapshots))
        {
            clear();
            return false;
        }
        snapshots.clear();
        character_snapshots.clear();
        state_->synchronized_scenes.clear();
        for (const auto& scene : world_->scenes())
        {
            state_->mark_synchronized(scene->id());
        }
        state_->synchronize_contacts(*world_);
        for (auto& [id, entry] : state_->bodies)
        {
            auto node = world_->find_node(id);
            auto body = !node.valid() ? RigidBody{} : node.get_component<RigidBody>();
            if (body.valid() && body.settings().motion == schema::BodyMotion::dynamic)
            {
                if (engine::ecs_detail::component_runtime(body)->pending_impulse_ != Vec3{})
                {
                    interface.AddImpulse(
                        entry.body,
                        vector(engine::ecs_detail::component_runtime(body)->pending_impulse_));
                    engine::ecs_detail::component_runtime(body)->pending_impulse_ = {};
                }
            }
        }
    }
    ENGINE_PROFILE_PLOT("Physics.bodies", static_cast<double>(state_->bodies.size()));
    ENGINE_PROFILE_PLOT("Physics.characters", static_cast<double>(state_->characters.size()));
    ENGINE_PROFILE_PLOT(
        "Physics.active_bodies",
        static_cast<double>(state_->physics.GetNumActiveBodies(JPH::EBodyType::RigidBody)));
    state_->accumulated = std::min(state_->accumulated + delta_time, fixed_step * maximum_steps);
    bool advanced = false;
    for (unsigned int step = 0; step < maximum_steps && state_->accumulated + 1.0e-12 >= fixed_step;
         ++step)
    {
        ENGINE_PROFILE_ZONE("Physics.fixed_step");
        if (state_->step == std::numeric_limits<std::uint64_t>::max())
        {
            clear();
            return false;
        }
        ++state_->step;
        state_->touched_contacts.clear();
        // 复用 Jolt CharacterVirtualTest 的速度/地面逻辑与 ExtendedUpdate 台阶求解。
        // 来源 Jolt Physics v5.4.0, Jorrit Rouwe, MIT。
        {
            ENGINE_PROFILE_ZONE("Physics.characters_step");
            for (auto& [id, entry] : state_->characters)
            {
                auto node = world_->find_node(id);
                auto motor = node.valid() ? node.get_component<CharacterMotor>() : CharacterMotor{};
                if (!motor.valid())
                {
                    clear();
                    return false;
                }
                auto& character = *entry.character;
                character.UpdateGroundVelocity();
                const auto ground_velocity = character.GetGroundVelocity();
                const bool on_ground =
                    character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
                const bool toward_ground =
                    character.GetLinearVelocity().GetY() - ground_velocity.GetY() < 0.1F;
                JPH::Vec3 velocity(0, character.GetLinearVelocity().GetY(), 0);
                if (on_ground && toward_ground)
                {
                    velocity = ground_velocity;
                    if (engine::ecs_detail::component_runtime(motor)->pending_jump_ > 0.0F)
                    {
                        velocity += JPH::Vec3(
                            0, engine::ecs_detail::component_runtime(motor)->pending_jump_, 0);
                    }
                }
                engine::ecs_detail::component_runtime(motor)->pending_jump_ = 0.0F;
                const auto delta =
                    vector(engine::ecs_detail::component_runtime(motor)->pending_velocity_);
                engine::ecs_detail::component_runtime(motor)->pending_velocity_ = {};
                entry.external_velocity += JPH::Vec3(delta.GetX(), 0, delta.GetZ());
                const auto gravity = state_->physics.GetGravity() * motor.settings().gravity_factor;
                velocity +=
                    gravity * static_cast<float>(fixed_step) +
                    vector(engine::ecs_detail::component_runtime(motor)->desired_velocity_) +
                    entry.external_velocity + JPH::Vec3(0, delta.GetY(), 0);
                if (!finite(velocity) || !finite(entry.external_velocity))
                {
                    clear();
                    return false;
                }
                character.SetLinearVelocity(velocity);
                JPH::CharacterVirtual::ExtendedUpdateSettings settings;
                settings.mStickToFloorStepDown =
                    JPH::Vec3(0, -motor.settings().ground_snap_distance, 0);
                settings.mWalkStairsStepUp = JPH::Vec3(0, motor.settings().step_height, 0);
                character.ExtendedUpdate(
                    static_cast<float>(fixed_step), gravity, settings,
                    state_->physics.GetDefaultBroadPhaseLayerFilter(moving_layer),
                    state_->physics.GetDefaultLayerFilter(moving_layer), CharacterBodyFilter{}, {},
                    state_->allocator);
                // ExtendedUpdate 求解位移，持久速度按 Jolt 的实际接触法线去掉被阻挡部分。
                // 不把受阻的主动输入反算成击退，防止下一帧出现相反方向的虚假速度。
                auto resolved = character.GetLinearVelocity();
                for (const auto& contact : character.GetActiveContacts())
                {
                    if (!contact.mHadCollision || contact.mWasDiscarded || contact.mIsSensorB)
                    {
                        continue;
                    }
                    const auto normal = contact.mContactNormal;
                    const float toward = (resolved - contact.mLinearVelocity).Dot(normal);
                    if (toward < 0.0F)
                    {
                        resolved -= normal * toward;
                    }
                    if (character.IsSlopeTooSteep(contact.mSurfaceNormal))
                    {
                        const JPH::Vec3 horizontal_normal(normal.GetX(), 0, normal.GetZ());
                        const float length_squared = horizontal_normal.LengthSq();
                        const float external_toward =
                            entry.external_velocity.Dot(horizontal_normal);
                        if (length_squared > 1.0e-12F && external_toward < 0.0F)
                        {
                            entry.external_velocity -=
                                horizontal_normal * (external_toward / length_squared);
                        }
                    }
                }
                character.SetLinearVelocity(resolved);
                entry.external_velocity *=
                    std::exp(-motor.settings().external_damping * static_cast<float>(fixed_step));
            }
        }
        {
            ENGINE_PROFILE_ZONE("Physics.jolt_step");
            if (state_->physics.Update(static_cast<float>(fixed_step), 1, &state_->allocator,
                                       &state_->jobs) != JPH::EPhysicsUpdateError::None)
            {
                clear();
                return false;
            }
        }
        state_->publish_contacts();
        state_->accumulated = std::max(0.0, state_->accumulated - fixed_step);
        advanced = true;
    }
    {
        ENGINE_PROFILE_ZONE("Physics.sync_results");
        for (auto& [id, entry] : state_->characters)
        {
            auto node = world_->find_node(id);
            auto motor = node.valid() ? node.get_component<CharacterMotor>() : CharacterMotor{};
            auto transform = node.valid() ? node.get_component<Transform>() : Transform{};
            if (!motor.valid() || !transform.valid())
            {
                clear();
                return false;
            }
            auto pose = transform.settings();
            pose.position = vector(JPH::Vec3(entry.character->GetPosition()));
            if (!transform.set_settings(pose))
            {
                clear();
                return false;
            }
            engine::ecs_detail::component_runtime(motor)->velocity_ =
                vector(entry.character->GetLinearVelocity());
            engine::ecs_detail::component_runtime(motor)->reset_pending_ = false;
            engine::ecs_detail::component_runtime(motor)->grounded_ =
                entry.character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround &&
                entry.character->GetLinearVelocity().GetY() -
                        entry.character->GetGroundVelocity().GetY() <
                    0.1F;
        }
        for (auto& [id, entry] : state_->bodies)
        {
            if (entry.snapshot.rigid_body.motion == schema::BodyMotion::static_body)
            {
                continue;
            }
            auto node = world_->find_node(id);
            auto body = !node.valid() ? RigidBody{} : node.get_component<RigidBody>();
            if (!body.valid())
            {
                clear();
                return false;
            }
            JPH::RVec3 position;
            JPH::Quat rotation;
            interface.GetPositionAndRotation(entry.body, position, rotation);
            const bool driven = entry.snapshot.rigid_body.motion == schema::BodyMotion::dynamic ||
                                entry.snapshot.rigid_body.linear_velocity != Vec3{} ||
                                entry.snapshot.rigid_body.angular_velocity != Vec3{};
            if (advanced && driven &&
                !write_transform(node, JPH::Vec3(position), rotation, entry.snapshot.scale))
            {
                std::fputs("Cannot write physics pose through the node hierarchy\n", stderr);
                clear();
                return false;
            }
            if (advanced && entry.snapshot.rigid_body.motion == schema::BodyMotion::dynamic)
            {
                entry.snapshot.position = JPH::Vec3(position);
                entry.snapshot.rotation = rotation;
                entry.snapshot.written_transform = node.get_component<Transform>().settings();
            }
            JPH::Vec3 linear_velocity;
            JPH::Vec3 angular_velocity;
            interface.GetLinearAndAngularVelocity(entry.body, linear_velocity, angular_velocity);
            auto settings = body.settings();
            settings.linear_velocity = vector(linear_velocity);
            settings.angular_velocity = vector(angular_velocity);
            if (!body.set_settings(settings))
            {
                clear();
                return false;
            }
            entry.snapshot.rigid_body = settings;
        }
        state_->impacts.reserve(state_->pending_impacts.size());
        for (const auto& [pair, impact] : state_->pending_impacts)
        {
            if (is_object_valid(pair.a) && is_object_valid(pair.b))
            {
                state_->impacts.push_back(impact);
            }
        }
        state_->pending_impacts.clear();
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
