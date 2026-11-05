#pragma once

#include "runtime/scene/component_runtime.hpp"
#include "runtime/scene/ui_component.hpp"

namespace engine
{
struct UiComponentRuntime
{
    std::uint64_t document_revision_ = 0;
    ResourcePtr document_;
};
} // namespace engine
