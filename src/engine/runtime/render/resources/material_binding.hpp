#pragma once

#include "runtime/schema/material_generated.h"
#include "runtime/schema/shader_generated.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::render_detail
{
struct GpuProgram;

// GpuMesh 材质槽拥有的派生缓存；GPU 句柄借用原资源拥有者，不另行加载或计数。
struct MaterialBinding
{
    struct Sampler
    {
        schema::ShaderTextureSource source{};
        std::uint16_t texture = bgfx::kInvalidHandle;
        std::uint32_t flags = 0;
        bool operator==(const Sampler&) const = default;
    };
    std::uint64_t slot_identity = 0;
    std::size_t content_hash = 0;
    std::size_t binding_id = 0;
    const GpuProgram* program = nullptr;
    std::vector<std::array<float, 4>> values;
    std::vector<Sampler> samplers;
    schema::CullMode cull{};
    schema::BlendMode blend{};
    bool frame_textures = false;
    bool unlit = false;
    bool shadow_valid = false;
    std::array<float, 4> shadow_alpha{0, 1, 0, 0};
    std::array<float, 4> shadow_uv{1, 1, 0, 0};
    bgfx::TextureHandle shadow_texture = BGFX_INVALID_HANDLE;

    bool operator==(const MaterialBinding& other) const
    {
        return program == other.program && cull == other.cull && blend == other.blend &&
               values == other.values && samplers == other.samplers;
    }
};
} // namespace engine::render_detail
