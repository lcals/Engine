#include "runtime/scene/scene.hpp"

#include <flecs.h>

#include <algorithm>
#include <new>
#include <utility>

namespace engine
{

Scene::Scene(ecs_world_t& world, std::uint64_t id, std::string_view name)
    : world_(world), id_(id), name_(name)
{
}

Scene::~Scene() noexcept
{
    active_camera_ = nullptr;
    nodes_.clear();
}

std::uint64_t Scene::id() const noexcept
{
    return id_;
}

std::string_view Scene::name() const noexcept
{
    return name_;
}

Node* Scene::create_node(std::string_view name)
{
    const ecs_entity_t entity = ecs_new(&world_);
    if (entity == 0)
    {
        return nullptr;
    }
    std::unique_ptr<Node> node(new (std::nothrow) Node(*this, entity, name));
    if (node == nullptr)
    {
        ecs_delete(&world_, entity);
        return nullptr;
    }
    Node* result = node.get();
    nodes_.push_back(std::move(node));
    return result;
}

Node* Scene::find_node(std::uint64_t id) noexcept
{
    return const_cast<Node*>(std::as_const(*this).find_node(id));
}

const Node* Scene::find_node(std::uint64_t id) const noexcept
{
    const auto iterator = std::find_if(nodes_.begin(), nodes_.end(),
                                       [id](const auto& node) { return node->id() == id; });
    return iterator == nodes_.end() ? nullptr : iterator->get();
}

bool Scene::destroy_node(std::uint64_t id)
{
    Node* root = find_node(id);
    if (root == nullptr)
    {
        return false;
    }
    // 先收集整个子树，再销毁对象，避免遍历已经失效的父指针。
    std::vector<const Node*> removed;
    for (const auto& node : nodes_)
    {
        for (const Node* ancestor = node.get(); ancestor != nullptr; ancestor = ancestor->parent())
        {
            if (ancestor == root)
            {
                removed.push_back(node.get());
                break;
            }
        }
    }
    const auto should_remove = [&removed](const Node* node)
    { return std::find(removed.begin(), removed.end(), node) != removed.end(); };
    if (active_camera_ != nullptr && should_remove(&active_camera_->node()))
    {
        active_camera_ = nullptr;
    }
    std::erase_if(nodes_, [&should_remove](const auto& node) { return should_remove(node.get()); });
    return true;
}

const std::vector<std::unique_ptr<Node>>& Scene::nodes() const noexcept
{
    return nodes_;
}

Camera* Scene::active_camera() noexcept
{
    return active_camera_;
}

const Camera* Scene::active_camera() const noexcept
{
    return active_camera_;
}

bool Scene::set_active_camera(Camera* camera) noexcept
{
    if (camera != nullptr && &camera->node().scene() != this)
    {
        return false;
    }
    active_camera_ = camera;
    return true;
}

void Scene::destroy_entity(std::uint64_t id) noexcept
{
    if (ecs_is_alive(&world_, id))
    {
        ecs_delete(&world_, id);
    }
}

} // namespace engine
