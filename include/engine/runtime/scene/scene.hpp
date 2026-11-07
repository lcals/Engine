#pragma once

#include "runtime/resource/resource_handle.hpp"
#include "runtime/scene/node.hpp"

#include <memory>
#include <span>
#include <unordered_map>

namespace engine
{
class WorldSystem;
class Camera;
class Resource;

class Scene final
{
public:
    ~Scene() noexcept;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    [[nodiscard]] std::uint64_t id() const noexcept;
    [[nodiscard]] std::string name() const;
    [[nodiscard]] WorldSystem& world() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] Node create_node(std::string_view name);
    [[nodiscard]] Node instantiate_model(std::string_view path, std::string_view name,
                                         std::vector<Node>* created = nullptr);
    [[nodiscard]] Node instantiate_prefab(std::string_view path, std::string_view name,
                                          std::vector<Node>* created = nullptr);
    [[nodiscard]] bool
    collect_prefab_instances(std::vector<schema::PrefabInstanceDataT>& output) const;
    [[nodiscard]] bool set_prefab_instance(const schema::PrefabInstanceDataT& instance);
    [[nodiscard]] bool remove_prefab_instance(std::uint64_t root_local_id) noexcept;
    [[nodiscard]] Node find_node(std::uint64_t id) const noexcept;
    [[nodiscard]] Node find_local_node(std::uint64_t local_id) const noexcept;
    [[nodiscard]] Node find_node(std::string_view name) const noexcept;
    [[nodiscard]] bool destroy_node(std::uint64_t id);
    [[nodiscard]] bool collect_nodes(std::vector<Node>& output) const;
    [[nodiscard]] Camera active_camera() const noexcept;
    [[nodiscard]] bool set_active_camera(Camera camera) noexcept;
    [[nodiscard]] bool build_data(schema::SceneDataT& output) const;

private:
    friend class WorldSystem;
    friend class Node;
    Scene(WorldSystem& world, std::uint64_t id) noexcept;
    [[nodiscard]] bool initialize(std::string_view name);
    [[nodiscard]] bool publish();
    [[nodiscard]] Node create_node(std::string_view name, std::uint64_t local_id);
    [[nodiscard]] bool restore_data(const schema::SceneDataT& data);
    [[nodiscard]] bool restore_node(const schema::NodeDataT& data,
                                    std::span<const schema::NodeIdMappingT> mapping = {});
    [[nodiscard]] bool restore_parent(const schema::NodeDataT& data);
    [[nodiscard]] bool finish_restore(const schema::SceneDataT& data);
    [[nodiscard]] bool valid_prefab_instance(const schema::PrefabInstanceDataT& instance) const;
    [[nodiscard]] schema::SceneInfoT* mutable_info() const noexcept;
    void retain_persistent();
    void unregister_entity(std::uint64_t entity, std::uint64_t local_id) noexcept;
    void rollback_created(std::uint64_t first_order) noexcept;
    [[nodiscard]] bool collect_created(std::uint64_t first_order, std::vector<Node>& output) const;

    WorldSystem& world_;
    std::uint64_t id_ = 0;
    std::uint64_t metadata_entity_ = 0;
    std::uint64_t node_sequence_ = 0;
    std::uint64_t order_sequence_ = 0;
    std::uint64_t prefab_sequence_ = 0;
    bool published_ = false;
    ResourceHandle resource_;
    std::unordered_map<std::uint64_t, std::uint64_t> local_node_index_;
};
} // namespace engine
