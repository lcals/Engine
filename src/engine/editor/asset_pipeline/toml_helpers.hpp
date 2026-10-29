#pragma once

#include "runtime/core/guid_map.hpp"
#include "runtime/schema/common_generated.h"

#include <toml++/toml.hpp>

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace engine::asset_pipeline
{

inline bool identifier(const toml::node* node, std::uint64_t& output)
{
    output = 0;
    if (node == nullptr)
    {
        return false;
    }
    if (const auto number = node->value<std::int64_t>())
    {
        if (*number < 0)
        {
            return false;
        }
        output = static_cast<std::uint64_t>(*number);
        return true;
    }
    const auto text = node->value<std::string_view>();
    if (!text || text->empty())
    {
        return false;
    }
    const auto parsed = std::from_chars(text->data(), text->data() + text->size(), output);
    return parsed.ec == std::errc{} && parsed.ptr == text->data() + text->size();
}

inline bool number(const toml::node* node, double& output)
{
    if (node == nullptr)
    {
        return false;
    }
    const auto value = node->value<double>();
    if (!value || !std::isfinite(*value))
    {
        return false;
    }
    output = *value;
    return true;
}

template <std::size_t Count> bool numbers(const toml::node* node, std::array<double, Count>& output)
{
    const auto* values = node != nullptr ? node->as_array() : nullptr;
    if (values == nullptr || values->size() != Count)
    {
        return false;
    }
    for (std::size_t index = 0; index < Count; ++index)
    {
        if (!number(values->get(index), output[index]))
        {
            return false;
        }
    }
    return true;
}

inline bool float_number(const toml::node* node, float& output)
{
    double value = 0;
    if (!number(node, value) || std::abs(value) > std::numeric_limits<float>::max())
    {
        return false;
    }
    output = static_cast<float>(value);
    return true;
}

inline bool value_union(const toml::table& table, schema::MaterialValueUnion& output,
                        std::string& error)
{
    const auto type = table["type"].value<std::string_view>();
    if (type == "float")
    {
        schema::FloatValueT value;
        if (float_number(table.get("value"), value.value))
        {
            output.Set(std::move(value));
            return true;
        }
    }
    else if (type == "v2")
    {
        std::array<double, 2> values{};
        if (numbers(table.get("value"), values))
        {
            schema::Vec2ValueT value;
            value.value = schema::Vec2(values[0], values[1]);
            output.Set(std::move(value));
            return true;
        }
    }
    else if (type == "v3")
    {
        std::array<double, 3> values{};
        if (numbers(table.get("value"), values))
        {
            schema::Vec3ValueT value;
            value.value = schema::Vec3(values[0], values[1], values[2]);
            output.Set(std::move(value));
            return true;
        }
    }
    else if (type == "v4")
    {
        std::array<double, 4> values{};
        if (numbers(table.get("value"), values))
        {
            schema::Vec4ValueT value;
            value.value = schema::Vec4(values[0], values[1], values[2], values[3]);
            output.Set(std::move(value));
            return true;
        }
    }
    error = "parameter type/value must be finite float, v2, v3 or v4";
    return false;
}

} // namespace engine::asset_pipeline
