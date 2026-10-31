#include "runtime/render/passes/skinning_pass.hpp"

#include "runtime/render/render_constants.hpp"
#include "runtime/render/resources/gpu_geometry.hpp"

#include <SDL3/SDL.h>

#include "cs_skinning.bin.h"

#include <algorithm>

namespace engine::render_detail
{

SkinningPass::~SkinningPass() noexcept
{
    release();
}

bool SkinningPass::prepare()
{
    if (bgfx::isValid(program_))
    {
        return true;
    }
    const auto* caps = bgfx::getCaps();
    if (caps == nullptr || (caps->supported & BGFX_CAPS_COMPUTE) == 0 ||
        caps->limits.maxComputeBindings < 4)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                     "GPU skinning requires compute shaders and four buffer bindings");
        return false;
    }
    release();
    shader_ = bgfx::createShader(bgfx::copy(cs_skinning, sizeof(cs_skinning)));
    if (!bgfx::isValid(shader_))
    {
        return false;
    }
    program_ = bgfx::createProgram(shader_, false);
    parameters_ = bgfx::createUniform("u_skinning_parameters", bgfx::UniformType::Vec4);
    if (!bgfx::isValid(program_) || !bgfx::isValid(parameters_))
    {
        release();
        return false;
    }
    bgfx::setViewName(skinning_view, "Skinning");
    bgfx::setViewMode(skinning_view, bgfx::ViewMode::Sequential);
    return true;
}

bool SkinningPass::dispatch(const GpuGeometry& geometry, bgfx::DynamicVertexBufferHandle palette,
                            bgfx::DynamicVertexBufferHandle output, std::uint32_t joint_count)
{
    if (!bgfx::isValid(program_) || !bgfx::isValid(geometry.vertices) ||
        !bgfx::isValid(geometry.influences) || !bgfx::isValid(palette) || !bgfx::isValid(output) ||
        geometry.vertex_count == 0 || joint_count == 0 || joint_count > 65536)
    {
        return false;
    }
    // bgfx examples/24-nbody: compute buffers become graphics inputs in a later view.
    // Copyright 2011-2025 Branimir Karadzic, BSD-2-Clause; third_party/notices/bgfx.txt.
    constexpr std::uint32_t group_size = 64;
    constexpr std::uint32_t maximum_groups = 65535;
    const auto groups = (geometry.vertex_count + group_size - 1) / group_size;
    const auto groups_x = std::min(groups, maximum_groups);
    const auto groups_y = (groups + groups_x - 1) / groups_x;
    if (groups_y > maximum_groups)
    {
        return false;
    }
    // 用两个精确的 16 位整数传递顶点数，避免 float 无法表达大于 2^24 的整数。
    const float parameters[] = {static_cast<float>(geometry.vertex_count & 65535U),
                                static_cast<float>(geometry.vertex_count >> 16U),
                                static_cast<float>(joint_count),
                                static_cast<float>(groups_x * group_size)};
    bgfx::setUniform(parameters_, parameters);
    bgfx::setBuffer(0, geometry.vertices, bgfx::Access::Read);
    bgfx::setBuffer(1, geometry.influences, bgfx::Access::Read);
    bgfx::setBuffer(2, palette, bgfx::Access::Read);
    bgfx::setBuffer(3, output, bgfx::Access::Write);
    bgfx::dispatch(skinning_view, program_, groups_x, groups_y, 1);
    return true;
}

std::uint32_t SkinningPass::resource_count() const noexcept
{
    return static_cast<std::uint32_t>(bgfx::isValid(shader_)) +
           static_cast<std::uint32_t>(bgfx::isValid(program_)) +
           static_cast<std::uint32_t>(bgfx::isValid(parameters_));
}

void SkinningPass::release() noexcept
{
    if (bgfx::isValid(program_))
    {
        bgfx::destroy(program_);
        program_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(shader_))
    {
        bgfx::destroy(shader_);
        shader_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(parameters_))
    {
        bgfx::destroy(parameters_);
        parameters_ = BGFX_INVALID_HANDLE;
    }
}

} // namespace engine::render_detail
