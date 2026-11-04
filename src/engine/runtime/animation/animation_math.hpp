#pragma once

#include "runtime/animation/animation_binding.hpp"
#include "runtime/scene/transform.hpp"

#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <span>

namespace engine::animation_detail
{
inline ozz::math::Float4x4 load_matrix(std::span<const float, 16> value)
{
    ozz::math::Float4x4 output;
    for (std::size_t column = 0; column < 4; ++column)
    {
        output.cols[column] = ozz::math::simd_float4::LoadPtrU(value.data() + column * 4);
    }
    return output;
}
inline void store_matrix(const ozz::math::Float4x4& value, std::array<float, 16>& output)
{
    for (std::size_t column = 0; column < 4; ++column)
    {
        ozz::math::StorePtrU(value.cols[column], output.data() + column * 4);
    }
}
inline bool invert_matrix(const ozz::math::Float4x4& value, ozz::math::Float4x4& output)
{
    ozz::math::SimdInt4 invertible;
    output = ozz::math::Invert(value, &invertible);
    return ozz::math::AreAllTrue1(invertible);
}
inline ozz::math::SimdFloat4 simd_point(const Vec3& value)
{
    return ozz::math::simd_float4::Load(static_cast<float>(value.x()),
                                        static_cast<float>(value.y()),
                                        static_cast<float>(value.z()), 0.0F);
}
inline ozz::math::Float4x4 pose_matrix(const schema::JointLocalPose& value)
{
    return ozz::math::Float4x4::FromAffine(
        simd_point(value.translation()),
        ozz::math::simd_float4::Load(
            static_cast<float>(value.rotation().x()), static_cast<float>(value.rotation().y()),
            static_cast<float>(value.rotation().z()), static_cast<float>(value.rotation().w())),
        simd_point(value.scale()));
}
inline bool readable_joint_paths(const schema::SkeletonDataT& skeleton,
                                 std::vector<std::string>& output)
{
    std::vector<std::string> validated;
    output.clear();
    if (!joint_paths(skeleton, validated))
    {
        return false;
    }
    for (const auto& joint : skeleton.joints)
    {
        if (joint->name.empty() || joint->name.find('/') != std::string::npos)
        {
            output.clear();
            return false;
        }
        output.push_back(joint->parent < 0 ? joint->name
                                           : output[joint->parent] + '/' + joint->name);
    }
    return true;
}
inline ozz::math::Float4x4 authored_matrix(const TransformSettings& value)
{
    constexpr double radians = std::numbers::pi / 180.0;
    const double sx = std::sin(value.rotation.x() * radians);
    const double cx = std::cos(value.rotation.x() * radians);
    const double sy = std::sin(value.rotation.y() * radians);
    const double cy = std::cos(value.rotation.y() * radians);
    const double sz = std::sin(value.rotation.z() * radians);
    const double cz = std::cos(value.rotation.z() * radians);
    std::array<float, 16> local{static_cast<float>(cz * cy * value.scale.x()),
                                static_cast<float>(sz * cy * value.scale.x()),
                                static_cast<float>(-sy * value.scale.x()),
                                0,
                                static_cast<float>((cz * sy * sx - sz * cx) * value.scale.y()),
                                static_cast<float>((sz * sy * sx + cz * cx) * value.scale.y()),
                                static_cast<float>(cy * sx * value.scale.y()),
                                0,
                                static_cast<float>((cz * sy * cx + sz * sx) * value.scale.z()),
                                static_cast<float>((sz * sy * cx - cz * sx) * value.scale.z()),
                                static_cast<float>(cy * cx * value.scale.z()),
                                0,
                                static_cast<float>(value.position.x()),
                                static_cast<float>(value.position.y()),
                                static_cast<float>(value.position.z()),
                                1};
    return load_matrix(local);
}
// Ozz分解后回构验证，拒绝剪切/退化；Euler与 WorldSystem 既有 Rz*Ry*Rx 一致。
inline bool decompose_authored(const ozz::math::Float4x4& matrix, TransformSettings& output)
{
    output = {};
    ozz::math::Float3 translation, scale;
    ozz::math::Quaternion rotation;
    if (!ozz::math::ToAffine(matrix, &translation, &rotation, &scale) || scale.x == 0 ||
        scale.y == 0 || scale.z == 0)
    {
        return false;
    }
    const auto normalized =
        ozz::math::Float4x4::FromAffine(ozz::math::Float3{}, rotation, ozz::math::Float3{1, 1, 1});
    std::array<float, 16> m;
    store_matrix(normalized, m);
    const double y = std::atan2(-static_cast<double>(m[2]),
                                std::hypot(static_cast<double>(m[0]), static_cast<double>(m[1])));
    const bool singular = std::abs(std::cos(y)) < 1.0e-6;
    const double x = singular ? std::atan2(-m[9], m[5]) : std::atan2(m[6], m[10]);
    const double z = singular ? 0.0 : std::atan2(m[1], m[0]);
    constexpr double degrees = 180.0 / std::numbers::pi;
    TransformSettings candidate;
    candidate.position = Vec3(translation.x, translation.y, translation.z);
    candidate.rotation = Vec3(x * degrees, y * degrees, z * degrees);
    candidate.scale = Vec3(scale.x, scale.y, scale.z);
    std::array<float, 16> original, reconstructed;
    store_matrix(matrix, original);
    store_matrix(authored_matrix(candidate), reconstructed);
    for (std::size_t i = 0; i < original.size(); ++i)
    {
        if (!std::isfinite(original[i]) || std::abs(original[i] - reconstructed[i]) >
                                               2.0e-4F * std::max(1.0F, std::abs(original[i])))
        {
            return false;
        }
    }
    output = candidate;
    return true;
}
inline float simd_lane(ozz::math::SimdFloat4 value, std::size_t lane)
{
    float lanes[4];
    ozz::math::StorePtrU(value, lanes);
    return lanes[lane];
}
inline void set_simd_lane(ozz::math::SimdFloat4& value, std::size_t lane, float scalar)
{
    float lanes[4];
    ozz::math::StorePtrU(value, lanes);
    lanes[lane] = scalar;
    value = ozz::math::simd_float4::LoadPtrU(lanes);
}
inline schema::JointLocalPose local_pose(const ozz::math::SoaTransform& value, std::size_t lane)
{
    return schema::JointLocalPose(
        Vec3(simd_lane(value.translation.x, lane), simd_lane(value.translation.y, lane),
             simd_lane(value.translation.z, lane)),
        Vec4(simd_lane(value.rotation.x, lane), simd_lane(value.rotation.y, lane),
             simd_lane(value.rotation.z, lane), simd_lane(value.rotation.w, lane)),
        Vec3(simd_lane(value.scale.x, lane), simd_lane(value.scale.y, lane),
             simd_lane(value.scale.z, lane)));
}
inline void set_local_pose(ozz::math::SoaTransform& output, std::size_t lane,
                           const schema::JointLocalPose& value)
{
    set_simd_lane(output.translation.x, lane, static_cast<float>(value.translation().x()));
    set_simd_lane(output.translation.y, lane, static_cast<float>(value.translation().y()));
    set_simd_lane(output.translation.z, lane, static_cast<float>(value.translation().z()));
    set_simd_lane(output.rotation.x, lane, static_cast<float>(value.rotation().x()));
    set_simd_lane(output.rotation.y, lane, static_cast<float>(value.rotation().y()));
    set_simd_lane(output.rotation.z, lane, static_cast<float>(value.rotation().z()));
    set_simd_lane(output.rotation.w, lane, static_cast<float>(value.rotation().w()));
    set_simd_lane(output.scale.x, lane, static_cast<float>(value.scale().x()));
    set_simd_lane(output.scale.y, lane, static_cast<float>(value.scale().y()));
    set_simd_lane(output.scale.z, lane, static_cast<float>(value.scale().z()));
}
} // namespace engine::animation_detail
