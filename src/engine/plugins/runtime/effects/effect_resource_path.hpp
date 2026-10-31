#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/schema/effect_generated.h"

#include <map>
#include <string>
#include <string_view>

namespace engine::plugins::effects::detail
{

using EffectBindings = std::map<std::string, ResourcePtr, std::less<>>;

[[nodiscard]] bool read_effect_asset(ByteView bytes, const schema::EffectAssetData*& output,
                                     std::string& error);
[[nodiscard]] bool load_effect_bindings(const Resource& resource,
                                        const schema::EffectAssetData& asset,
                                        EffectBindings& output, std::string& error);
[[nodiscard]] std::string effect_dependency_path(std::string_view path);
[[nodiscard]] std::string effect_utf8(const char16_t* path);

} // namespace engine::plugins::effects::detail
