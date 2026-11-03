#pragma once

#include "runtime/scene/node.hpp"

#include <span>

namespace engine
{
class ComponentRegistry;
struct ComponentState;
template <class T, class Data, class Runtime> bool register_component_storage(ComponentRegistry&);

class Component
{
public:
    Component() noexcept = default;
    [[nodiscard]] bool valid() const noexcept;
    explicit operator bool() const noexcept
    {
        return valid();
    }
    [[nodiscard]] bool operator==(const Component&) const noexcept = default;
    [[nodiscard]] Node node() const noexcept;
    [[nodiscard]] std::uint64_t instance_id() const noexcept;
    [[nodiscard]] ComponentType type() const noexcept;
    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] bool set_enabled(bool enabled) noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool visible() const noexcept;
    [[nodiscard]] bool set_visible(bool visible) noexcept;
    [[nodiscard]] bool visible_in_hierarchy() const noexcept;
    [[nodiscard]] bool transient() const noexcept;
    [[nodiscard]] bool set_transient(bool transient) noexcept;
    [[nodiscard]] schema::ComponentDataT data() const;
    [[nodiscard]] bool build_data(schema::ComponentDataT& output) const;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);
    [[nodiscard]] bool remap_node_references(schema::ComponentDataT& data,
                                             std::span<const schema::NodeIdMappingT> mapping) const;
    [[nodiscard]] bool initialize()
    {
        return true;
    }

protected:
    Component(Component value, ComponentType expected) noexcept;
    template <class Data> [[nodiscard]] const Data* read_data() const noexcept
    {
        return static_cast<const Data*>(read_value());
    }
    template <class Data> [[nodiscard]] Data* write_data() noexcept
    {
        return static_cast<Data*>(write_value());
    }
    template <class Runtime> [[nodiscard]] Runtime* runtime_state() const noexcept
    {
        return static_cast<Runtime*>(runtime_value());
    }
    void modified() noexcept;
    [[nodiscard]] bool writable() const noexcept;
    [[nodiscard]] bool restore_flags(const schema::ComponentDataT& data) noexcept;
    [[nodiscard]] bool build_storage_data(schema::ComponentDataT& output) const;

private:
    friend class ComponentRegistry;
    template <class T, class Data, class Runtime>
    friend bool register_component_storage(ComponentRegistry&);
    Component(Node node, ComponentType type, std::uint64_t instance) noexcept;
    [[nodiscard]] const void* read_value() const noexcept;
    [[nodiscard]] void* write_value() noexcept;
    [[nodiscard]] void* runtime_value() const noexcept;
    [[nodiscard]] ComponentState* state() const noexcept;
    Node node_;
    ComponentType type_ = ComponentType::NONE;
    std::uint64_t instance_id_ = 0;
};
} // namespace engine
