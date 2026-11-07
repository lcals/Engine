#pragma once

#include "runtime/resource/resource_reference.hpp"

#include "plugins/runtime/destruction/destruction_component.hpp"
#include "runtime/scene/component_runtime.hpp"

namespace engine::plugins::destruction
{
struct DestructionComponentRuntime
{
    ResourceReference resource_;
    ResourceReference material_;
    DestructionGraph graph_;
    std::shared_ptr<const std::uint8_t> lifetime_ = std::make_shared<const std::uint8_t>(0);
    std::uint64_t revision_ = 0;
    bool ready_ = false;
    bool pending_ = false;
    std::string last_error_;
};
} // namespace engine::plugins::destruction
