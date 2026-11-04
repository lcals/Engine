#pragma once

#include "runtime/scene/scene_types.hpp"

#include <cstdint>

namespace engine
{

enum class RenderFeature : std::uint8_t
{
    frustum_culling,
    static_instancing,
    static_lod,
    wireframe,
    debug_draw,
    mesh_bounds,
    camera_frusta,
    stats,
    count
};

enum class RenderFeatureReason : std::uint8_t
{
    none,
    device_unavailable,
    device_unsupported,
    no_scene,
    no_applicable_draws,
    resources_unavailable
};

struct RenderFeatureState
{
    bool requested = false;
    bool supported = false;
    bool effective = false;
    RenderFeatureReason reason = RenderFeatureReason::none;
};

// 本模块的逐视图 scene mesh 提交快照，不代表 GPU 已完成或 bgfx 全设备统计。
struct RenderViewStats
{
    std::uint32_t frame_number = 0;
    std::uint16_t view_id = 0;
    std::uint32_t candidate_meshes = 0;
    std::uint32_t culled_meshes = 0;
    std::uint32_t visible_meshes = 0;
    std::uint32_t draw_calls = 0;
    std::uint32_t instance_batches = 0;
    std::uint32_t submitted_instances = 0;
    std::uint64_t triangles = 0;
};

struct DebugLine
{
    Vec3 start{};
    Vec3 end{};
    std::uint32_t rgba = 0xffffffff;
    bool depth_test = true;
};

} // namespace engine
