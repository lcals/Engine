#pragma once

#include "runtime/scene/node.hpp"

#include <map>

namespace engine
{

class ComponentRegistry final
{
public:
    template <std::derived_from<Component> T> [[nodiscard]] bool add()
    {
        if (T::component_type == ComponentType::NONE)
        {
            return false;
        }
        return factories_.emplace(T::component_type, &create_component<T>).second;
    }
    [[nodiscard]] bool contains(ComponentType type) const noexcept;
    // 返回由 node 拥有的借用；未知类型或重复组件返回空。
    [[nodiscard]] Component* create(ComponentType type, Node& node) const;

private:
    template <std::derived_from<Component> T> static Component* create_component(Node& node)
    {
        return node.add_component<T>();
    }
    using Factory = Component* (*)(Node&);
    std::map<ComponentType, Factory> factories_;
};

[[nodiscard]] bool register_scene_components(ComponentRegistry& registry);

} // namespace engine
