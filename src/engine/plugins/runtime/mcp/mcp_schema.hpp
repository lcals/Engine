#pragma once

#include <nlohmann/json_fwd.hpp>

namespace engine::plugins::mcp::protocol
{

[[nodiscard]] bool valid_schema(const nlohmann::json& schema);
[[nodiscard]] bool valid_arguments(const nlohmann::json& schema, const nlohmann::json& arguments);

} // namespace engine::plugins::mcp::protocol
