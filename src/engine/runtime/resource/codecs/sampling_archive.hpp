#pragma once

#include "runtime/schema/animation_generated.h"
#include "runtime/schema/skeleton_generated.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <span>
#include <string_view>
#include <vector>

namespace engine::sampling_archive_detail
{
// ozz 0.17 的归档读取以 assert 为前提；进入其读取器前先验证当前固定格式。
// 仅接纳本工程生成的小端、无 seek iframe 的 Skeleton v2 / Animation v7。
struct Cursor
{
    std::span<const std::uint8_t> bytes;
    bool valid = true;
    std::span<const std::uint8_t> take(std::size_t count)
    {
        if (count > bytes.size())
        {
            valid = false;
            return {};
        }
        const auto result = bytes.first(count);
        bytes = bytes.subspan(count);
        return result;
    }
    std::uint32_t integer(std::size_t width)
    {
        const auto value = take(width);
        std::uint32_t result = 0;
        for (std::size_t i = 0; i < value.size(); ++i)
        {
            result |= std::uint32_t(value[i]) << (8 * i);
        }
        return result;
    }
    float scalar()
    {
        return std::bit_cast<float>(integer(4));
    }
    bool text(std::string_view expected, bool terminated)
    {
        const auto value = take(expected.size());
        return valid && std::equal(value.begin(), value.end(), expected.begin(), expected.end()) &&
               (!terminated || integer(1) == 0);
    }
    bool header(std::string_view tag, std::uint32_t version)
    {
        return integer(1) == 1 && text(tag, true) && integer(4) == version && valid;
    }
};

inline bool skeleton(const schema::SkeletonDataT& data)
{
    if (data.sampling_encoding == schema::SamplingEncoding::structured)
    {
        return data.sampling_archive.empty() && data.sampling_joint_order.empty();
    }
    const auto count = data.joints.size();
    if (data.sampling_encoding != schema::SamplingEncoding::ozz_archive ||
        data.sampling_joint_order.size() != count || count == 0 || count > 1024 ||
        data.sampling_archive.size() > 64 * 1024 * 1024)
    {
        return false;
    }
    Cursor input{data.sampling_archive};
    if (!input.header("ozz-skeleton", 2) || input.integer(4) != count)
    {
        return false;
    }
    std::vector<std::vector<std::uint32_t>> children(count);
    std::vector<std::uint32_t> stack, order, inverse(count);
    std::size_t characters = 0;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (!data.joints[i] || data.joints[i]->parent >= static_cast<int>(i))
        {
            return false;
        }
        characters += data.joints[i]->name.size() + 1;
        if (data.joints[i]->parent < 0)
        {
            stack.push_back(i);
        }
        else
        {
            children[data.joints[i]->parent].push_back(i);
        }
    }
    std::reverse(stack.begin(), stack.end());
    while (!stack.empty())
    {
        const auto index = stack.back();
        stack.pop_back();
        inverse[index] = static_cast<std::uint32_t>(order.size());
        order.push_back(index);
        stack.insert(stack.end(), children[index].rbegin(), children[index].rend());
    }
    if (order != data.sampling_joint_order || input.integer(4) != characters)
    {
        return false;
    }
    for (auto index : order)
    {
        if (!input.text(data.joints[index]->name, true))
        {
            return false;
        }
    }
    for (auto index : order)
    {
        const auto parent = data.joints[index]->parent;
        if (input.integer(2) != (parent < 0 ? 65535U : inverse[parent]))
        {
            return false;
        }
    }
    for (std::size_t base = 0; base < count; base += 4)
    {
        std::array<std::array<float, 10>, 4> expected{};
        for (std::size_t lane = 0; lane < 4; ++lane)
        {
            expected[lane] = {0, 0, 0, 0, 0, 0, 1, 1, 1, 1};
            if (base + lane >= count)
            {
                continue;
            }
            const auto& joint = *data.joints[order[base + lane]];
            const auto& q = joint.rotation;
            const double length =
                std::sqrt(q.x() * q.x() + q.y() * q.y() + q.z() * q.z() + q.w() * q.w());
            expected[lane] = {static_cast<float>(joint.translation.x()),
                              static_cast<float>(joint.translation.y()),
                              static_cast<float>(joint.translation.z()),
                              static_cast<float>(q.x() / length),
                              static_cast<float>(q.y() / length),
                              static_cast<float>(q.z() / length),
                              static_cast<float>(q.w() / length),
                              static_cast<float>(joint.scale.x()),
                              static_cast<float>(joint.scale.y()),
                              static_cast<float>(joint.scale.z())};
        }
        for (std::size_t component = 0; component < 10; ++component)
        {
            for (std::size_t lane = 0; lane < 4; ++lane)
            {
                const auto value = input.scalar();
                const auto reference = expected[lane][component];
                if (!std::isfinite(value) ||
                    std::abs(value - reference) > 0.0001F * std::max(1.0F, std::abs(reference)))
                {
                    return false;
                }
            }
        }
    }
    return input.valid && input.bytes.empty();
}

