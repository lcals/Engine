#pragma once

#include "runtime/render/resources/gpu_sampler.hpp"
#include "runtime/render/resources/gpu_uniform.hpp"
#include "runtime/schema/shader_generated.h"

#include <bgfx/bgfx.h>

#include <string>
#include <vector>

namespace engine
{
class Material;
}

namespace engine::render_detail
{

struct GpuTexture;

// 独占 shader、program 和材质绑定句柄；仅在渲染线程且 bgfx 存活时使用。
struct GpuProgram
{
    GpuProgram() = default;
    ~GpuProgram() noexcept;
    GpuProgram(const GpuProgram&) = delete;
    GpuProgram& operator=(const GpuProgram&) = delete;
    GpuProgram(GpuProgram&& other) noexcept;
    GpuProgram& operator=(GpuProgram&& other) noexcept;

    // 调用前释放旧句柄；失败后的部分初始化由 release 或析构清理。
    [[nodiscard]] bool create(const schema::ShaderDataT& shader);
    [[nodiscard]] bool bind_material(const Material& material,
                                     const std::vector<GpuTexture>& textures);
    void release() noexcept;

    std::string path;
    schema::ShaderDataT definition;
    bgfx::ShaderHandle vertex_shader = BGFX_INVALID_HANDLE;
    bgfx::ShaderHandle fragment_shader = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    std::vector<GpuUniform> uniforms;
    std::vector<GpuSampler> samplers;
};

} // namespace engine::render_detail
