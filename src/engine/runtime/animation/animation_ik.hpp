#pragma once

#include "runtime/scene/animator.hpp"

#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>

#include <vector>

namespace engine::animation_detail
{
struct CompiledSkeleton;
// 收集阶段将Node/组件身份解析为值；worker只使用本批次拥有的数值快照。
struct IkOperation
{
    std::size_t start = 0, middle = 0, end = 0;
    std::size_t attachment_joint = 0;
    bool two_bone = false;
    bool attachment_target = false;
    ozz::math::Float4x4 attachment_offset;
    ozz::math::SimdFloat4 target, pole, forward, up, offset, mid_axis;
    float weight = 0, soften = 1, twist = 0;
};
[[nodiscard]] bool set_ik_target(Animator actor, std::string_view name,
                                 const AnimationIkTarget& target, bool two_bone);
[[nodiscard]] bool collect_ik_operations(Animator actor, const CompiledSkeleton& skeleton,
                                         const ozz::math::Float4x4& model_to_world,
                                         std::vector<IkOperation>& output);
[[nodiscard]] bool apply_ik(const CompiledSkeleton& skeleton,
                            std::span<const IkOperation> operations,
                            std::vector<ozz::math::SoaTransform>& locals,
                            std::vector<ozz::math::Float4x4>& models);
} // namespace engine::animation_detail
