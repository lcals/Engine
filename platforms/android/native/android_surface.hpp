#pragma once

#include <cstdint>

namespace engine::android
{

constexpr std::int32_t suspend_rendering_event = 0x454e4750;

// 平台内部协议：begin/end 只由 SDL 原生主线程调用。
[[nodiscard]] bool surface_paused() noexcept;
[[nodiscard]] bool begin_rendering() noexcept;
void end_rendering() noexcept;

} // namespace engine::android
