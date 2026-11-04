#pragma once

#include <array>
#include <bgfx/bgfx.h>
#include <cstddef>
#include <cstdint>

namespace engine
{
struct RenderSettings;
namespace render_detail
{
struct SceneDrawView;

// Private bgfx 53-sky2 integration. One owner per scene GPU resource set; no ECS borrows persist.
class AtmospherePass final
{
public:
    AtmospherePass() = default;
    ~AtmospherePass() noexcept;
    AtmospherePass(const AtmospherePass&) = delete;
    AtmospherePass& operator=(const AtmospherePass&) = delete;

    [[nodiscard]] static bool supported() noexcept;
    [[nodiscard]] bool draw(std::uint64_t scene_id, const RenderSettings& settings,
                            const SceneDrawView& view, std::size_t view_index,
                            bgfx::ViewId skyview_compute);
    void release() noexcept;
    [[nodiscard]] std::uint32_t resource_count() const noexcept;
    [[nodiscard]] bool submitted_for(std::uint64_t scene_id) const noexcept;

private:
    [[nodiscard]] bool initialize();
    void set_medium() const;
    std::array<bgfx::ProgramHandle, 4> programs_ = {
        {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    std::array<bgfx::UniformHandle, 11> uniforms_ = {
        {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
         BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
         BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    std::array<bgfx::UniformHandle, 3> samplers_ = {
        {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    std::array<bgfx::TextureHandle, 2> medium_ = {{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    std::array<bgfx::TextureHandle, 4> skyview_ = {
        {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    bgfx::VertexBufferHandle fullscreen_ = BGFX_INVALID_HANDLE;
    std::array<std::array<float, 4>, 5> physical_{};
    std::uint64_t scene_id_ = 0;
    std::uint64_t submitted_scene_id_ = 0;
    bool medium_valid_ = false;
};
} // namespace render_detail
} // namespace engine
