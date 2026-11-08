#pragma once

#include "runtime/resource/resource_reference.hpp"

#include "runtime/scene/component_runtime.hpp"
#include "runtime/scene/material_state.hpp"
#include "runtime/scene/mesh_renderer.hpp"

namespace engine
{
struct MeshRendererRuntime
{
    ResourceReference mesh_resource_;
    ResourceReference lightmap_resource_;
    std::vector<MaterialState> materials_;
    std::uint64_t next_material_identity_ = 1;
    std::vector<schema::MeshVertex> deformed_vertices_;
    std::vector<Vec4> deformed_tangents_;
    std::vector<std::array<float, 16>> skinning_palette_;
    std::vector<std::array<float, 16>> skinning_normal_palette_;
    std::uint32_t skin_joint_count_ = 0;
    std::uint64_t mesh_revision_ = 0;
    std::uint64_t deformation_revision_ = 0;
};
} // namespace engine
