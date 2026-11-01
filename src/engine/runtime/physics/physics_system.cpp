#include "runtime/physics/physics_system.hpp"

#include "runtime/performance/profiling.hpp"
#include "runtime/physics/mesh_collision_data.hpp"
#include "runtime/physics/physics_jobs.hpp"
#include "runtime/resource/resource.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/character_motor.hpp"
#include "runtime/scene/collider.hpp"
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
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
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

class SolidBodyFilter final : public JPH::BodyFilter
{
public:
    bool ShouldCollideLocked(const JPH::Body& body) const override
    {
        return !body.IsSensor();
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
    std::uint64_t node_id = 0;
    std::uint64_t instance_id = 0;
    schema::CharacterMotorDataT settings;
    JPH::Vec3 position = JPH::Vec3::sZero();
    bool reset_velocity = false;
};

bool read_character(const Node& node, const CharacterMotor& motor, CharacterSnapshot& output)
{
    const auto* transform = node.get_component<Transform>();
    const auto* collider = node.get_component<Collider>();
    const auto* rigid = node.get_component<RigidBody>();
    if (node.parent() != nullptr || transform == nullptr ||
        (collider != nullptr && collider->enabled()) || (rigid != nullptr && rigid->enabled()) ||
        !CharacterMotor::valid_settings(motor.settings()))
    {
        return false;
    }
    const auto& pose = transform->settings();
    if (pose.scale != Vec3(1, 1, 1) || pose.rotation.x() != 0.0 || pose.rotation.z() != 0.0 ||
        !finite(vector(pose.position)) || vector(pose.position).Abs().ReduceMax() > 10000000.0F)
    {
        return false;
    }
    output.node_id = node.id();
    output.settings = motor.settings();
    output.position = vector(pose.position);
    return true;
}

using MeshPartShapes = std::vector<JPH::RefConst<JPH::Shape>>;

struct BodySnapshot
{
    std::uint64_t node_id = 0;
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

bool read_snapshot(const Node& node, const schema::ColliderDataT& collider,
                   const schema::MeshDataT* mesh, const RigidBody* body, BodySnapshot& output,
                   const BodySnapshot* previous = nullptr)
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
    if (transform == nullptr)
    {
        return false;
    }
    const bool unchanged_output = previous != nullptr && previous->written_transform &&
        node.parent() == nullptr && body != nullptr &&
        body->settings().motion == schema::BodyMotion::dynamic &&
        transform->settings() == *previous->written_transform;
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
        if (!world_matrix(*transform, matrix) ||
            !decompose(matrix, output.position, output.rotation, output.scale))
        {
            return false;
        }
    }
    output.node_id = node.id();
    // 比较完整描述，不能用组件地址判断：FlatBuffers 的嵌套凸包仍可能被修改。
    const bool same_collider =
        previous != nullptr && previous->collider && *previous->collider == collider;
    const bool previously_valid = same_collider && previous->mesh == output.mesh &&
                                  previous->transient_mesh == output.transient_mesh &&
                                  previous->mesh_parts == output.mesh_parts;
    if (body != nullptr)
    {
        output.rigid_body = body->settings();
    }
    else
    {
        output.rigid_body.motion = schema::BodyMotion::static_body;
    }
    if ((output.rigid_body.motion == schema::BodyMotion::dynamic && node.parent() != nullptr) ||
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
        const bool same_group = reuse && group < previous->part_groups->size() &&
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
        const auto* old_shape = slot < before.part_shapes->size()
                                    ? (*before.part_shapes)[slot].GetPtr()
                                    : nullptr;
        const auto* new_shape = slot < after.part_shapes->size()
                                    ? (*after.part_shapes)[slot].GetPtr()
                                    : nullptr;
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

bool active_collision(const Node& node)
{
    const auto* collider = node.get_component<Collider>();
    const auto* body = node.get_component<RigidBody>();
    return collider != nullptr && collider->enabled() && !collider->settings().is_trigger &&
           (body == nullptr || body->enabled()) && active_transform(node);
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

struct PhysicsSystem::State final : JPH::ContactListener
{
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
    std::uint64_t scene_id = 0;
    std::uint64_t frame = 0;
    double accumulated = 0.0;
    std::mutex impact_mutex;
    std::map<std::pair<std::uint64_t, std::uint64_t>, PhysicsImpact> pending_impacts;
    std::vector<PhysicsImpact> impacts;

    void collect_impact(const JPH::Body& first, const JPH::Body& second,
                        const JPH::ContactManifold& manifold, const JPH::ContactSettings& settings)
    {
        ENGINE_PROFILE_ZONE("Physics.collect_impact");
        const auto first_node = first.GetUserData();
        const auto second_node = second.GetUserData();
        if (settings.mIsSensor || first.IsSensor() || second.IsSensor() || first_node == 0 ||
            second_node == 0 || first_node == second_node ||
            manifold.mRelativeContactPointsOn1.empty() ||
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
            JPH::EstimateCollisionResponse(first, second, manifold, estimate,
                                           settings.mCombinedFriction, settings.mCombinedRestitution,
                                           configuration.mMinVelocityForRestitution,
                                           configuration.mNumVelocitySteps);
        }
        float impulse = 0;
        for (const auto& item : estimate.mImpulses)
        {
            impulse += std::max(0.0F, item.mContactImpulse);
        }
        if (!std::isfinite(impulse) || impulse <= 0)
        {
            return;
        }
        PhysicsImpact impact;
        impact.frame = frame;
        impact.scene_id = scene_id;
        impact.node_a = std::min(first_node, second_node);
        impact.node_b = std::max(first_node, second_node);
        impact.point = {point.GetX(), point.GetY(), point.GetZ()};
        impact.normal = vector(first_node < second_node ? manifold.mWorldSpaceNormal
                                                        : -manifold.mWorldSpaceNormal);
        impact.closing_speed = closing_speed;
        impact.estimated_impulse = impulse;
        const std::lock_guard lock(impact_mutex);
        auto [entry, inserted] =
            pending_impacts.try_emplace({impact.node_a, impact.node_b}, impact);
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
        collect_impact(first, second, manifold, settings);
    }

    void OnContactPersisted(const JPH::Body& first, const JPH::Body& second,
                            const JPH::ContactManifold& manifold,
                            JPH::ContactSettings& settings) override
    {
        collect_impact(first, second, manifold, settings);
    }

    void clear() noexcept
    {
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
        scene_id = 0;
        accumulated = 0.0;
        pending_impacts.clear();
        impacts.clear();
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
        auto previous_settings = found != bodies.end() ? found->second.snapshot.rigid_body
                                                       : snapshot.rigid_body;
        previous_settings.linear_velocity = snapshot.rigid_body.linear_velocity;
        previous_settings.angular_velocity = snapshot.rigid_body.angular_velocity;
        const bool rebuild = found == bodies.end() ||
                             (found->second.snapshot.collider != snapshot.collider &&
                              *found->second.snapshot.collider != *snapshot.collider) ||
                             found->second.snapshot.mesh != snapshot.mesh ||
                             found->second.snapshot.transient_mesh != snapshot.transient_mesh ||
                             found->second.snapshot.mesh_parts != snapshot.mesh_parts ||
                             previous_settings != snapshot.rigid_body ||
                             found->second.snapshot.shape != snapshot.shape;
        if (!rebuild)
        {
            const bool moved_static = !moving &&
                (found->second.snapshot.position != snapshot.position ||
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
                interface.SetLinearAndAngularVelocity(
                    found->second.body, vector(snapshot.rigid_body.linear_velocity),
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
            affected = local_change ? changed_bounds(previous, snapshot)
                                    : interface.GetTransformedShape(found->second.body)
                                          .GetWorldSpaceBounds();
        }
        if (found != bodies.end() && !moving &&
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
                interface.RemoveBody(found->second.body);
                interface.DestroyBody(found->second.body);
                found->second = Entry{snapshot, body};
            }
            else
            {
                found = bodies.emplace(snapshot.node_id, Entry{snapshot, body}).first;
            }
        }
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
                configuration.mEnhancedInternalEdgeRemoval = true;
                configuration.mMaxSlopeAngle =
                    settings.max_slope_degrees * std::numbers::pi_v<float> / 180.0F;
                configuration.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -settings.radius);
                CharacterEntry entry;
                entry.snapshot = snapshot;
                entry.character =
                    new JPH::CharacterVirtual(&configuration, JPH::RVec3(snapshot.position),
                                              JPH::Quat::sIdentity(), snapshot.node_id, &physics);
                entry.character->SetCharacterVsCharacterCollision(&character_collisions);
                character_collisions.Add(entry.character.GetPtr());
                found = characters.emplace(snapshot.node_id, std::move(entry)).first;
                found->second.character->RefreshContacts(
                    physics.GetDefaultBroadPhaseLayerFilter(moving_layer),
                    physics.GetDefaultLayerFilter(moving_layer), SolidBodyFilter{}, {}, allocator);
            }
            else if (snapshot.reset_velocity || !found->second.character->GetPosition().IsClose(
                                                    JPH::RVec3(snapshot.position), 1.0e-10F))
            {
                found->second.character->SetPosition(JPH::RVec3(snapshot.position));
                found->second.character->SetLinearVelocity(JPH::Vec3::sZero());
                found->second.external_velocity = JPH::Vec3::sZero();
                found->second.character->RefreshContacts(
                    physics.GetDefaultBroadPhaseLayerFilter(moving_layer),
                    physics.GetDefaultLayerFilter(moving_layer), SolidBodyFilter{}, {}, allocator);
            }
            found->second.snapshot = snapshot;
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
    state_->physics.SetContactListener(state_.get());
    return true;
}

void PhysicsSystem::clear() noexcept
{
    if (auto* scene = world_ != nullptr ? world_->scene() : nullptr)
    {
        for (const auto& node : scene->nodes())
        {
            if (auto* motor = node->get_component<CharacterMotor>())
            {
                motor->desired_velocity_ = {};
                motor->velocity_ = {};
                motor->pending_velocity_ = {};
                motor->pending_jump_ = 0.0F;
                motor->grounded_ = false;
                motor->reset_pending_ = false;
            }
        }
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
    if (!state_ || !finite(position) || !finite(direction) ||
        position.Abs().ReduceMax() > 10000000.0F || direction.Abs().ReduceMax() > 100000.0F ||
        !std::isfinite(radius) || radius < 0.0001F || radius > 1000.0F)
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
    state_->physics.GetNarrowPhaseQuery().CastShape(
        cast, settings, JPH::RVec3::sZero(), collector,
        state_->physics.GetDefaultBroadPhaseLayerFilter(moving_layer),
        state_->physics.GetDefaultLayerFilter(moving_layer), SolidBodyFilter{});
    fraction = collector.HadHit() ? std::clamp(collector.mHit.mFraction, 0.0F, 1.0F) : 1.0F;
    return true;
}

bool PhysicsSystem::cast_ray(const Collider& collider, const Vec3& origin, const Vec3& displacement,
                             float& fraction) const
{
    fraction = 0;
    const auto* scene = world_ != nullptr ? world_->scene() : nullptr;
    const auto& node = collider.node();
    const auto position = vector(origin);
    const auto direction = vector(displacement);
    if (!state_ || !scene || &node.scene() != scene ||
        node.get_component<Collider>() != &collider || !finite(position) || !finite(direction) ||
        position.Abs().ReduceMax() > 10000000.0F || direction.Abs().ReduceMax() > 100000.0F)
    {
        return false;
    }
    fraction = 1;
    const auto body = state_->bodies.find(node.id());
    if (state_->scene_id != scene->id() || body == state_->bodies.end() || !active_collision(node))
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

std::span<const PhysicsImpact> PhysicsSystem::impacts() const noexcept
{
    const auto* scene = world_ != nullptr ? world_->scene() : nullptr;
    if (!state_)
    {
        return {};
    }
    if (!scene || scene->id() != state_->scene_id)
    {
        state_->impacts.clear();
        return {};
    }
    std::erase_if(state_->impacts,
                  [&](const auto& impact)
                  {
                      const auto* a = scene->find_node(impact.node_a);
                      const auto* b = scene->find_node(impact.node_b);
                      return impact.scene_id != scene->id() || !a || !b || !active_collision(*a) ||
                             !active_collision(*b) || !state_->bodies.contains(impact.node_a) ||
                             !state_->bodies.contains(impact.node_b);
                  });
    return state_->impacts;
}

bool PhysicsSystem::set_collider_data(Collider& collider, const schema::ColliderDataT& settings)
{
    ENGINE_PROFILE_ZONE("Physics.set_collider");
    auto* scene = world_ != nullptr ? world_->scene() : nullptr;
    auto& node = collider.node();
    if (state_ == nullptr || scene == nullptr || &node.scene() != scene ||
        node.get_component<Collider>() != &collider || !Collider::valid_settings(settings))
    {
        return false;
    }
    const auto* rigid = node.get_component<RigidBody>();
    const auto* motor = node.get_component<CharacterMotor>();
    if (motor != nullptr && motor->enabled() && collider.enabled())
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
        collider.enabled() && active_transform(node) && (rigid == nullptr || rigid->enabled());
    snapshot.shape_scale = snapshot.scale;
    if (active && !make_shape(*snapshot.collider, mesh, snapshot.shape_scale, snapshot.shape))
    {
        return false;
    }
    if (state_->scene_id != scene->id())
    {
        state_->clear();
        state_->scene_id = scene->id();
    }
    if (active && !state_->publish_body(snapshot))
    {
        return false;
    }
    if (!active)
    {
        state_->remove_body(node.id());
    }
    *collider.mutable_data().payload.AsColliderData() = *snapshot.collider;
    collider.mesh_ = std::move(snapshot.mesh);
    collider.transient_mesh_.reset();
    collider.mesh_parts_.reset();
    return true;
}

bool PhysicsSystem::set_mesh_data(Collider& collider, const schema::MeshDataT& mesh)
{
    ENGINE_PROFILE_ZONE("Physics.set_mesh");
    auto* scene = world_ != nullptr ? world_->scene() : nullptr;
    auto& node = collider.node();
    if (state_ == nullptr || scene == nullptr || &node.scene() != scene ||
        node.get_component<Collider>() != &collider || mesh.schema_version != 1)
    {
        return false;
    }
    const auto* rigid = node.get_component<RigidBody>();
    const auto* motor = node.get_component<CharacterMotor>();
    if ((rigid != nullptr && rigid->settings().motion != schema::BodyMotion::static_body) ||
        (motor != nullptr && motor->enabled() && collider.enabled()))
    {
        return false;
    }
    BodySnapshot snapshot;
    snapshot.mesh_parts = collider.mesh_parts_;
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
    if (!empty &&
        !make_shape(*snapshot.collider, snapshot.transient_mesh.get(), snapshot.shape_scale,
                     snapshot.shape))
    {
        return false;
    }
    const bool active = !empty && collider.enabled() && active_transform(node) &&
                        (rigid == nullptr || rigid->enabled());
    if (state_->scene_id != scene->id())
    {
        state_->clear();
        state_->scene_id = scene->id();
    }
    if (active && !state_->publish_body(snapshot))
    {
        return false;
    }
    if (!active)
    {
        state_->remove_body(node.id());
    }
    *collider.mutable_data().payload.AsColliderData() = *snapshot.collider;
    collider.mesh_.reset();
    collider.transient_mesh_ = std::move(snapshot.transient_mesh);
    collider.mesh_parts_.reset();
    return true;
}

bool PhysicsSystem::set_mesh_parts(Collider& collider, const MeshCollisionData& parts)
{
    ENGINE_PROFILE_ZONE("Physics.set_mesh_parts");
    auto* scene = world_ != nullptr ? world_->scene() : nullptr;
    auto& node = collider.node();
    if (state_ == nullptr || scene == nullptr || &node.scene() != scene ||
        node.get_component<Collider>() != &collider)
    {
        return false;
    }
    const auto* rigid = node.get_component<RigidBody>();
    const auto* motor = node.get_component<CharacterMotor>();
    if ((rigid != nullptr && rigid->settings().motion != schema::BodyMotion::static_body) ||
        (motor != nullptr && motor->enabled() && collider.enabled()))
    {
        return false;
    }
    BodySnapshot snapshot;
    snapshot.mesh_parts = collider.mesh_parts_;
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
    const auto* previous = state_->scene_id == scene->id() && found != state_->bodies.end()
                               ? &found->second.snapshot
                               : nullptr;
    if (!prepare_mesh_parts(snapshot, previous))
    {
        return false;
    }
    const bool active = snapshot.shape != nullptr && collider.enabled() && active_transform(node) &&
                        (rigid == nullptr || rigid->enabled());
    if (state_->scene_id != scene->id())
    {
        state_->clear();
        state_->scene_id = scene->id();
    }
    if (active && !state_->publish_body(snapshot))
    {
        return false;
    }
    if (!active)
    {
        state_->remove_body(node.id());
    }
    *collider.mutable_data().payload.AsColliderData() = *snapshot.collider;
    collider.mesh_.reset();
    collider.transient_mesh_.reset();
    collider.mesh_parts_ = std::move(snapshot.mesh_parts);
    return true;
}

bool PhysicsSystem::on_tick(double delta_time)
{
    ENGINE_PROFILE_ZONE("Physics.tick");
    auto* scene = world_ != nullptr ? world_->scene() : nullptr;
    if (!state_)
    {
        return false;
    }
    state_->impacts.clear();
    state_->pending_impacts.clear();
    if (state_->frame == std::numeric_limits<std::uint64_t>::max())
    {
        clear();
        return false;
    }
    ++state_->frame;
    if (scene == nullptr)
    {
        clear();
        return true;
    }
    if (state_->scene_id != scene->id())
    {
        // 旧场景已经由 WorldSystem 替换；不能清掉新组件在首个物理帧前提交的请求。
        state_->clear();
        state_->scene_id = scene->id();
    }
    auto& snapshots = state_->body_snapshots;
    auto& character_snapshots = state_->character_snapshots;
    snapshots.clear();
    character_snapshots.clear();
    auto& interface = state_->physics.GetBodyInterface();
    {
        ENGINE_PROFILE_ZONE("Physics.prepare");
        for (const auto& node : scene->nodes())
        {
            if (auto* motor = node->get_component<CharacterMotor>())
            {
                if (motor->enabled() && active_transform(*node))
                {
                    CharacterSnapshot snapshot;
                    if (!read_character(*node, *motor, snapshot))
                    {
                        std::fputs(
                            "Physics rejects character pose or conflicting body components\n",
                            stderr);
                        clear();
                        return false;
                    }
                    snapshot.instance_id = motor->instance_id();
                    snapshot.reset_velocity = motor->reset_pending_;
                    character_snapshots.push_back(std::move(snapshot));
                }
                else
                {
                    motor->desired_velocity_ = {};
                    motor->velocity_ = {};
                    motor->pending_velocity_ = {};
                    motor->pending_jump_ = 0.0F;
                    motor->grounded_ = false;
                    motor->reset_pending_ = false;
                }
            }
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
            snapshot.mesh = collider->mesh_;
            snapshot.transient_mesh = collider->transient_mesh_;
            snapshot.mesh_parts = collider->mesh_parts_;
            const auto existing = state_->bodies.find(node->id());
            const auto* previous =
                existing != state_->bodies.end() ? &existing->second.snapshot : nullptr;
            if (!read_snapshot(*node, collider->settings(), collider->mesh_data(), rigid_body,
                               snapshot, previous))
            {
                std::fputs(
                    "Physics rejects invalid settings, shear, reflection or a dynamic parent\n",
                    stderr);
                clear();
                return false;
            }
            if (snapshot.transient_mesh && snapshot.transient_mesh->vertices.empty() &&
                snapshot.transient_mesh->indices.empty())
            {
                continue;
            }
            if (snapshot.mesh_parts)
            {
                if (!prepare_mesh_parts(snapshot, previous))
                {
                    std::fputs("Physics cannot create segmented mesh collision\n", stderr);
                    clear();
                    return false;
                }
                if (snapshot.shape == nullptr)
                {
                    continue;
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
                if (!make_shape(*snapshot.collider, collider->mesh_data(), snapshot.shape_scale,
                                snapshot.shape))
                {
                    std::fputs("Physics cannot create collider with the requested world scale\n",
                               stderr);
                    clear();
                    return false;
                }
            }
            snapshots.push_back(std::move(snapshot));
        }
        if (snapshots.size() + character_snapshots.size() > maximum_bodies ||
            !state_->synchronize(snapshots) || !state_->synchronize_characters(character_snapshots))
        {
            clear();
            return false;
        }
        snapshots.clear();
        character_snapshots.clear();
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
    }
    ENGINE_PROFILE_PLOT("Physics.bodies", static_cast<double>(state_->bodies.size()));
    ENGINE_PROFILE_PLOT("Physics.characters", static_cast<double>(state_->characters.size()));
    ENGINE_PROFILE_PLOT("Physics.active_bodies",
                        static_cast<double>(state_->physics.GetNumActiveBodies(JPH::EBodyType::RigidBody)));
    state_->accumulated = std::min(state_->accumulated + delta_time, fixed_step * maximum_steps);
    bool advanced = false;
    for (unsigned int step = 0; step < maximum_steps && state_->accumulated + 1.0e-12 >= fixed_step;
         ++step)
    {
        ENGINE_PROFILE_ZONE("Physics.fixed_step");
        // 复用 Jolt CharacterVirtualTest 的速度/地面逻辑与 ExtendedUpdate 台阶求解。
        // 来源 Jolt Physics v5.4.0, Jorrit Rouwe, MIT。
        {
            ENGINE_PROFILE_ZONE("Physics.characters_step");
            for (auto& [id, entry] : state_->characters)
            {
                auto* node = scene->find_node(id);
                auto* motor = node != nullptr ? node->get_component<CharacterMotor>() : nullptr;
                if (motor == nullptr)
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
                    if (motor->pending_jump_ > 0.0F)
                    {
                        velocity += JPH::Vec3(0, motor->pending_jump_, 0);
                    }
                }
                motor->pending_jump_ = 0.0F;
                const auto delta = vector(motor->pending_velocity_);
                motor->pending_velocity_ = {};
                entry.external_velocity += JPH::Vec3(delta.GetX(), 0, delta.GetZ());
                const auto gravity =
                    state_->physics.GetGravity() * motor->settings().gravity_factor;
                velocity += gravity * static_cast<float>(fixed_step) +
                            vector(motor->desired_velocity_) + entry.external_velocity +
                            JPH::Vec3(0, delta.GetY(), 0);
                if (!finite(velocity) || !finite(entry.external_velocity))
                {
                    clear();
                    return false;
                }
                character.SetLinearVelocity(velocity);
                JPH::CharacterVirtual::ExtendedUpdateSettings settings;
                settings.mStickToFloorStepDown =
                    JPH::Vec3(0, -motor->settings().ground_snap_distance, 0);
                settings.mWalkStairsStepUp = JPH::Vec3(0, motor->settings().step_height, 0);
                character.ExtendedUpdate(
                    static_cast<float>(fixed_step), gravity, settings,
                    state_->physics.GetDefaultBroadPhaseLayerFilter(moving_layer),
                    state_->physics.GetDefaultLayerFilter(moving_layer), SolidBodyFilter{}, {},
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
                    std::exp(-motor->settings().external_damping * static_cast<float>(fixed_step));
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
        state_->accumulated = std::max(0.0, state_->accumulated - fixed_step);
        advanced = true;
    }
    {
        ENGINE_PROFILE_ZONE("Physics.sync_results");
        for (auto& [id, entry] : state_->characters)
        {
            auto* node = scene->find_node(id);
            auto* motor = node != nullptr ? node->get_component<CharacterMotor>() : nullptr;
            auto* transform = node != nullptr ? node->get_component<Transform>() : nullptr;
            if (motor == nullptr || transform == nullptr)
            {
                clear();
                return false;
            }
            auto pose = transform->settings();
            pose.position = vector(JPH::Vec3(entry.character->GetPosition()));
            if (!transform->set_settings(pose))
            {
                clear();
                return false;
            }
            motor->velocity_ = vector(entry.character->GetLinearVelocity());
            motor->reset_pending_ = false;
            motor->grounded_ =
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
            if (advanced && entry.snapshot.rigid_body.motion == schema::BodyMotion::dynamic)
            {
                entry.snapshot.position = JPH::Vec3(position);
                entry.snapshot.rotation = rotation;
                entry.snapshot.written_transform = node->get_component<Transform>()->settings();
            }
            JPH::Vec3 linear_velocity;
            JPH::Vec3 angular_velocity;
            interface.GetLinearAndAngularVelocity(entry.body, linear_velocity, angular_velocity);
            auto settings = body->settings();
            settings.linear_velocity = vector(linear_velocity);
            settings.angular_velocity = vector(angular_velocity);
            if (!body->set_settings(settings))
            {
                clear();
                return false;
            }
            entry.snapshot.rigid_body = settings;
        }
        state_->impacts.reserve(state_->pending_impacts.size());
        for (const auto& [pair, impact] : state_->pending_impacts)
        {
            const auto* a = scene->find_node(pair.first);
            const auto* b = scene->find_node(pair.second);
            if (a && b && active_collision(*a) && active_collision(*b))
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
