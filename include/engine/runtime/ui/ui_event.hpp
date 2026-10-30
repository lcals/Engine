#pragma once

#include "runtime/ui/ui_element_handle.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace engine
{
struct UiEvent
{
    std::uint64_t node_id = 0;
    std::string element_id;
    std::string type;
    std::uint64_t scene_id = 0;
    UiElementHandle target;
    std::vector<UiElementHandle> path;
    std::string value;
};
} // namespace engine
