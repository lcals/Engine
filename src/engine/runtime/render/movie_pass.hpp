#pragma once
#include "runtime/movie/movie_frame.hpp"
#include <array>
#include <bgfx/bgfx.h>
#include <cstdint>

namespace engine::render_detail
{
class MoviePass final
{
public:
    ~MoviePass() noexcept;
    [[nodiscard]] bool draw(const MovieFrame& frame, std::uint16_t width, std::uint16_t height);
    void shutdown() noexcept;

private:
    [[nodiscard]] bool initialize();
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle vertices_ = BGFX_INVALID_HANDLE;
    std::array<bgfx::UniformHandle, 3> samplers_{
        {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    bgfx::UniformHandle color_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle chroma_ = BGFX_INVALID_HANDLE;
};
} // namespace engine::render_detail