inline bool animation(const schema::AnimationDataT& data)
{
    if (data.sampling_encoding == schema::SamplingEncoding::structured)
    {
        return data.sampling_archive.empty() && data.sampling_channels.empty();
    }
    if (data.sampling_encoding != schema::SamplingEncoding::ozz_archive ||
        data.sampling_archive.size() > 64 * 1024 * 1024 || data.sampling_channels.empty() ||
        data.sampling_channels.size() > 1024 ||
        std::any_of(data.sampling_channels.begin(), data.sampling_channels.end(),
                    [](auto value) { return (value & ~7) != 0; }))
    {
        return false;
    }
    Cursor input{data.sampling_archive};
    if (!input.header("ozz-animation", 7) || input.scalar() != data.duration)
    {
        return false;
    }
    const auto tracks = input.integer(4);
    const auto name_size = input.integer(4);
    const auto times = input.integer(4);
    const std::array keys{input.integer(4), input.integer(4), input.integer(4)};
    for (int i = 0; i < 6; ++i)
    {
        if (input.integer(4) != 0)
        {
            return false;
        }
    }
    const std::uint64_t key_count = std::uint64_t(keys[0]) + keys[1] + keys[2];
    const auto ratio_bytes = times <= 255 ? 1U : 2U;
    if (!input.valid || tracks != data.sampling_channels.size() || name_size != data.name.size() ||
        times < 2 || times > 65535 ||
        name_size + std::uint64_t(times) * 4 + key_count * (ratio_bytes + 2 + 6) + 12 !=
            input.bytes.size() ||
        !input.text(data.name, false))
    {
        return false;
    }
    std::vector<float> timepoints(times);
    for (std::size_t i = 0; i < times; ++i)
    {
        const auto value = input.scalar();
        if (!std::isfinite(value) || value < 0 || value > 1 || (i && value <= timepoints[i - 1]))
        {
            return false;
        }
        timepoints[i] = value;
    }
    if (timepoints.front() != 0 || timepoints.back() != 1)
    {
        return false;
    }
    const auto padded = (tracks + 3) & ~3U;
    for (std::size_t channel = 0; channel < keys.size(); ++channel)
    {
        const auto count = keys[channel];
        if (count < padded * 2)
        {
            return false;
        }
        std::vector<std::uint32_t> ratios(count), owners(count), previous(count), last(padded);
        for (auto& ratio : ratios)
        {
            ratio = input.integer(ratio_bytes);
            if (ratio >= times)
            {
                return false;
            }
        }
        for (auto& distance : previous)
        {
            distance = input.integer(2);
        }
        float previous_time = 0;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            if (i < padded)
            {
                if (previous[i] || ratios[i])
                {
                    return false;
                }
                owners[i] = i;
                last[i] = i;
                continue;
            }
            if (!previous[i] || previous[i] > i)
            {
                return false;
            }
            const auto before = i - previous[i];
            const auto track = owners[before];
            const auto time = timepoints[ratios[before]];
            if (last[track] != before || ratios[i] <= ratios[before] || time < previous_time ||
                (i < padded * 2 && before != i - padded))
            {
                return false;
            }
            previous_time = time;
            owners[i] = track;
            last[track] = i;
        }
        for (auto index : last)
        {
            if (ratios[index] != times - 1)
            {
                return false;
            }
        }
        const auto interval = input.scalar();
        if (!std::isfinite(interval) || interval < 0)
        {
            return false;
        }
        for (std::uint32_t i = 0; i < count; ++i)
        {
            for (int component = 0; component < 3; ++component)
            {
                const auto value = input.integer(2);
                if (channel != 1 &&
                    ((value & 0x7c00) == 0x7c00 || (channel == 2 && (value & 0x7fff) == 0)))
                {
                    return false;
                }
            }
        }
    }
    return input.valid && input.bytes.empty();
}
} // namespace engine::sampling_archive_detail
