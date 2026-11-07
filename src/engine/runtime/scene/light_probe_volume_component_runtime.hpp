#pragma once

#include "runtime/resource/resource_reference.hpp"

#include "runtime/scene/component_runtime.hpp"
#include "runtime/scene/light_probe_volume_component.hpp"

namespace engine
{
struct LightProbeVolumeComponentRuntime
{
    resource_detail::ResourceReference resource_;
};
} // namespace engine
