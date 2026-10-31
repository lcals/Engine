#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>
#include <string>

namespace engine::render_detail
{

// GpuProgram 独占并统一销毁 handle；本记录仅保存材质 sampler 的绑定状态。
struct GpuSampler
{
    std::string name;
    std::string uniform_name;
    std::uint8_t stage = 0;
    bgfx::UniformHandle handle = BGFX_INVALID_HANDLE;
};

} // namespace engine::render_detail
