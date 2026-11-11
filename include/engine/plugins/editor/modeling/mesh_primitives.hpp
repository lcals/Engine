#pragma once

#include "plugins/editor/modeling/editable_mesh.hpp"

namespace engine::plugins::modeling
{

namespace mesh
{
[[nodiscard]] bool create(const schema::ModelingPrimitiveDataT& options,
                          schema::EditableMeshDataT& output, std::string& error);
// 以输入绕序为正面；洞与外环必须共面且不相交。
[[nodiscard]] bool polygon(std::span<const schema::Vec3> points,
                           const std::vector<std::vector<schema::Vec3>>& holes,
                           schema::EditableMeshDataT& output, std::string& error);
} // namespace mesh

} // namespace engine::plugins::modeling
