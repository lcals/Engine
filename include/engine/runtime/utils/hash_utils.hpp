#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace engine::hash
{
inline constexpr std::uint64_t offset = 14695981039346656037ULL;

[[nodiscard]] inline std::uint64_t fnv1a(std::span<const std::byte> bytes,
                                         std::uint64_t seed = offset) noexcept
{
    for (const auto byte : bytes)
    {
        seed = (seed ^ std::to_integer<std::uint8_t>(byte)) * 1099511628211ULL;
    }
    return seed;
}

[[nodiscard]] inline std::uint64_t fnv1a(std::string_view bytes,
                                         std::uint64_t seed = offset) noexcept
{
    return fnv1a(std::as_bytes(std::span(bytes.data(), bytes.size())), seed);
}

[[nodiscard]] inline std::string hex(std::uint64_t value)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string output(16, '0');
    for (std::size_t index = output.size(); index != 0; --index)
    {
        output[index - 1] = digits[value & 15U];
        value >>= 4U;
    }
    return output;
}
} // namespace engine::hash
