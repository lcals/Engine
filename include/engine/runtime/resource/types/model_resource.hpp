#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/schema/model_generated.h"

namespace engine
{
namespace resource_detail
{
template <> struct Payload<schema::AssetKind::model>
{
    using Type = schema::ModelDataT;
};
} // namespace resource_detail
using ModelResource = ResourceValue<schema::ModelDataT, schema::AssetKind::model>;
} // namespace engine
