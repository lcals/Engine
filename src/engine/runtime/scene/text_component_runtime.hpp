#pragma once

#include "runtime/resource/resource_reference.hpp"

#include "runtime/scene/component_runtime.hpp"
#include "runtime/scene/text_component.hpp"

namespace engine
{
struct TextComponentRuntime
{
    resource_detail::ResourceReference font_;
    resource_detail::ResourceReference shader_;
};
} // namespace engine
