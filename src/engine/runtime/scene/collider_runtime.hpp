#pragma once

#include "runtime/scene/collider.hpp"
#include "runtime/scene/component_runtime.hpp"

namespace engine
{
struct ColliderRuntime
{
    std::shared_ptr<const Resource> mesh_;
    std::shared_ptr<const schema::MeshDataT> transient_mesh_;
    std::shared_ptr<const MeshCollisionData> mesh_parts_;
};
} // namespace engine
