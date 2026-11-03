#pragma once

#include "runtime/scene/node.hpp"

#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

struct ecs_world_t;

namespace engine
{

class WorldSystem;
class Camera;
class Resource;
class ComponentRegistry;

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
    // 对象包展开为普通节点；失败删除本次创建的层级，保留原场景。
    [[nodiscard]] Node* instantiate_model(std::string_view path, std::string_view name);
    // 实例化普通节点子树并保留来源；失败回滚全部候选节点。非空名称成为显式覆盖。
    [[nodiscard]] Node* instantiate_prefab(std::string_view path, std::string_view name);
    [[nodiscard]] const std::vector<std::unique_ptr<schema::PrefabInstanceDataT>>&
    prefab_instances() const noexcept;
    // 仅登记来源和显式覆盖，不修改节点；拒绝无效映射、重叠或嵌套实例。
    [[nodiscard]] bool set_prefab_instance(const schema::PrefabInstanceDataT& instance);
    // 仅解除来源关联，保留实例节点与组件。
    [[nodiscard]] bool remove_prefab_instance(std::uint64_t root_local_id) noexcept;
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
    Scene(ecs_world_t& world, std::uint64_t id, std::string_view name,
          const ComponentRegistry& components);
    [[nodiscard]] Node* create_node(std::string_view name, std::uint64_t local_id);
    [[nodiscard]] Node* find_local_node(std::uint64_t local_id) const noexcept;
    [[nodiscard]] bool restore_data(const schema::SceneDataT& data);
    [[nodiscard]] bool restore_node(const schema::NodeDataT& data,
                                    std::span<const schema::NodeIdMappingT> mapping = {});
    [[nodiscard]] bool restore_parent(const schema::NodeDataT& data);
    [[nodiscard]] bool finish_restore(const schema::SceneDataT& data);
    [[nodiscard]] bool valid_prefab_instance(const schema::PrefabInstanceDataT& instance) const;
    void retain_persistent();
    void destroy_entity(std::uint64_t id, std::uint64_t local_id) noexcept;

    ecs_world_t& world_;
    const ComponentRegistry& components_;
    std::uint64_t id_;
    std::uint64_t node_sequence_ = 0;
    std::shared_ptr<const Resource> resource_;
    schema::SceneInfoT info_{};
    // Borrow nodes_; a node unregisters both identities during destruction.
    std::unordered_map<std::uint64_t, Node*> node_index_;
    std::unordered_map<std::uint64_t, Node*> local_node_index_;
    std::vector<std::unique_ptr<Node>> nodes_;
    std::vector<std::unique_ptr<schema::PrefabInstanceDataT>> prefab_instances_;
};

} // namespace engine
