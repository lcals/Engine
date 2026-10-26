#include "engine/greeting.hpp"

#include <fmt/format.h>

#include <stdexcept>

namespace engine {

std::string greeting(std::string_view name)
{
    if (name.empty()) {
        throw std::invalid_argument("name must not be empty");
    }

    return fmt::format("Hello, {}!", name);
}

} // namespace engine
