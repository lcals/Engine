#pragma once

#include <chrono>
#include <cstdint>

namespace engine
{

class FrameLimiter final
{
public:
    // -1 表示不限帧；其余值必须为正整数，失败保持当前设置。
    [[nodiscard]] bool set_target_frame_rate(std::int32_t rate) noexcept;
    [[nodiscard]] std::int32_t target_frame_rate() const noexcept;
    void begin_frame() noexcept;
    void end_frame();
    void reset() noexcept;

private:
    std::chrono::steady_clock::time_point frame_start_{};
    std::int32_t target_frame_rate_ = -1;
    bool frame_started_ = false;
};

} // namespace engine
