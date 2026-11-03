#pragma once

#include "runtime/ui/ui_element_handle.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace engine
{
// Transient observation of the actual RmlUi document, not persistent scene data.
struct UiElementInfo
{
    UiElementHandle handle;
    UiElementHandle parent;
    std::string tag;
    std::string id;
    std::string text;
    std::string value;
    std::string classes;
    std::array<float, 4> bounds{};
    std::uint32_t child_count = 0;
    bool visible = false;
    bool enabled = false;
    bool focused = false;
};
} // namespace engine
