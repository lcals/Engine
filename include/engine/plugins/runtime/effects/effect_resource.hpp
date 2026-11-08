#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/schema/effect_generated.h"

namespace engine::resource_detail
{
template <> struct Payload<schema::AssetKind::effect>
{
    using Type = schema::EffectAssetDataT;
};
} // namespace engine::resource_detail
namespace engine::plugins::effects
{
using EffectResource = ResourceValue<schema::EffectAssetDataT, schema::AssetKind::effect>;
}
