#pragma once

#include <cstdint>
#include <string>

namespace engine
{

struct EngineConfig
{
    // 项目根目录，必须包含 settings、cache、assets 三个子目录。
    std::string project;
    std::string window_title = "Engine";
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
};

} // namespace engine
