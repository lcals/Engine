#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/schema/ui_generated.h"

namespace engine
{
namespace resource_detail
{
template <> struct Payload<schema::AssetKind::ui>
{
    using Type = schema::UiDocumentDataT;
};
} // namespace resource_detail
using UiResource = ResourceValue<schema::UiDocumentDataT, schema::AssetKind::ui>;
} // namespace engine
