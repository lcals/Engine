#pragma once

#include <bgfx/bgfx.h>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::render_detail
{
// Final display-space image shared by presentation and ordinary screenshots.
class FrameOutput final
{
public:
    FrameOutput() = default;
    ~FrameOutput() noexcept;
    FrameOutput(const FrameOutput&) = delete;
    FrameOutput& operator=(const FrameOutput&) = delete;
    [[nodiscard]] bool prepare(std::uint16_t width, std::uint16_t height);
    void present(std::uint32_t clear_color, bool movie);
    [[nodiscard]] bool capture();
    [[nodiscard]] bool collect(std::uint32_t frame);
    [[nodiscard]] const std::vector<std::byte>& pixels() const noexcept
    {
        return pixels_;
    }
    [[nodiscard]] std::uint32_t capture_width() const noexcept
    {
        return capture_width_;
    }
    [[nodiscard]] std::uint32_t capture_height() const noexcept
    {
        return capture_height_;
    }
    void release() noexcept;

private:
    bgfx::FrameBufferHandle target_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle color_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle readback_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle vertices_ = BGFX_INVALID_HANDLE;
    std::uint16_t width_ = 0, height_ = 0;
    std::uint16_t capture_width_ = 0, capture_height_ = 0;
    std::uint32_t ready_frame_ = 0;
    bool pending_ = false;
    std::vector<std::byte> pixels_;
};
} // namespace engine::render_detail
