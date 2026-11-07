#pragma once

#include "runtime/resource/resource_reference.hpp"
#include "runtime/resource/types/texture_resource.hpp"

#include <bgfx/bgfx.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace engine
{
class Resource;
}

namespace engine::render_detail
{

struct PreparedTexture
{
    // 协调线程按 mip / face / byte_offset 描述一个独立准备单元，不分配输出。
    [[nodiscard]] bool describe(ResourceLoader* loader, ResourceHandle source,
                                const bgfx::Caps& caps, std::size_t max_bytes);
    // 仅访问本对象和不可变资源；可在 worker 执行，不调用 bgfx。
    [[nodiscard]] bool prepare();

    ResourceReference resource;
    const schema::TextureDataT* data = nullptr;
    bgfx::TextureFormat::Enum format = bgfx::TextureFormat::RGBA8;
    std::uint64_t flags = 0;
    std::size_t bytes = 0;
    std::uint32_t mip = 0;
    std::uint32_t face = 0;
    std::uint64_t byte_offset = 0;
    std::vector<std::uint8_t> pixels;
    // 仅本次 upload 借用；完整工作持有 pixels，分段上传可借用其中的矩形范围。
    std::span<const std::uint8_t> view;
};

[[nodiscard]] std::size_t texture_region_bytes(const PreparedTexture& part, std::uint64_t offset,
                                               std::size_t maximum) noexcept;

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
    [[nodiscard]] bool upload(const PreparedTexture& part);
    void release() noexcept;
    [[nodiscard]] bool matches(ResourceHandle source) const noexcept
    {
        const Resource* value = nullptr;
        return resource.loader() && resource.loader()->get(source, value) == ResourceError::none &&
               value == resource.get();
    }

    ResourceReference resource;
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    std::uint32_t sampler_flags = 0;
};

using TextureTable = std::unordered_map<const Resource*, GpuTexture>;

// 同一个场景表中的获取均由宿主注入的同一加载器发放。
[[nodiscard]] inline const GpuTexture* find_texture(const TextureTable& textures,
                                                    ResourceHandle handle) noexcept
{
    if (textures.empty())
    {
        return nullptr;
    }
    const auto* loader = textures.begin()->second.resource.loader();
    const Resource* source = nullptr;
    if (!loader || loader->get(handle, source) != ResourceError::none)
    {
        return nullptr;
    }
    const auto found = textures.find(source);
    return found == textures.end() ? nullptr : &found->second;
}

} // namespace engine::render_detail
