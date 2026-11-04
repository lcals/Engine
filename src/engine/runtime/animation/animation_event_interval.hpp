#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace engine::animation_detail
{
// 周期与标记索引共同标识一次跨越；事件名不能作为去重键。
using EventCrossing = std::pair<std::uint64_t, std::size_t>;

// 正向区间 (previous, current]；starting 只允许 previous == 0，并包含零时刻一次。
// 时间未取模。失败清空 output；limit 是显式事件预算，不允许截断后返回成功。
[[nodiscard]] bool collect_event_crossings(std::span<const double> marker_times, double duration,
                                           double previous, double current, bool loop,
                                           bool starting, std::size_t limit,
                                           std::vector<EventCrossing>& output);
} // namespace engine::animation_detail
