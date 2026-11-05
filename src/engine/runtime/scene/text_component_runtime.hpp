#pragma once

#include "runtime/scene/component_runtime.hpp"
#include "runtime/scene/text_component.hpp"

namespace engine
{
struct TextComponentRuntime
{
    std::shared_ptr<const Resource> font_;
    std::shared_ptr<const Resource> shader_;
};
} // namespace engine
