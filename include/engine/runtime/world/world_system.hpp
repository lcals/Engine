#pragma once

#include "runtime/resource/resource_handle.hpp"
#include "runtime/scene/component_registry.hpp"
#include "runtime/scene/scene_loading.hpp"
#include "runtime/system/system.hpp"
#include <map>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

struct ecs_world_t;
struct ecs_query_t;

namespace engine
{

class Scene;
class Node;
class Resource;
class ResourceLoader;
namespace ecs_detail
{
struct ResourceBindingScope;
}

struct SceneRestoreConfig
{
    std::size_t nodes_per_step = 64;
    double max_seconds = 0.001;
};

class WorldSystem final : public System
{
public:
    explicit WorldSystem(ResourceLoader* resources = nullptr,
                         const SceneRestoreConfig& config = {}) noexcept;
    ~WorldSystem() noexcept override;

    [[nodiscard]] ComponentRegistry& component_registry() noexcept;
    [[nodiscard]] ResourceLoader* resource_loader() const noexcept;

    // 返回借用；已有场景或世界未初始化时失败。
    [[nodiscard]] Scene* create_scene(std::string_view name);
    // 只取得已加载的候选，失败保留当前场景；不读文件。
    [[nodiscard]] Scene* load_scene(std::string_view path);
    [[nodiscard]] bool begin_scope(SceneLoadKind kind, std::uint64_t destination,
                                   SceneScopeId& output);
    [[nodiscard]] bool restore_scope(SceneScopeId scope, const ResourceHandle& resource);
    [[nodiscard]] SceneWorkState step_scope(SceneScopeId scope, const SceneWorkBudget& budget);
    [[nodiscard]] Scene* scope_candidate(SceneScopeId scope) noexcept;
    [[nodiscard]] bool seal_scope(SceneScopeId scope);
    [[nodiscard]] bool scope_status(SceneScopeId scope, ScenePreparationSnapshot& output) const;
    [[nodiscard]] bool publish_scope(SceneScopeId scope);
    [[nodiscard]] bool activate_scope(SceneScopeId scope);
    [[nodiscard]] bool cancel_scope(SceneScopeId scope);
    void release_scope(SceneScopeId scope) noexcept;

    // 卸载玩法并取消恢复候选；保留常驻子树，释放旧场景根资源。
    void unload_scene() noexcept;
    // 完整清理世界；带 ID 重载只完整清理指定的已发布场景。
    void destroy_scene() noexcept;
    [[nodiscard]] bool destroy_scene(std::uint64_t id) noexcept;
    [[nodiscard]] Scene* scene() noexcept;
    [[nodiscard]] const Scene* scene() const noexcept;
    // 包含活动场景及常驻节点的原容器，不含恢复候选；迭代器不能跨场景变更持有。
    [[nodiscard]] const std::vector<std::unique_ptr<Scene>>& scenes() const noexcept;
    [[nodiscard]] Node find_node(std::uint64_t id) const noexcept;
    [[nodiscard]] ecs_world_t* native_world() const noexcept;
    [[nodiscard]] std::uint64_t epoch() const noexcept;
    [[nodiscard]] bool can_modify() const noexcept;
    [[nodiscard]] bool refresh_scene_state();
    void invalidate_scene_state() noexcept;
    // 临时世界模拟状态；菜单、输入和绘制仍由原有系统链更新。
    [[nodiscard]] bool set_paused(bool paused) noexcept;
    [[nodiscard]] bool paused() const noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    friend struct ecs_detail::ResourceBindingScope;
    std::vector<std::pair<Node, bool>> retirement_;
    struct Scope;
    std::map<std::uint64_t, std::unique_ptr<Scope>> scopes_;
    ResourceLoader* resources_ = nullptr;
    ResourceHandle binding_owner_;
    void retain_persistent();
    SceneRestoreConfig restore_config_;
    ComponentRegistry components_;
    ecs_world_t* world_ = nullptr;
    ecs_query_t* state_query_ = nullptr;
    std::uint64_t world_epoch_ = 0;
    std::uint64_t state_revision_ = 1;
    std::uint64_t prepared_revision_ = 0;
    std::vector<std::unique_ptr<Scene>> scenes_;
    Scene* scene_ = nullptr; // 借用 scenes_ 中当前玩法场景。
    std::uint64_t scene_sequence_ = 0;
    bool paused_ = false;
};

} // namespace engine
