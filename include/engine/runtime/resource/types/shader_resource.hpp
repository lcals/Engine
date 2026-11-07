#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/schema/shader_generated.h"

namespace engine
{
namespace resource_detail
{
template <> struct Payload<schema::AssetKind::shader>
{
    using Type = schema::ShaderDataT;
};
} // namespace resource_detail
using ShaderResource = ResourceValue<schema::ShaderDataT, schema::AssetKind::shader>;
} // namespace engine
