#pragma once

#include <filesystem>
#include <string>

namespace engine::plugins::mcp
{

struct McpTarget
{
    std::filesystem::path runtime_dir;
    std::filesystem::path adb_executable = "adb";
    std::string android_serial;
    std::string android_package;
};

} // namespace engine::plugins::mcp
