#pragma once

#include "runtime/schema/animation_pose_generated.h"
#include "runtime/schema/animation_retarget_generated.h"
#include "runtime/schema/skeleton_generated.h"

#include <memory>
#include <span>
#include <string>

namespace engine
{

class AnimationRetargetBinding
{
public:
    virtual ~AnimationRetargetBinding() noexcept = default;
    // Immutable binding; concurrent calls use distinct, nonoverlapping output buffers.
    // On failure, target_local remains unchanged. Joint order is SkeletonData order.
    [[nodiscard]] virtual bool retarget(std::span<const schema::JointLocalPose> source_local,
                                        std::span<schema::JointLocalPose> target_local) const = 0;
};

class AnimationRetargeter
{
public:
    virtual ~AnimationRetargeter() noexcept = default;
    // Synchronous preparation. Inputs are borrowed only for this call.
    // Failure clears output and reports error without changing an active binding.
    [[nodiscard]] virtual bool prepare(const schema::AnimationRetargetProfileDataT& profile,
                                       const schema::SkeletonDataT& source,
                                       const schema::SkeletonDataT& target,
                                       std::shared_ptr<const AnimationRetargetBinding>& output,
                                       std::string& error) = 0;
};

using AnimationRetargeterFactory = std::unique_ptr<AnimationRetargeter> (*)();

} // namespace engine
