#pragma once

#include "runtime/scene/component_registry.hpp"
#include "runtime/system/system.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

struct ecs_world_t;

namespace engine
{

class Scene;
class Node;
class Resource;

enum class SceneRestoreStatus
{
    loading,
    ready,
    failed
};

struct SceneRestoreConfig
{
    std::size_t nodes_per_step = 64;
    double max_seconds = 0.001;
};

class WorldSystem final : public System
{
public:
    explicit WorldSystem(const SceneRestoreConfig& config = {}) noexcept;
    ~WorldSystem() noexcept override;

    [[nodiscard]] ComponentRegistry& component_registry() noexcept;

    // 返回借用；已有场景或世界未初始化时失败。
    [[nodiscard]] Scene* create_scene(std::string_view name);
    // 从活动资源目录加载候选，失败保留当前场景。
    [[nodiscard]] Scene* load_scene(std::string_view path);
    // 已加载资源的主线程分帧恢复；0 表示拒绝，过期请求不能影响新的候选。
    [[nodiscard]] std::uint64_t begin_restore_scene(std::shared_ptr<const Resource> resource);
    [[nodiscard]] SceneRestoreStatus step_restore_scene(std::uint64_t request,
                                                        std::size_t node_budget);
    [[nodiscard]] Scene* commit_restore_scene(std::uint64_t request);
    void cancel_restore_scene(std::uint64_t request) noexcept;
    // 卸载玩法并取消恢复候选；保留常驻子树，释放旧场景根资源。
    void unload_scene() noexcept;
    // 完整清理世界；带 ID 重载只完整清理指定的已发布场景。
    void destroy_scene() noexcept;
    [[nodiscard]] bool destroy_scene(std::uint64_t id) noexcept;
    [[nodiscard]] Scene* scene() noexcept;
    [[nodiscard]] const Scene* scene() const noexcept;
    // 包含活动场景及常驻节点的原容器，不含恢复候选；迭代器不能跨场景变更持有。
    [[nodiscard]] const std::vector<std::unique_ptr<Scene>>& scenes() const noexcept;
    [[nodiscard]] Node* find_node(std::uint64_t id) noexcept;
    [[nodiscard]] const Node* find_node(std::uint64_t id) const noexcept;
    // 临时世界模拟状态；菜单、输入和绘制仍由原有系统链更新。
    [[nodiscard]] bool set_paused(bool paused) noexcept;
    [[nodiscard]] bool paused() const noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    void retain_persistent();
    SceneRestoreConfig restore_config_;
    std::unique_ptr<Scene> restoring_;
    std::size_t restore_nodes_ = 0;
    std::size_t restore_parents_ = 0;
    bool restore_ready_ = false;
    ComponentRegistry components_;
    ecs_world_t* world_ = nullptr;
    std::vector<std::unique_ptr<Scene>> scenes_;
    Scene* scene_ = nullptr; // 借用 scenes_ 中当前玩法场景。
    std::uint64_t scene_sequence_ = 0;
    bool paused_ = false;
};

} // namespace engine
