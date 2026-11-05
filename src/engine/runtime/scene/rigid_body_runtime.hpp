#pragma once

#include "runtime/scene/component_runtime.hpp"
#include "runtime/scene/rigid_body.hpp"

namespace engine
{
struct RigidBodyRuntime
{
    Vec3 pending_impulse_{};
};
} // namespace engine
