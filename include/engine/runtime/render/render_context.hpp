#pragma once

#include <array>
#include <cstdint>

namespace engine
{

class Scene;

// 仅在 draw() 调用期间有效；矩阵来自本次实际观察镜头。
struct RenderContext
{
    const Scene& scene;
    std::array<float, 16> view{};
    std::array<float, 16> projection{};
    std::uint16_t view_id = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    bool hdr = false;
};

} // namespace engine
