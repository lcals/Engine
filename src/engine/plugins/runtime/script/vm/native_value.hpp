#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace engine::plugins::script::vm
{

// 只承载原生调用的返回数据；不拥有虚拟机句柄。uint64_t 专用于 BigInt 身份。
struct NativeValue
{
    using Array = std::vector<NativeValue>;
    using Object = std::vector<std::pair<std::string, NativeValue>>;
    std::variant<std::monostate, bool, double, std::uint64_t, std::string, Array, Object> data;

    NativeValue() = default;
    NativeValue(bool value) : data(value)
    {
    }
    NativeValue(double value) : data(value)
    {
    }
    NativeValue(std::int32_t value) : data(static_cast<double>(value))
    {
    }
    NativeValue(std::uint32_t value) : data(static_cast<double>(value))
    {
    }
    NativeValue(std::uint64_t value) : data(value)
    {
    }
    NativeValue(std::string_view value) : data(std::string(value))
    {
    }
    NativeValue(const char* value) : data(std::string(value))
    {
    }
    NativeValue(std::string value) : data(std::move(value))
    {
    }
    NativeValue(Array value) : data(std::move(value))
    {
    }
    NativeValue(Object value) : data(std::move(value))
    {
    }
};

} // namespace engine::plugins::script::vm
