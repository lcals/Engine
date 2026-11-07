#pragma once

#include "runtime/resource/resource_handle.hpp"

#include <bgfx/bgfx.h>

namespace engine
{
class ResourceLoader;

namespace render
{
// 在 bgfx API 线程同步上传已加载资源，不读取文件。output 必须为空，失败保持为空。
// 成功转交 GPU 纹理所有权，调用方须在 bgfx 关闭前调用 bgfx::destroy。
// CPU 资源只在调用期间借用；需要保持其身份的调用方自行持有获取。
[[nodiscard]] bool upload_texture(ResourceLoader& loader, ResourceHandle resource,
                                  bgfx::TextureHandle& output);
} // namespace render
} // namespace engine
