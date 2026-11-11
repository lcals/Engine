#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <webgpu/webgpu.h>

namespace engine::render_detail
{
struct WebSurfaceState
{
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    std::uint32_t pending_callbacks = 0;
    bool cancelled = false;
    bool adapter_requested = false;
    bool device_requested = false;
    std::string error;
    std::string selector;
};

// 仅 Web 入口在原生关闭后调用，保活退役设备回调所需的执行环境。
[[nodiscard]] bool web_gpu_callbacks_pending();
} // namespace engine::render_detail
