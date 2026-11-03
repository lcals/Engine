#include "runtime/scene/node.hpp"

#include "runtime/scene/scene.hpp"

#include <algorithm>
#include <utility>

namespace engine
{

Node::Node(Scene& scene, std::uint64_t id, std::uint64_t local_id, std::string_view name)
    : scene_(scene), entity_id_(id)
{
    info_.id = local_id;
    info_.name = name;
}

Node::~Node() noexcept
{
    components_.clear();
    scene_.destroy_entity(entity_id_, info_.id);
}

bool Node::enabled() const noexcept
{
    return info_.enabled;
}

void Node::set_enabled(bool enabled) noexcept
{
    info_.enabled = enabled;
}

bool Node::active() const noexcept
{
    for (const Node* current = this; current != nullptr; current = current->parent())
    {
        if (!current->enabled())
        {
            return false;
        }
    }
    return true;
}

bool Node::visible() const noexcept
{
    return info_.visible;
}

void Node::set_visible(bool visible) noexcept
{
    info_.visible = visible;
}

bool Node::visible_in_hierarchy() const noexcept
{
    for (const Node* current = this; current != nullptr; current = current->parent())
    {
        if (!current->visible())
        {
            return false;
        }
    }
    return true;
}

std::uint64_t Node::id() const noexcept
{
    return entity_id_;
}

std::string_view Node::name() const noexcept
{
    return info_.name;
}

const schema::NodeInfoT& Node::info() const noexcept
{
    return info_;
}

Scene& Node::scene() noexcept
{
    return scene_;
}

const Scene& Node::scene() const noexcept
{
    return scene_;
}

Node* Node::parent() noexcept
{
    return info_.parent_id == 0 ? nullptr : scene_.find_local_node(info_.parent_id);
}

const Node* Node::parent() const noexcept
{
    return info_.parent_id == 0 ? nullptr : std::as_const(scene_).find_local_node(info_.parent_id);
}

bool Node::set_parent(Node* parent) noexcept
{
    if (parent != nullptr && (persistent_ || &parent->scene() != &scene_))
    {
        return false;
    }
    for (const Node* ancestor = parent; ancestor != nullptr; ancestor = ancestor->parent())
    {
        if (ancestor == this)
        {
            return false;
        }
    }
    info_.parent_id = parent == nullptr ? 0 : parent->info_.id;
    return true;
}

bool Node::remove_component(ComponentType type) noexcept
{
    return std::erase_if(components_,
                         [type](const auto& value) { return value->type() == type; }) != 0;
}

bool Node::transient() const noexcept
{
    for (const Node* current = this; current != nullptr; current = current->parent())
    {
        if (current->transient_)
        {
            return true;
        }
    }
    return false;
}

void Node::set_transient(bool transient) noexcept
{
    transient_ = transient;
}

bool Node::set_persistent(bool value) noexcept
{
    if (parent() != nullptr)
    {
        return false;
    }
    persistent_ = value;
    return true;
}

bool Node::persistent() const noexcept
{
    for (const Node* current = this; current != nullptr; current = current->parent())
    {
        if (current->persistent_)
        {
            return true;
        }
    }
    return false;
}

Component* Node::get_component(ComponentType type) noexcept
{
    return const_cast<Component*>(std::as_const(*this).get_component(type));
}

const Component* Node::get_component(ComponentType type) const noexcept
{
    const auto iterator = std::find_if(components_.begin(), components_.end(),
                                       [type](const auto& item) { return item->type() == type; });
    return iterator == components_.end() ? nullptr : iterator->get();
}

const std::vector<std::unique_ptr<Component>>& Node::components() const noexcept
{
    return components_;
}

} // namespace engine
