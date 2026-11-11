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
    std::string ios_udid;
    std::string ios_bundle_identifier;
    std::string ios_application_name;
    bool ios_simulator = false;
    std::filesystem::path xcrun_executable = "xcrun";
};

} // namespace engine::plugins::mcp
