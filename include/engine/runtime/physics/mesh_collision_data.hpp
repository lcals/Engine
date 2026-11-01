#pragma once

#include <memory>
#include <vector>

namespace engine
{

namespace schema
{
struct MeshDataT;
}

// 瞬时静态碰撞的完整分段列表；槽位稳定，nullptr 表示空槽。
// 顶点使用 Collider 局部坐标；发布后不可通过可变别名修改网格，替换须使用新快照。
// Jolt 清理退化三角后没有有效面的叶子不产生碰撞；非法索引等错误使整批失败。
struct MeshCollisionData
{
    std::vector<std::shared_ptr<const schema::MeshDataT>> parts;
};

} // namespace engine
