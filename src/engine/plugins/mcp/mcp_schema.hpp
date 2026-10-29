#pragma once

#include <nlohmann/json_fwd.hpp>

namespace engine::mcp_protocol
{

[[nodiscard]] bool valid_schema(const nlohmann::json& schema);
[[nodiscard]] bool valid_arguments(const nlohmann::json& schema, const nlohmann::json& arguments);

} // namespace engine::mcp_protocol
