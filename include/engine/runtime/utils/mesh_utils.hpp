#pragma once

#include "runtime/schema/mesh_generated.h"

#include <vector>

namespace engine
{

class MeshUtils final
{
public:
    // 只校验几何，资源路径及身份由调用方校验；动态网格允许空路径。
    [[nodiscard]] static bool validate(const schema::MeshDataT& data) noexcept;
    // 成功输出每顶点归一化切线，w 为手性；失败输出为空。
    [[nodiscard]] static bool build_tangents(const schema::MeshDataT& data,
                                             std::vector<schema::Vec4>& output);
};

} // namespace engine
