#pragma once

#include "runtime/schema/material_generated.h"
#include "runtime/schema/shader_generated.h"

#include <array>
#include <cstddef>

namespace engine::editor::asset_pipeline
{
[[nodiscard]] bool parameter_values(const schema::MaterialValueUnion& value,
                                    std::array<double, 4>& values, std::size_t& size);
[[nodiscard]] bool valid_parameter(const schema::ShaderParameterT& definition,
                                   const schema::MaterialValueUnion& value);
} // namespace engine::editor::asset_pipeline
