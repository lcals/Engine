#pragma once

#include "runtime/resource/resource_reference.hpp"

#include "runtime/scene/collider.hpp"
#include "runtime/scene/component_runtime.hpp"

namespace engine
{
struct ColliderRuntime
{
    ResourceReference mesh_;
    std::shared_ptr<const schema::MeshDataT> transient_mesh_;
    std::shared_ptr<const MeshCollisionData> mesh_parts_;
};
} // namespace engine
