#pragma once

#include "runtime/schema/shader_generated.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace engine::resource_codec_detail
{

inline bool parameter_values(const schema::MaterialValueUnion& value, std::array<double, 4>& output,
                             std::uint8_t& size)
{
    output = {};
    size = 0;
    if (value.value == nullptr)
    {
        return false;
    }
    switch (value.type)
    {
    case schema::MaterialValue::FloatValue:
        output[0] = value.AsFloatValue()->value;
        size = 1;
        break;
    case schema::MaterialValue::Vec2Value:
    {
        const auto& vector = value.AsVec2Value()->value;
        output = {vector.x(), vector.y(), 0.0, 0.0};
        size = 2;
        break;
    }
    case schema::MaterialValue::Vec3Value:
    {
        const auto& vector = value.AsVec3Value()->value;
        output = {vector.x(), vector.y(), vector.z(), 0.0};
        size = 3;
        break;
    }
    case schema::MaterialValue::Vec4Value:
    {
        const auto& vector = value.AsVec4Value()->value;
        output = {vector.x(), vector.y(), vector.z(), vector.w()};
        size = 4;
        break;
    }
    default:
        return false;
    }
    return std::all_of(output.begin(), output.begin() + size,
                       [](double item)
                       {
                           constexpr double maximum = std::numeric_limits<float>::max();
                           return std::isfinite(item) && item >= -maximum && item <= maximum;
                       });
}

inline bool valid_parameter_value(const schema::ShaderParameterT& definition,
                                  const schema::MaterialValueUnion& value)
{
    std::array<double, 4> values{};
    std::uint8_t count = 0;
    return value.type == definition.default_value.type && parameter_values(value, values, count) &&
           (!definition.bounded ||
            std::all_of(values.begin(), values.begin() + count, [&definition](double item)
                        { return item >= definition.minimum && item <= definition.maximum; }));
}

} // namespace engine::resource_codec_detail
