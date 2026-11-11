#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <string_view>

namespace engine::schema
{
struct ModelingPrimitiveDataT;
}

namespace engine::plugins::modeling::detail
{
[[nodiscard]] bool patch_primitive(std::string_view shape, const nlohmann::json& parameters,
                                   schema::ModelingPrimitiveDataT& options, std::string& error);
nlohmann::json primitive_json(const schema::ModelingPrimitiveDataT& options);
nlohmann::json shape_contracts(bool primitives_only);
} // namespace engine::plugins::modeling::detail
