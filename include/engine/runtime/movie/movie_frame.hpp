#pragma once

#include "runtime/schema/movie_generated.h"

#include <array>
#include <cstdint>
#include <span>

namespace engine
{
struct MoviePlane
{
    std::span<const std::uint8_t> bytes{};
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride_bytes = 0;
};

struct MovieFrame
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::int64_t presentation_ticks = 0;
    std::uint32_t time_base_num = 0;
    std::uint32_t time_base_den = 0;
    schema::MoviePixelFormat format = schema::MoviePixelFormat::yuv420p8;
    schema::MovieColorInfo color{};
    std::array<MoviePlane, 3> planes{};
};
} // namespace engine
