#pragma once

#include "runtime/scene/component_runtime.hpp"
#include "runtime/scene/light_probe_volume_component.hpp"

namespace engine
{
struct LightProbeVolumeComponentRuntime
{
    std::shared_ptr<const Resource> resource_;
};
} // namespace engine
