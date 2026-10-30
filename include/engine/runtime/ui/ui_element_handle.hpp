#pragma once

#include <cstdint>

namespace engine
{
struct UiElementHandle
{
    std::uint64_t document_id = 0;
    std::uint64_t element_id = 0;
};
} // namespace engine
