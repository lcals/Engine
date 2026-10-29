#pragma once

#include "runtime/scene/node.hpp"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

struct ecs_world_t;

namespace engine
{

class WorldSystem;
class Camera;

class Scene final
{
public:
    ~Scene() noexcept;

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    [[nodiscard]] std::uint64_t id() const noexcept;
    [[nodiscard]] std::string_view name() const noexcept;
    // 返回借用；节点或场景销毁后失效。新节点自动附加 Transform。
    [[nodiscard]] Node* create_node(std::string_view name);
    [[nodiscard]] Node* find_node(std::uint64_t id) noexcept;
    [[nodiscard]] const Node* find_node(std::uint64_t id) const noexcept;
    // 连同后代一起删除，不存在时返回 false。
    [[nodiscard]] bool destroy_node(std::uint64_t id);
    [[nodiscard]] const std::vector<std::unique_ptr<Node>>& nodes() const noexcept;
    [[nodiscard]] Camera* active_camera() noexcept;
    [[nodiscard]] const Camera* active_camera() const noexcept;
    // 允许空指针；拒绝其他场景的相机。添加 Camera 不隐式选择活动相机。
    [[nodiscard]] bool set_active_camera(Camera* camera) noexcept;
    // 创建独立的 FlatBuffers 原生数据快照；失败输出为空，不写入磁盘。
    [[nodiscard]] bool build_data(schema::SceneDataT& output) const;

private:
    friend class WorldSystem;
    friend class Node;
    Scene(ecs_world_t& world, std::uint64_t id, std::string_view name);
    void destroy_entity(std::uint64_t id) noexcept;

    ecs_world_t& world_;
    std::uint64_t id_;
    schema::SceneInfoT info_{};
    std::vector<std::unique_ptr<Node>> nodes_;
};

} // namespace engine
