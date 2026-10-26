#include "cpp_template/greeting.hpp"

#include <fmt/format.h>

#include <stdexcept>

namespace cpp_template {

std::string greeting(std::string_view name)
{
    if (name.empty()) {
        throw std::invalid_argument("name must not be empty");
    }

    return fmt::format("Hello, {}!", name);
}

} // namespace cpp_template
