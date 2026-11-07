#pragma once

#include "plugins/runtime/effects/effect_resource.hpp"
#include "runtime/resource/resource_reference.hpp"

#include "runtime/resource/resource.hpp"
#include "runtime/schema/effect_generated.h"

#include <map>
#include <string>
#include <string_view>

namespace engine::plugins::effects::detail
{

using EffectBindings = std::map<std::string, resource_detail::ResourceReference, std::less<>>;

[[nodiscard]] bool load_effect_bindings(const resource_detail::ResourceReference& resource,
                                        const schema::EffectAssetDataT& asset,
                                        EffectBindings& output, std::string& error);
[[nodiscard]] std::string effect_dependency_path(std::string_view path);
[[nodiscard]] std::string effect_utf8(const char16_t* path);

} // namespace engine::plugins::effects::detail
