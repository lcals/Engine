#pragma once

#include "runtime/schema/mesh_generated.h"
#include "runtime/schema/modeling_generated.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine
{

struct MeshEdge
{
    std::uint64_t first = 0;
    std::uint64_t second = 0;
};

struct MeshSelection
{
    std::vector<std::uint64_t> vertices;
    std::vector<MeshEdge> edges;
    std::vector<std::uint64_t> faces;
    bool all = false;
};

class EditableMesh final
{
public:
    // 空模型合法；编译时需要至少一个面。所有失败输出保持空值。
    [[nodiscard]] static bool validate(const schema::EditableMeshDataT& data, std::string& error);
    [[nodiscard]] static bool compile(const schema::EditableMeshDataT& data, std::string_view path,
                                      schema::MeshDataT& output, std::string& error);
    [[nodiscard]] static bool from_mesh(const schema::MeshDataT& data,
                                        schema::EditableMeshDataT& output, std::string& error);
    [[nodiscard]] static bool serialize(const schema::EditableMeshDataT& data,
                                        std::vector<std::uint8_t>& output, std::string& error);
    [[nodiscard]] static bool deserialize(std::span<const std::uint8_t> bytes,
                                          schema::EditableMeshDataT& output, std::string& error);
    [[nodiscard]] static const schema::ModelingVertex*
    find_vertex(const schema::EditableMeshDataT& data, std::uint64_t id) noexcept;
    [[nodiscard]] static schema::ModelingFaceT* find_face(schema::EditableMeshDataT& data,
                                                          std::uint64_t id) noexcept;
    [[nodiscard]] static const schema::ModelingFaceT*
    find_face(const schema::EditableMeshDataT& data, std::uint64_t id) noexcept;
    [[nodiscard]] static bool selected_vertices(const schema::EditableMeshDataT& data,
                                                const MeshSelection& selection,
                                                std::vector<std::uint64_t>& output,
                                                std::string& error);
    // 面的顶点朝向决定正面；支持凹面、洞。输出为外环后依次各洞的角索引。
    [[nodiscard]] static bool triangulate_face(const schema::EditableMeshDataT& data,
                                               const schema::ModelingFaceT& face,
                                               std::vector<std::uint32_t>& output,
                                               std::string& error);
};

} // namespace engine
