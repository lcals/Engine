#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/schema/destruction_generated.h"

namespace engine::resource_detail
{
template <> struct Payload<schema::AssetKind::destruction>
{
    using Type = schema::DestructionAssetDataT;
};
} // namespace engine::resource_detail
namespace engine::plugins::destruction
{
using DestructionResource =
    ResourceValue<schema::DestructionAssetDataT, schema::AssetKind::destruction>;
}
