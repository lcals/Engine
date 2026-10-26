#pragma once

#include <string>
#include <string_view>

namespace cpp_template {

// Returns a greeting; throws std::invalid_argument when name is empty.
[[nodiscard]] std::string greeting(std::string_view name);

} // namespace cpp_template
