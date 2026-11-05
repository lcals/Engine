#pragma once

#include "runtime/scene/bone_attachment.hpp"
#include "runtime/scene/component_runtime.hpp"

namespace engine
{
struct BoneAttachmentRuntime
{
    Animator source;
    std::uint64_t skeleton_generation = 0;
    std::uint64_t source_pose_revision = 0;
    std::uint32_t resolved_socket_index = 0;
    bool following = false;
    bool binding_pending = false;
};
} // namespace engine
