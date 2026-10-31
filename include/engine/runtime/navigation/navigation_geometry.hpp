#pragma once

#include <array>

namespace engine::schema
{
struct MeshDataT;
}

namespace engine
{

struct NavigationGeometry
{
    // 仅在同步 build/replace 调用期间借用，不保留指针；空几何集合表示没有表面。
    const schema::MeshDataT* mesh = nullptr;
    // 列主序仿射矩阵，局部位置映射到世界坐标。
    std::array<float, 16> transform{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

} // namespace engine
