#pragma once

#include "plugins/runtime/script/vm/native_value.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace engine::plugins::script::vm
{

class Arguments
{
public:
    virtual ~Arguments() noexcept = default;
    [[nodiscard]] virtual int count() const noexcept = 0;
    // 可空借用，仅在本次回调有效，指向该 Run 的 BindingContext。
    [[nodiscard]] virtual void* owner() const noexcept = 0;
    [[nodiscard]] virtual std::int32_t data() const noexcept = 0;
    [[nodiscard]] virtual bool is_boolean(int index) const = 0;
    [[nodiscard]] virtual bool boolean(int index) const = 0;
    [[nodiscard]] virtual bool is_int32(int index) const = 0;
    [[nodiscard]] virtual std::int32_t integer(int index) const = 0;
    [[nodiscard]] virtual bool is_uint32(int index) const = 0;
    [[nodiscard]] virtual std::uint32_t unsigned_integer(int index) const = 0;
    [[nodiscard]] virtual bool identifier(int index, std::uint64_t& output) const = 0;
    [[nodiscard]] virtual bool number(int index, double& output) const = 0;
    [[nodiscard]] virtual bool text(int index, std::string& output) const = 0;
    // 转换失败由 VM 记录 JavaScript 异常，外层调用通过返回值报告失败。
    virtual void set_return(const NativeValue& value) const = 0;
    virtual void type_error(std::string_view message) const = 0;
};

} // namespace engine::plugins::script::vm
