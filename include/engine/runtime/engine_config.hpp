#pragma once

#include <cstdint>
#include <string>

namespace engine
{

struct EngineConfig
{
    std::string window_title = "Engine";
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    std::int32_t target_frame_rate = -1;
};

} // namespace engine
