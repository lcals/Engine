#pragma once

#include "plugins/editor/modeling/editable_mesh.hpp"

#include <span>

namespace engine
{

enum class MeshBooleanOperation
{
    unite,
    subtract,
    intersect
};

enum class MeshUvProjection
{
    planar,
    box,
    cylindrical,
    spherical
};

struct MeshSweepOptions
{
    std::vector<schema::Vec2> profile;
    std::vector<schema::Vec3> path;
    bool closed_profile = true;
    bool cap = true;
    double twist_degrees = 0.0;
};

struct MeshLatheOptions
{
    // x 是距 Y 轴的半径，y 是高度；按从下到上的顺序提供轮廓。
    std::vector<schema::Vec2> profile;
    std::uint32_t segments = 32;
    double angle_degrees = 360.0;
    bool cap = true;
};

struct MeshMirrorOptions
{
    schema::Vec3 normal = {1.0, 0.0, 0.0};
    double offset = 0.0;
    bool duplicate = true;
    bool weld = true;
    double tolerance = 0.000001;
    // 持续镜像模式要求源模型只占镜像平面的一侧；一次复制镜像可关闭此约束。
    bool require_half_space = false;
};

struct MeshClipOptions
{
    schema::Vec3 normal = {1.0, 0.0, 0.0};
    double offset = 0.0;
    bool keep_positive = true;
    bool cap = true;
    std::uint32_t cap_material_slot = 0;
};

struct MeshBevelOptions
{
    std::vector<MeshEdge> edges;
    double width = 0.1;
    std::uint32_t segments = 1;
    double profile = 0.5;
    std::uint32_t material_slot = 0;
};

struct MeshUvProjectionOptions
{
    MeshUvProjection projection = MeshUvProjection::planar;
    schema::Vec3 normal = {0.0, 1.0, 0.0};
    schema::Vec2 scale = {1.0, 1.0};
    schema::Vec2 offset = {0.0, 0.0};
};

struct MeshUvTransformOptions
{
    schema::Vec2 scale = {1.0, 1.0};
    schema::Vec2 offset = {0.0, 0.0};
    schema::Vec2 pivot = {0.5, 0.5};
    double rotation_degrees = 0.0;
};

struct MeshUvUnwrapOptions
{
    double padding = 0.01;
    std::uint32_t resolution = 1024;
    std::uint32_t iterations = 2;
    bool rotate = true;
    bool respect_existing_seams = false;
    std::vector<MeshEdge> seams;
};

struct MeshHotspotOptions
{
    // 每个 Vec4 为归一化图集中的 x、y、width、height。
    std::vector<schema::Vec4> rectangles;
    double padding = 0.0;
    bool allow_rotation = true;
};

class MeshGeometry final
{
public:
    // 操作仅依赖模型数据。所有失败均保持传入模型/输出模型不变。
    [[nodiscard]] static bool sweep(const MeshSweepOptions& options,
                                    schema::EditableMeshDataT& output, std::string& error);
    [[nodiscard]] static bool lathe(const MeshLatheOptions& options,
                                    schema::EditableMeshDataT& output, std::string& error);
    [[nodiscard]] static bool mirror(schema::EditableMeshDataT& data,
                                     const MeshMirrorOptions& options, std::string& error);
    [[nodiscard]] static bool clip(schema::EditableMeshDataT& data, const MeshClipOptions& options,
                                   std::string& error);
    [[nodiscard]] static bool boolean_operation(schema::EditableMeshDataT& data,
                                                const schema::EditableMeshDataT& other,
                                                MeshBooleanOperation operation, std::string& error);
    // 选择真实拓扑边；当前接受闭合凸多面体，不对凹体执行全局切平面近似。
    [[nodiscard]] static bool bevel(schema::EditableMeshDataT& data,
                                    const MeshBevelOptions& options, std::string& error);
    [[nodiscard]] static bool project_uv(schema::EditableMeshDataT& data,
                                         std::span<const std::uint64_t> faces,
                                         const MeshUvProjectionOptions& options,
                                         std::string& error);
    [[nodiscard]] static bool transform_uv(schema::EditableMeshDataT& data,
                                           std::span<const std::uint64_t> faces,
                                           const MeshUvTransformOptions& options,
                                           std::string& error);
    [[nodiscard]] static bool unwrap_uv(schema::EditableMeshDataT& data,
                                        std::span<const std::uint64_t> faces,
                                        const MeshUvUnwrapOptions& options, std::string& error);
    // 独立平面岛是另一项操作，不代替 xatlas 跨面图表展开。
    [[nodiscard]] static bool unwrap_face_uv(schema::EditableMeshDataT& data,
                                             std::span<const std::uint64_t> faces,
                                             const MeshUvUnwrapOptions& options,
                                             std::string& error);
    [[nodiscard]] static bool hotspot_uv(schema::EditableMeshDataT& data,
                                         std::span<const std::uint64_t> faces,
                                         const MeshHotspotOptions& options, std::string& error);
    [[nodiscard]] static bool set_smoothing(schema::EditableMeshDataT& data,
                                            std::span<const std::uint64_t> faces,
                                            std::uint32_t group, std::string& error);
    [[nodiscard]] static bool auto_smooth(schema::EditableMeshDataT& data, double angle_degrees,
                                          std::string& error);
};

} // namespace engine
