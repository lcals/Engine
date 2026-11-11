#pragma once

#include "editor/schema/modeling_generated.h"

#include <span>
#include <string>
#include <string_view>

namespace engine::plugins::modeling::detail
{
// 仅用于生成参数的映射与约束，不保存另一份持久模型。
struct PrimitiveParameter
{
    std::string_view name;
    std::uint32_t shapes;
    double schema::ModelingPrimitiveDataT::* scalar{};
    std::uint32_t schema::ModelingPrimitiveDataT::* count{};
    int size_axis{-1}; // 0..2 为单轴，3 为完整 size。
    double minimum{};
    double maximum{};
    bool exclusive_minimum{};
};

std::span<const PrimitiveParameter> primitive_parameters();
bool supports(const PrimitiveParameter& parameter, schema::ModelingPrimitive type);
[[nodiscard]] bool validate_primitive(const schema::ModelingPrimitiveDataT& options,
                                      std::string& error);
} // namespace engine::plugins::modeling::detail
