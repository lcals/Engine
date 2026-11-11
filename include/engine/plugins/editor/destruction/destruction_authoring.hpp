#pragma once

#include "editor/schema/destruction_authoring_generated.h"
#include "runtime/schema/destruction_generated.h"

#include <string>

namespace engine::plugins::destruction
{
// Editor-only bridge to official Blast ExtAuthoring. All generated vertices stay asset-local.
class DestructionAuthoring final
{
public:
    [[nodiscard]] static bool generate(const schema::DestructionGeneratorDataT& settings,
                                       schema::DestructionAssetDataT& output, std::string& error);
};
} // namespace engine::plugins::destruction
