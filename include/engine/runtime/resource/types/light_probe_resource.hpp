#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/schema/light_probe_generated.h"

namespace engine
{
namespace resource_detail
{
template <> struct Payload<schema::AssetKind::light_probe>
{
    using Type = schema::LightProbeDataT;
};
} // namespace resource_detail
using LightProbeResource = ResourceValue<schema::LightProbeDataT, schema::AssetKind::light_probe>;
} // namespace engine
