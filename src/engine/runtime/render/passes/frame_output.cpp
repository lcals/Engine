#include "runtime/render/passes/frame_output.hpp"
#include "runtime/render/render_constants.hpp"

#include "fs_hdr_copy.bin.h"
#include "vs_hdr.bin.h"

#include <array>

namespace engine::render_detail
{
FrameOutput::~FrameOutput() noexcept
{
    release();
}
bool FrameOutput::prepare(std::uint16_t width, std::uint16_t height)
{
    if (width == 0 || height == 0)
    {
        return false;
    }
    if (!bgfx::isValid(program_))
    {
        const auto vs = bgfx::createShader(bgfx::copy(vs_hdr, sizeof(vs_hdr)));
        const auto fs = bgfx::createShader(bgfx::copy(fs_hdr_copy, sizeof(fs_hdr_copy)));
        if (!bgfx::isValid(vs) || !bgfx::isValid(fs))
        {
            if (bgfx::isValid(vs))
            {
                bgfx::destroy(vs);
            }
            if (bgfx::isValid(fs))
            {
                bgfx::destroy(fs);
            }
            return false;
        }
        program_ = bgfx::createProgram(vs, fs, true);
        sampler_ = bgfx::createUniform("s_hdr_scene", bgfx::UniformType::Sampler);
        struct Vertex
        {
            float x, y, z, u, v;
        };
        std::array vertices = {Vertex{-1, -1, 0, 0, 1}, Vertex{3, -1, 0, 2, 1},
                               Vertex{-1, 3, 0, 0, -1}};
        if (bgfx::getCaps()->originBottomLeft)
        {
            for (auto& value : vertices)
            {
                value.v = 1 - value.v;
            }
        }
        bgfx::VertexLayout layout;
        layout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
        vertices_ = bgfx::createVertexBuffer(bgfx::copy(vertices.data(), sizeof(vertices)), layout);
    }
    if (!bgfx::isValid(program_) || !bgfx::isValid(sampler_) || !bgfx::isValid(vertices_))
    {
        return false;
    }
    if (width != width_ || height != height_ || !bgfx::isValid(target_))
    {
        if (bgfx::isValid(target_))
        {
            bgfx::destroy(target_);
        }
        color_ =
            bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::BGRA8,
                                  BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        if (!bgfx::isValid(color_))
        {
            target_ = BGFX_INVALID_HANDLE;
            return false;
        }
        target_ = bgfx::createFrameBuffer(1, &color_, true);
        width_ = width;
        height_ = height;
    }
    return bgfx::isValid(target_);
}

void FrameOutput::present(std::uint32_t clear_color, bool movie)
{
    bgfx::setViewName(output_clear_view, "Display output clear");
    bgfx::setViewFrameBuffer(output_clear_view, target_);
    bgfx::setViewRect(output_clear_view, 0, 0, width_, height_);
    bgfx::setViewClear(output_clear_view, BGFX_CLEAR_COLOR, clear_color);
    bgfx::touch(output_clear_view);
    bgfx::setViewFrameBuffer(hdr_resolve_view, target_);
    bgfx::setViewFrameBuffer(200, target_);
    if (movie)
    {
        bgfx::setViewFrameBuffer(main_view + 1, target_);
    }
    bgfx::setViewName(output_present_view, "Present display output");
    bgfx::setViewFrameBuffer(output_present_view, BGFX_INVALID_HANDLE);
    bgfx::setViewRect(output_present_view, 0, 0, width_, height_);
    bgfx::setViewClear(output_present_view, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(output_present_view, nullptr, nullptr);
    bgfx::setTexture(0, sampler_, color_,
                     BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT |
                         BGFX_SAMPLER_MAG_POINT);
    bgfx::setVertexBuffer(0, vertices_);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(output_present_view, program_);
}

bool FrameOutput::capture()
{
    if (pending_ || !bgfx::isValid(color_) ||
        !bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::BGRA8,
                              BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST))
    {
        return false;
    }
    if (bgfx::isValid(readback_))
    {
        bgfx::destroy(readback_);
    }
    readback_ = bgfx::createTexture2D(width_, height_, false, 1, bgfx::TextureFormat::BGRA8,
                                      BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
    if (!bgfx::isValid(readback_))
    {
        return false;
    }
    capture_width_ = width_;
    capture_height_ = height_;
    pixels_.resize(static_cast<std::size_t>(width_) * height_ * 4);
    bgfx::blit(output_capture_view, bgfx::TextureRegion{.handle = readback_},
               bgfx::TextureRegion{.handle = color_});
    ready_frame_ = bgfx::read(bgfx::TextureRegion{.handle = readback_}, pixels_.data());
    pending_ = ready_frame_ != UINT32_MAX;
    return pending_;
}

bool FrameOutput::collect(std::uint32_t frame)
{
    if (!pending_ || static_cast<std::int32_t>(frame - ready_frame_) < 0)
    {
        return false;
    }
    pending_ = false;
    if (bgfx::isValid(readback_))
    {
        bgfx::destroy(readback_);
    }
    readback_ = BGFX_INVALID_HANDLE;
    return true;
}

void FrameOutput::release() noexcept
{
    if (bgfx::isValid(target_))
    {
        bgfx::destroy(target_);
    }
    if (bgfx::isValid(readback_))
    {
        bgfx::destroy(readback_);
    }
    if (bgfx::isValid(program_))
    {
        bgfx::destroy(program_);
    }
    if (bgfx::isValid(sampler_))
    {
        bgfx::destroy(sampler_);
    }
    if (bgfx::isValid(vertices_))
    {
        bgfx::destroy(vertices_);
    }
    target_ = BGFX_INVALID_HANDLE;
    color_ = readback_ = BGFX_INVALID_HANDLE;
    program_ = BGFX_INVALID_HANDLE;
    sampler_ = BGFX_INVALID_HANDLE;
    vertices_ = BGFX_INVALID_HANDLE;
    width_ = height_ = 0;
    // GPU readback memory is retained until bgfx shutdown has drained outstanding work.
}
} // namespace engine::render_detail
