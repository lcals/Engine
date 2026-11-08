#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/schema/font_generated.h"

namespace engine
{
namespace resource_detail
{
template <> struct Payload<schema::AssetKind::font>
{
    using Type = schema::FontDataT;
};
} // namespace resource_detail
using FontResource = ResourceValue<schema::FontDataT, schema::AssetKind::font>;
} // namespace engine
