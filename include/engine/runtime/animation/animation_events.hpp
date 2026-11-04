#pragma once

#include "runtime/schema/animation_generated.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace engine
{
struct AnimationEvent
{
    std::uint64_t scene_id = 0;
    std::uint64_t node_id = 0;
    std::uint64_t component_instance_id = 0;
    std::uint64_t playback_id = 0;
    std::string animation_guid;
    std::uint64_t resource_generation = 0;
    std::uint64_t track_revision = 0;
    std::uint64_t cycle = 0;
    schema::AnimationEventMarkerDataT marker;
};
using AnimationEventSink = std::function<bool(std::span<const AnimationEvent>)>;
} // namespace engine
