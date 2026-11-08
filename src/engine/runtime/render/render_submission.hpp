#pragma once

#include "runtime/render/passes/shadow_frame.hpp"
#include "runtime/render/render_features.hpp"
#include "runtime/render/resources/gpu_mesh.hpp"
#include "runtime/schema/light_probe_generated.h"

#include <array>
#include <compare>
#include <cstdint>
#include <vector>

namespace engine::render_detail
{
// 所有指针仅在当前结构稳定的渲染阶段借用，提交结束后清空，容量可复用。
struct RenderFrameData
{
    struct Mesh
    {
        const GpuMesh* gpu = nullptr;
        const schema::MeshDataT* data = nullptr;
        std::array<float, 16> model{}, normal{};
        std::array<float, 3> center{};
        std::array<std::array<float, 4>, 9> coefficients{};
        bgfx::TextureHandle lightmap = BGFX_INVALID_HANDLE;
        bool has_lightmap = false, has_probe = false;
        bool instance_eligible = false, instancing = false, lod = false;
        bool plane = false, active_plane = false, cast_shadows = false;
        bool all_opaque = true, any_opaque = false, any_transparent = false;
    };
    struct Probe
    {
        std::uint64_t node_id = 0;
        const schema::LightProbeDataT* data = nullptr;
        std::array<float, 16> inverse{};
        double volume = 0;
    };
    std::uint64_t world_epoch = 0;
    std::vector<Mesh> meshes;
    std::vector<LocalShadowLight> lights;
    std::vector<Probe> probes;
};

struct ViewDrawList
{
    struct Item
    {
        std::size_t mesh = 0, slot = 0, lod = 0;
        const GpuGeometry* geometry = nullptr;
        std::uint32_t first = 0, count = 0;
        float depth = 0;
        std::array<std::array<float, 4>, 9> coefficients{};
        bgfx::TextureHandle lightmap = BGFX_INVALID_HANDLE;
        bool has_lightmap = false, has_probe = false;
    };
    std::array<std::uint64_t, 2> observer{};
    std::vector<Item> opaque, transparent;
    RenderViewStats opaque_stats{}, transparent_stats{};
};

// 逐字段比较完整有效状态；不以哈希相等替代状态相等，不读取 struct 填充。
struct DrawBatchKey
{
    std::uintptr_t geometry = 0;
    std::uint64_t state = 0;
    std::size_t binding = 0, lod = 0;
    std::uint32_t first = 0, count = 0;
    std::uint16_t program = bgfx::kInvalidHandle, lightmap = bgfx::kInvalidHandle;
    bool has_lightmap = false, has_probe = false;
    std::array<std::array<float, 4>, 9> coefficients{};
    std::array<float, 4> shadow_alpha{}, shadow_uv{};
    std::uint16_t shadow_texture = bgfx::kInvalidHandle;
    auto operator<=>(const DrawBatchKey&) const = default;
};

struct DrawBatch
{
    DrawBatchKey key;
    std::vector<std::size_t> items;
    std::size_t cursor = 0;
};
} // namespace engine::render_detail
