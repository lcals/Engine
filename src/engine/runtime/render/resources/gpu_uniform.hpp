#pragma once

#include <bgfx/bgfx.h>

#include <string>

namespace engine::render_detail
{

// GpuProgram 独占并统一销毁 handle；本记录仅保存材质 uniform 的绑定状态。
struct GpuUniform
{
    std::string name;
    bgfx::UniformHandle handle = BGFX_INVALID_HANDLE;
};

} // namespace engine::render_detail
