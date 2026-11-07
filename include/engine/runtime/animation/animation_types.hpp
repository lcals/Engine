#pragma once

#include "runtime/scene/node.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace engine
{
enum class AnimationPlayback
{
    stopped,
    playing,
    paused
};
struct AnimationPlayRequest
{
    std::string animation_path;
    std::string retarget_profile_path;
    double fade_seconds = 0.0;
    float speed = 1.0F;
    bool loop = true;
    bool restart = true;
};
struct AnimatorPlaybackInfo
{
    AnimationPlayback playback = AnimationPlayback::stopped;
    double time = 0.0;
    double duration = 0.0;
    double normalized_time = 0.0;
    bool completed = false;
    bool transitioning = false;
    std::uint64_t playback_id = 0;
    std::string animation_guid;
    std::uint64_t resource_generation = 0;
    std::uint64_t track_revision = 0;
};
inline constexpr std::array<float, 16> animation_identity_matrix{1, 0, 0, 0, 0, 1, 0, 0,
                                                                 0, 0, 1, 0, 0, 0, 0, 1};
struct AnimationPoseTransform
{
    std::array<float, 16> model = animation_identity_matrix;
    std::array<float, 16> world = animation_identity_matrix;
    std::uint64_t pose_revision = 0;
};
enum class AnimationIkTargetKind
{
    point,
    node
};
enum class AnimationIkSpace
{
    model,
    world
};
struct AnimationIkTarget
{
    AnimationIkTargetKind kind = AnimationIkTargetKind::point;
    AnimationIkSpace space = AnimationIkSpace::world;
    Vec3 point;
    Node target_node;
    Vec3 local_offset;
    Vec3 pole;
    float weight = 0.0F;
    bool enabled = false;
};
} // namespace engine
