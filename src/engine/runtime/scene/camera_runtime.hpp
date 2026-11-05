#pragma once

#include "runtime/scene/camera.hpp"
#include "runtime/scene/component_runtime.hpp"

namespace engine
{
struct CameraRuntime
{
    std::vector<std::uint64_t> hidden_meshes_;
};
} // namespace engine
