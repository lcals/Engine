#pragma once

#include "runtime/schema/destruction_generated.h"

#include <string>
#include <string_view>

namespace engine::plugins::destruction
{

[[nodiscard]] bool read_destruction_source(std::string_view text,
                                           schema::DestructionGeneratorDataT& output,
                                           std::string& error);

} // namespace engine::plugins::destruction
