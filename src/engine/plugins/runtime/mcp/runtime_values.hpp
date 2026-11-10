#pragma once
#include "runtime/render/render_observation.hpp"
#include <filesystem>
#include <future>
#include <nlohmann/json.hpp>

namespace engine
{
class RuntimeHost;
class TaskSystem;
} // namespace engine

namespace engine::plugins::mcp
{
[[nodiscard]] nlohmann::json scene_load_value(const SceneLoadSnapshot& value);
[[nodiscard]] nlohmann::json frame_value(const RenderFrameSnapshot& value);
[[nodiscard]] nlohmann::json screenshot_value(const ScreenshotSnapshot& value);
[[nodiscard]] bool publish_observation(TaskSystem* tasks, std::future<bool>& pending,
                                       const std::filesystem::path& target,
                                       const nlohmann::json& status, bool finish);
[[nodiscard]] bool observation_command(RuntimeHost& host, const std::filesystem::path& cache,
                                       std::string_view command, const nlohmann::json& arguments,
                                       nlohmann::json& result, std::string& error);
} // namespace engine::plugins::mcp
