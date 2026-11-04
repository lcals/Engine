#include "runtime/animation/animation_event_interval.hpp"

#include <algorithm>
#include <cmath>

namespace engine::animation_detail
{
bool collect_event_crossings(std::span<const double> marker_times, double duration, double previous,
                             double current, bool loop, bool starting, std::size_t limit,
                             std::vector<EventCrossing>& output)
{
    output.clear();
    if (!std::isfinite(duration) || duration <= 0.0 || !std::isfinite(previous) ||
        !std::isfinite(current) || previous < 0.0 || current < previous ||
        (starting && previous != 0.0))
    {
        return false;
    }
    for (const auto time : marker_times)
    {
        if (!std::isfinite(time) || time < 0.0 || time > duration)
        {
            return false;
        }
    }
    if (marker_times.empty() || (current == previous && !starting))
    {
        return true;
    }

    // 周期和局部相位分别计算；大绝对时间减标记会吞掉小数，尤其 ARM 的
    // long double 与 double 精度相同。超过可精确表示的周期范围明确失败。
    const auto phase = [duration](double value, std::uint64_t& cycle, double& local)
    {
        double whole = std::floor(value / duration);
        if (!std::isfinite(whole) || whole >= 0x1p53)
        {
            return false;
        }
        local = std::fma(-whole, duration, value);
        if (local < 0.0)
        {
            whole -= 1.0;
            local = std::fma(-whole, duration, value);
        }
        else if (local >= duration)
        {
            whole += 1.0;
            local = std::fma(-whole, duration, value);
        }
        if (whole < 0.0 || whole >= 0x1p53 || local < 0.0 || local >= duration)
        {
            return false;
        }
        cycle = static_cast<std::uint64_t>(whole);
        return true;
    };
    std::uint64_t previous_cycle = 0;
    std::uint64_t current_cycle = 0;
    double previous_phase = 0.0;
    double current_phase = 0.0;
    if (loop && (!phase(previous, previous_cycle, previous_phase) ||
                 !phase(current, current_cycle, current_phase)))
    {
        return false;
    }
    const auto range = [&](double time, std::uint64_t& first, std::uint64_t& last)
    {
        const bool end_marker = time == duration;
        const double marker_phase = end_marker ? 0.0 : time;
        first = previous_cycle + (marker_phase <= previous_phase ? 1U : 0U);
        last = current_cycle;
        if (marker_phase > current_phase)
        {
            if (last == 0)
            {
                return false;
            }
            --last;
        }
        if (starting && time == 0.0)
        {
            first = 0;
        }
        if (end_marker)
        {
            // duration 属于结束的周期，0 属于下一周期；均保持原标记身份。
            first = std::max(first, std::uint64_t{1});
            if (last < first)
            {
                return false;
            }
            --first;
            --last;
        }
        return last >= first;
    };

    // 先计算完整数量，拒绝大步长/预算溢出，避免产生可被误发布的半批事件。
    std::size_t count = 0;
    for (const auto time : marker_times)
    {
        std::uint64_t first = 0;
        std::uint64_t last = 0;
        if (loop)
        {
            if (!range(time, first, last))
            {
                continue;
            }
        }
        else if (!((time > previous && time <= current) || (starting && time == 0.0)))
        {
            continue;
        }
        const auto difference = last - first;
        if (count >= limit || difference >= limit - count)
        {
            return false;
        }
        count += static_cast<std::size_t>(difference) + 1;
    }

    if (count > output.max_size())
    {
        return false;
    }
    output.reserve(count);
    for (std::size_t index = 0; index < marker_times.size(); ++index)
    {
        const auto time = marker_times[index];
        std::uint64_t first = 0;
        std::uint64_t last = 0;
        if (loop)
        {
            if (!range(time, first, last))
            {
                continue;
            }
        }
        else if (!((time > previous && time <= current) || (starting && time == 0.0)))
        {
            continue;
        }
        for (auto cycle = first;; ++cycle)
        {
            output.emplace_back(cycle, index);
            if (cycle == last)
            {
                break;
            }
        }
    }
    std::sort(output.begin(), output.end(),
              [&](const auto& first, const auto& second)
              {
                  const bool first_end = marker_times[first.second] == duration;
                  const bool second_end = marker_times[second.second] == duration;
                  const auto first_cycle = first.first + (first_end ? 1U : 0U);
                  const auto second_cycle = second.first + (second_end ? 1U : 0U);
                  const double first_phase = first_end ? 0.0 : marker_times[first.second];
                  const double second_phase = second_end ? 0.0 : marker_times[second.second];
                  if (first_cycle != second_cycle)
                  {
                      return first_cycle < second_cycle;
                  }
                  return first_phase < second_phase ||
                         (first_phase == second_phase && first < second);
              });
    return true;
}
} // namespace engine::animation_detail
