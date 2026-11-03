#pragma once

#include "runtime/scene/component.hpp"

#include <concepts>
#include <cstdint>
#include <memory>
#include <new>
#include <string_view>
#include <utility>
#include <vector>

namespace engine
{

class Scene;

class Node final
{
public:
    ~Node() noexcept;

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    Node(Node&&) = delete;
    Node& operator=(Node&&) = delete;

    [[nodiscard]] std::uint64_t id() const noexcept;
    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] const schema::NodeInfoT& info() const noexcept;
    [[nodiscard]] bool enabled() const noexcept;
    void set_enabled(bool enabled) noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool visible() const noexcept;
    void set_visible(bool visible) noexcept;
    [[nodiscard]] bool visible_in_hierarchy() const noexcept;
    [[nodiscard]] Scene& scene() noexcept;
    [[nodiscard]] const Scene& scene() const noexcept;
    [[nodiscard]] Node* parent() noexcept;
    [[nodiscard]] const Node* parent() const noexcept;
    // 保留局部变换；空指针解除父子关系。跨场景或形成循环时返回 false。
    [[nodiscard]] bool set_parent(Node* parent) noexcept;
    // 只影响场景保存；有效瞬态状态包含祖先标记，渲染和物理行为不变。
    [[nodiscard]] bool transient() const noexcept;
    void set_transient(bool transient) noexcept;
    // 只允许根节点设置；整棵子树跨场景卸载保留，不影响保存策略。
    [[nodiscard]] bool set_persistent(bool value) noexcept;
    [[nodiscard]] bool persistent() const noexcept;

    // 返回借用，由本节点独占；每类组件至多一个，重复添加返回空指针。
    template <std::derived_from<Component> T> [[nodiscard]] T* add_component()
    {
        if (get_component(T::component_type) != nullptr)
        {
            return nullptr;
        }
        std::unique_ptr<T> component(new (std::nothrow) T(*this));
        if (component == nullptr || component->instance_id() == 0 ||
            !static_cast<Component&>(*component).initialize())
        {
            return nullptr;
        }
        T* result = component.get();
        components_.push_back(std::move(component));
        return result;
    }

    template <std::derived_from<Component> T> [[nodiscard]] T* get_component() noexcept
    {
        return static_cast<T*>(get_component(T::component_type));
    }

    template <std::derived_from<Component> T> [[nodiscard]] const T* get_component() const noexcept
    {
        return static_cast<const T*>(get_component(T::component_type));
    }

    // 销毁指定类型组件，缺失返回 false；该组件的既有借用立即失效。
    [[nodiscard]] bool remove_component(ComponentType type) noexcept;
    [[nodiscard]] Component* get_component(ComponentType type) noexcept;
    [[nodiscard]] const Component* get_component(ComponentType type) const noexcept;
    [[nodiscard]] const std::vector<std::unique_ptr<Component>>& components() const noexcept;

private:
    friend class Scene;
    Node(Scene& scene, std::uint64_t id, std::uint64_t local_id, std::string_view name);

    Scene& scene_;
    std::uint64_t entity_id_;
    schema::NodeInfoT info_{};
    bool transient_ = false;
    bool persistent_ = false;
    std::vector<std::unique_ptr<Component>> components_;
};

} // namespace engine
