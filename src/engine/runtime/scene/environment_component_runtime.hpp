#pragma once

#include "runtime/resource/resource_reference.hpp"

#include <array>

namespace engine
{
struct EnvironmentRuntime
{
    std::array<resource_detail::ResourceReference, 3> ibl;
};
} // namespace engine
