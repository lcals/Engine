#pragma once

#include <bgfx/bgfx.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace engine
{
class Resource;
}

namespace engine::render_detail
{

struct PreparedTexture
{
    // 主线程只读 GPU 能力并计算准确输出字节；此阶段不分配 mip 输出。
    [[nodiscard]] bool describe(const std::shared_ptr<const Resource>& source,
                                const bgfx::Caps& caps, std::size_t max_bytes);
    // 仅访问本对象和不可变资源；可在 worker 执行，不调用 bgfx。
    [[nodiscard]] bool prepare();

    std::shared_ptr<const Resource> resource;
    bgfx::TextureFormat::Enum format = bgfx::TextureFormat::RGBA8;
    std::uint64_t flags = 0;
    std::size_t bytes = 0;
    std::vector<std::vector<std::uint8_t>> levels;
};

// SceneGpuResources 独占纹理句柄，源资源共享引用随 GPU 纹理一起释放。
// 创建、移动和释放仅在 bgfx API 所属主线程且 bgfx 生命周期内执行。
struct GpuTexture
{
    GpuTexture() = default;
    ~GpuTexture() noexcept;
    GpuTexture(const GpuTexture&) = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;
    GpuTexture(GpuTexture&& other) noexcept;
    GpuTexture& operator=(GpuTexture&& other) noexcept;

    // 调用前释放旧句柄；失败后的部分初始化由 release 或析构清理。
    [[nodiscard]] bool create(const PreparedTexture& prepared);
    void release() noexcept;

    std::shared_ptr<const Resource> resource;
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    std::uint32_t sampler_flags = 0;
};

} // namespace engine::render_detail
