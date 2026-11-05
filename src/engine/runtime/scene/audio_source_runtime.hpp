#pragma once

#include "runtime/scene/audio_source.hpp"
#include "runtime/scene/component_runtime.hpp"

namespace engine
{
struct AudioSourceRuntime
{
    std::shared_ptr<const Resource> clip_;
    std::uint64_t revision_ = 0;
    AudioPlayback playback_ = AudioPlayback::stopped;
    bool awake_pending_ = true;
};
} // namespace engine
