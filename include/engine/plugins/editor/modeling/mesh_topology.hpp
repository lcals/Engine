#pragma once

#include "plugins/editor/modeling/editable_mesh.hpp"

#include <array>
#include <span>

namespace engine::plugins::modeling
{

enum class MeshFaceSelection
{
    connected,
    grow,
    shrink
};

namespace mesh
{
// 4x4 列主序仿射矩阵。失败保持原模型；负行列式会同步反转全部受影响面的绕序。
[[nodiscard]] bool transform(schema::EditableMeshDataT& data, const MeshSelection& selection,
                             const std::array<double, 16>& matrix, std::string& error);
[[nodiscard]] bool extrude(schema::EditableMeshDataT& data, std::span<const std::uint64_t> faces,
                           double distance, bool individual, std::string& error);
[[nodiscard]] bool extrude_edges(schema::EditableMeshDataT& data, std::span<const MeshEdge> edges,
                                 const schema::Vec3& delta, std::string& error);
[[nodiscard]] bool inset(schema::EditableMeshDataT& data, std::span<const std::uint64_t> faces,
                         double distance, std::string& error, bool group = false);
[[nodiscard]] bool erase_faces(schema::EditableMeshDataT& data,
                               std::span<const std::uint64_t> faces, std::string& error);
[[nodiscard]] bool reverse_faces(schema::EditableMeshDataT& data,
                                 std::span<const std::uint64_t> faces, std::string& error);
[[nodiscard]] bool triangulate(schema::EditableMeshDataT& data,
                               std::span<const std::uint64_t> faces, std::string& error);
[[nodiscard]] bool subdivide(schema::EditableMeshDataT& data, std::span<const std::uint64_t> faces,
                             std::uint32_t iterations, std::string& error);
[[nodiscard]] bool weld(schema::EditableMeshDataT& data, std::span<const std::uint64_t> vertices,
                        double tolerance, std::string& error);
[[nodiscard]] bool merge_vertices(schema::EditableMeshDataT& data,
                                  std::span<const std::uint64_t> vertices,
                                  const schema::Vec3& position, std::string& error);
[[nodiscard]] bool split_vertices(schema::EditableMeshDataT& data,
                                  std::span<const std::uint64_t> vertices, std::string& error);
[[nodiscard]] bool detach_faces(schema::EditableMeshDataT& data,
                                std::span<const std::uint64_t> faces, std::string& error);
[[nodiscard]] bool duplicate_faces(schema::EditableMeshDataT& data,
                                   std::span<const std::uint64_t> faces, std::string& error);
[[nodiscard]] bool bridge(schema::EditableMeshDataT& data,
                          std::span<const std::uint64_t> first_loop,
                          std::span<const std::uint64_t> second_loop, std::string& error);
[[nodiscard]] bool fill_hole(schema::EditableMeshDataT& data, std::span<const std::uint64_t> loop,
                             std::string& error);
[[nodiscard]] bool dissolve_edge(schema::EditableMeshDataT& data, MeshEdge edge,
                                 std::string& error);
[[nodiscard]] bool split_edge(schema::EditableMeshDataT& data, MeshEdge edge, double fraction,
                              std::string& error);
[[nodiscard]] bool connect_vertices(schema::EditableMeshDataT& data, std::uint64_t face,
                                    std::uint64_t first, std::uint64_t second, std::string& error);
[[nodiscard]] bool set_material(schema::EditableMeshDataT& data,
                                std::span<const std::uint64_t> faces, std::uint32_t slot,
                                std::string& error);
[[nodiscard]] bool set_color(schema::EditableMeshDataT& data, std::span<const std::uint64_t> faces,
                             const schema::Color& color, std::string& error);
[[nodiscard]] bool select_faces(const schema::EditableMeshDataT& data,
                                std::span<const std::uint64_t> faces, MeshFaceSelection mode,
                                std::vector<std::uint64_t>& output, std::string& error);
// 四边形带中连接相对边；分支、非流形或不支持的拓扑返回失败。
[[nodiscard]] bool edge_ring(const schema::EditableMeshDataT& data, MeshEdge edge,
                             std::vector<MeshEdge>& output, std::string& error);
[[nodiscard]] bool edge_loop(const schema::EditableMeshDataT& data, MeshEdge edge,
                             std::vector<MeshEdge>& output, std::string& error);
// 在连续四边形带插入 cuts 条平行切线；pinch 缩放间距，offset 平移比例。
[[nodiscard]] bool loop_cut(schema::EditableMeshDataT& data, MeshEdge edge, std::uint32_t cuts,
                            double pinch, double offset, std::string& error);
[[nodiscard]] bool append(schema::EditableMeshDataT& data, const schema::EditableMeshDataT& other,
                          std::string& error, std::uint32_t material_slot_offset = 0);
} // namespace mesh

} // namespace engine::plugins::modeling
