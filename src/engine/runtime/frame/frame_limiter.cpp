#include "runtime/frame/frame_limiter.hpp"

#include <thread>

namespace engine
{

bool FrameLimiter::set_target_frame_rate(std::int32_t rate) noexcept
{
    if (rate != -1 && rate <= 0)
    {
        return false;
    }
    target_frame_rate_ = rate;
    return true;
}

std::int32_t FrameLimiter::target_frame_rate() const noexcept
{
    return target_frame_rate_;
}

void FrameLimiter::begin_frame() noexcept
{
    frame_start_ = std::chrono::steady_clock::now();
    frame_started_ = true;
}

void FrameLimiter::end_frame()
{
    if (!frame_started_)
    {
        return;
    }
    frame_started_ = false;
    if (target_frame_rate_ == -1)
    {
        return;
    }
    const auto duration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(1.0 / static_cast<double>(target_frame_rate_)));
    const auto deadline = frame_start_ + duration;
    if (std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_until(deadline);
    }
}

void FrameLimiter::reset() noexcept
{
    frame_start_ = {};
    target_frame_rate_ = -1;
    frame_started_ = false;
}

} // namespace engine
