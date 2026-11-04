#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string>

namespace engine
{
class WorldSystem;
}

namespace engine::editor
{
[[nodiscard]] bool query_scene(WorldSystem& world, const nlohmann::json& arguments,
                               nlohmann::json& result, std::string& error);
} // namespace engine::editor
