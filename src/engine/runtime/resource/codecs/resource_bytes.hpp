#pragma once
#include "runtime/resource/resource.hpp"

namespace engine::resource_codec_detail
{
inline bool valid_utf8(ByteView bytes) noexcept
{
    std::size_t index = 0;
    while (index < bytes.size())
    {
        const auto first = std::to_integer<unsigned char>(bytes[index++]);
        if (first == 0)
        {
            return false;
        }
        if (first < 0x80)
        {
            continue;
        }
        unsigned count = 0;
        std::uint32_t value = 0;
        std::uint32_t minimum = 0;
        if (first >= 0xc2 && first <= 0xdf)
        {
            count = 1;
            value = first & 0x1f;
            minimum = 0x80;
        }
        else if (first >= 0xe0 && first <= 0xef)
        {
            count = 2;
            value = first & 0x0f;
            minimum = 0x800;
        }
        else if (first >= 0xf0 && first <= 0xf4)
        {
            count = 3;
            value = first & 7;
            minimum = 0x10000;
        }
        else
        {
            return false;
        }
        if (count > bytes.size() - index)
        {
            return false;
        }
        for (unsigned n = 0; n < count; ++n)
        {
            const auto next = std::to_integer<unsigned char>(bytes[index++]);
            if ((next & 0xc0) != 0x80)
            {
                return false;
            }
            value = (value << 6) | (next & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
        {
            return false;
        }
    }
    return true;
}
} // namespace engine::resource_codec_detail
