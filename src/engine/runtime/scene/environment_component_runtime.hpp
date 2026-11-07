#pragma once

#include "runtime/resource/resource_reference.hpp"

#include <array>

namespace engine
{
struct EnvironmentRuntime
{
    std::array<ResourceReference, 3> ibl;
};
} // namespace engine
