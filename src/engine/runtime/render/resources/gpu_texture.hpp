#pragma once

#include <bgfx/bgfx.h>

#include <memory>

namespace engine
{
class Resource;
}

namespace engine::render_detail
{

// SceneGpuResources 独占纹理句柄，源资源共享引用随 GPU 纹理一起释放。
// 创建、移动和释放仅在渲染线程且 bgfx 生命周期内执行。
struct GpuTexture
{
    GpuTexture() = default;
    ~GpuTexture() noexcept;
    GpuTexture(const GpuTexture&) = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;
    GpuTexture(GpuTexture&& other) noexcept;
    GpuTexture& operator=(GpuTexture&& other) noexcept;

    // 调用前释放旧句柄；失败后的部分初始化由 release 或析构清理。
    [[nodiscard]] bool create(const std::shared_ptr<const Resource>& source);
    void release() noexcept;

    std::shared_ptr<const Resource> resource;
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
};

} // namespace engine::render_detail
