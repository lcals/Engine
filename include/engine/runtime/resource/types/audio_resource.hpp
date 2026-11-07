#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/schema/audio_generated.h"

namespace engine
{
namespace resource_detail
{
template <> struct Payload<schema::AssetKind::audio>
{
    using Type = schema::AudioClipDataT;
};
} // namespace resource_detail
using AudioResource = ResourceValue<schema::AudioClipDataT, schema::AssetKind::audio>;
} // namespace engine
