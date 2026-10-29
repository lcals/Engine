#pragma once

#include "runtime/scene/scene_types.hpp"

namespace engine
{

class Node;

class Component
{
public:
    virtual ~Component() noexcept = 0;

    Component(const Component&) = delete;
    Component& operator=(const Component&) = delete;
    Component(Component&&) = delete;
    Component& operator=(Component&&) = delete;

    [[nodiscard]] Node& node() noexcept;
    [[nodiscard]] const Node& node() const noexcept;
    [[nodiscard]] ComponentType type() const noexcept;
    [[nodiscard]] bool enabled() const noexcept;
    void set_enabled(bool enabled) noexcept;
    [[nodiscard]] const schema::ComponentDataT& data() const noexcept;
    // 快照失败输出为空；恢复失败保持原组件状态。
    [[nodiscard]] virtual bool build_data(schema::ComponentDataT& output) const;
    [[nodiscard]] virtual bool restore_data(const schema::ComponentDataT& data) = 0;

protected:
    explicit Component(Node& node, schema::ComponentDataT data = {}) noexcept;
    // 由 Node 在转入组件列表之前调用，失败则销毁尚未发布的组件。
    [[nodiscard]] virtual bool initialize();
    [[nodiscard]] schema::ComponentDataT& mutable_data() noexcept;

private:
    friend class Node;
    Node& node_;
    schema::ComponentDataT data_{};
};

} // namespace engine
