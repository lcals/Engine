#pragma once
#include "runtime/render/passes/shadow_frame.hpp"
#include "runtime/scene/camera.hpp"
#include <array>
#include <bgfx/bgfx.h>
#include <cstdint>

namespace engine
{
class Camera;
class RenderSystem;
struct SceneCaptureRequest;
namespace render_detail
{
class ClusteredLightingPass;
enum class SceneDrawMode : std::uint8_t
{
    depth,
    surface,
    opaque,
    transparent,
    capture
};

// A synchronous draw borrows this frame's camera and lighting, never scene ownership.
struct SceneDrawView
{
    RenderSystem* renderer = nullptr;
    bgfx::ViewId id = 0;
    std::uint16_t width = 0, height = 0;
    const float* world = nullptr;
    const float* view = nullptr;
    const float* projection = nullptr;
    Camera visibility = {};
    // Stable camera/component identities keep hysteresis while temporary view ids are reused.
    std::array<std::uint64_t, 2> lod_observer{};
    const ClusteredLightingPass* cluster = nullptr;
    const ShadowFrame* shadow = nullptr;
    std::uint32_t shadow_light = UINT32_MAX;
    float shadow_bias = 0;
    float inverse_shadow_size = 0;
    float near_clip = 0.1F;
    bool ambient_occlusion = false;
    bool reflection = false;
    bool frustum_culling_enabled = false;
    bool multisampling = false;
    bool max_anisotropy = false;
    const SceneCaptureRequest* capture = nullptr;
    std::array<bgfx::TextureHandle, 3> textures = {
        {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
};
} // namespace render_detail
} // namespace engine
