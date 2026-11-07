#pragma once

#include "runtime/resource/resource_reference.hpp"

#include "runtime/scene/component_runtime.hpp"
#include "runtime/scene/ui_component.hpp"

namespace engine
{
struct UiComponentRuntime
{
    std::uint64_t document_revision_ = 0;
    resource_detail::ResourceReference document_;
};
} // namespace engine
