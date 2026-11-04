#include "runtime/animation/animation_event_interval.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
using engine::animation_detail::collect_event_crossings;
using engine::animation_detail::EventCrossing;

bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
    return condition;
}

bool intervals()
{
    const std::array markers{0.0, 0.25, 0.75, 1.0};
    std::vector<EventCrossing> events;
    return expect(collect_event_crossings(markers, 1, 0, 0.75, false, true, 4, events) &&
                      events == std::vector<EventCrossing>{{0, 0}, {0, 1}, {0, 2}},
                  "initial marker fires once; inclusive end marker is not lost") &&
           expect(collect_event_crossings(markers, 1, 0.75, 1, false, false, 4, events) &&
                      events == std::vector<EventCrossing>{{0, 3}},
                  "adjacent updates do not repeat their shared boundary") &&
           expect(collect_event_crossings(markers, 1, 1, 10, false, false, 4, events) &&
                      events.empty(),
                  "nonloop final pose emits no further marker") &&
           expect(collect_event_crossings(markers, 1, 0.5, 0.5, true, false, 0, events) &&
                      events.empty(),
                  "paused interval produces no event, including with zero budget") &&
           expect(collect_event_crossings(markers, 1, 0.75, 0.9, false, false, 4, events) &&
                      events.empty(),
                  "seek establishes a new baseline without replaying skipped markers");
}

bool multiple_cycles()
{
    const std::array markers{0.0, 0.25, 0.75, 1.0};
    std::vector<EventCrossing> events;
    const std::vector<EventCrossing> expected{{0, 2}, {0, 3}, {1, 0}, {1, 1}, {1, 2}, {1, 3},
                                              {2, 0}, {2, 1}, {2, 2}, {2, 3}, {3, 0}, {3, 1}};
    if (!expect(collect_event_crossings(markers, 1, 0.5, 3.25, true, false, 12, events) &&
                    events == expected,
                "one update spans three wraps in chronological order with distinct end/start") ||
        !expect(!collect_event_crossings(markers, 1, 0.5, 3.25, true, false, 11, events) &&
                    events.empty(),
                "budget overflow rejects the complete batch instead of truncating"))
    {
        return false;
    }
    const std::array coincident{0.5, 0.5};
    return expect(collect_event_crossings(coincident, 1, 0, 0.5, true, false, 2, events) &&
                      events == std::vector<EventCrossing>{{0, 0}, {0, 1}},
                  "distinct markers at the same time are not deduplicated by time/name") &&
           expect(collect_event_crossings(markers, 1, 0, 0, true, true, 1, events) &&
                      events == std::vector<EventCrossing>{{0, 0}},
                  "zero-time initial update reports the new playback's origin exactly once");
}

bool partition_invariance()
{
    const std::array markers{0.0, 0.125, 0.5, 0.875, 1.0};
    std::vector<EventCrossing> one;
    std::vector<EventCrossing> split;
    std::vector<EventCrossing> part;
    if (!collect_event_crossings(markers, 1, 0, 16, true, true, 100, one))
    {
        return false;
    }
    for (int tick = 0; tick < 128; ++tick)
    {
        if (!collect_event_crossings(markers, 1, tick / 8.0, (tick + 1) / 8.0, true, tick == 0, 100,
                                     part))
        {
            return false;
        }
        split.insert(split.end(), part.begin(), part.end());
    }
    return expect(one == split && one.size() == 81,
                  "one large update equals 128 small updates, including all loop boundaries");
}

bool invalid_input_and_isolation()
{
    const std::array valid{0.25};
    const std::array invalid{std::numeric_limits<double>::quiet_NaN()};
    const std::array outside{1.1};
    std::vector<EventCrossing> events{{7, 9}};
    if (!expect(!collect_event_crossings(valid, 0, 0, 1, true, false, 2, events) && events.empty(),
                "invalid duration clears stale output") ||
        !expect(!collect_event_crossings(invalid, 1, 0, 1, true, false, 2, events) &&
                    !collect_event_crossings(outside, 1, 0, 1, true, false, 2, events),
                "nonfinite and out-of-range resource markers are rejected") ||
        !expect(!collect_event_crossings(valid, 1, 1, 0, true, false, 2, events) &&
                    !collect_event_crossings(valid, 1, 0.5, 1, true, true, 2, events),
                "unsupported reverse interval and invalid playback start are rejected") ||
        !expect(!collect_event_crossings(valid, 1, 0, std::numeric_limits<double>::infinity(), true,
                                         false, 2, events) &&
                    !collect_event_crossings(valid, 1, 0, 1e30, true, false, 2, events),
                "nonfinite time and cycle overflow return failure without a partial batch"))
    {
        return false;
    }
    if (!expect(
            !collect_event_crossings(valid, 1, 0x1p53 - 1, 0x1p53 + 2, true, false, 3, events) &&
                events.empty(),
            "unrepresentable absolute cycle ranges fail rather than lose fractional markers"))
    {
        return false;
    }
    const std::array reverse_order{0.75, 0.25};
    constexpr auto late_cycle = std::uint64_t{1} << 50;
    if (!expect(
            collect_event_crossings(reverse_order, 1, 0x1p50, 0x1p50 + 1, true, false, 2, events) &&
                events == std::vector<EventCrossing>{{late_cycle, 1}, {late_cycle, 0}},
            "large but supported cycles sort by local phase without absolute-time rounding"))
    {
        return false;
    }
    std::vector<EventCrossing> other;
    return expect(collect_event_crossings(valid, 1, 0, 2, true, false, 2, events) &&
                      collect_event_crossings(valid, 1, 0.5, 0.6, true, false, 2, other) &&
                      events == std::vector<EventCrossing>{{0, 0}, {1, 0}} && other.empty() &&
                      valid[0] == 0.25,
                  "actors share immutable marker data and retain independent output intervals");
}
} // namespace

int main()
{
    if (!intervals() || !multiple_cycles() || !partition_invariance() ||
        !invalid_input_and_isolation())
    {
        return 1;
    }
    std::puts("PASS animation event intervals: boundaries, wraps, budget, identity and partitions");
    return 0;
}
