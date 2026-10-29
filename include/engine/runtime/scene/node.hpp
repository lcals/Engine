#pragma once

#include "runtime/scene/component.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
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
    [[nodiscard]] Scene& scene() noexcept;
    [[nodiscard]] const Scene& scene() const noexcept;
    [[nodiscard]] const Transform& transform() const noexcept;
    [[nodiscard]] bool set_transform(const Transform& transform) noexcept;
    [[nodiscard]] Node* parent() noexcept;
    [[nodiscard]] const Node* parent() const noexcept;
    // 保留局部变换；空指针解除父子关系。跨场景或形成循环时返回 false。
    [[nodiscard]] bool set_parent(Node* parent) noexcept;
    // 输出 16 个列主序 float；无效输出或计算溢出返回 false，非空输出保留单位矩阵。
    [[nodiscard]] bool world_matrix(float* output) const noexcept;

    // 返回借用，由本节点独占；每类组件至多一个，重复添加返回空指针。
    [[nodiscard]] Camera* add_camera();
    [[nodiscard]] Light* add_light();
    [[nodiscard]] MeshRenderer* add_mesh_renderer();
    [[nodiscard]] Camera* camera() noexcept;
    [[nodiscard]] const Camera* camera() const noexcept;
    [[nodiscard]] Light* light() noexcept;
    [[nodiscard]] const Light* light() const noexcept;
    [[nodiscard]] MeshRenderer* mesh_renderer() noexcept;
    [[nodiscard]] const MeshRenderer* mesh_renderer() const noexcept;

private:
    friend class Scene;
    Node(Scene& scene, std::uint64_t id, std::string_view name);

    Scene& scene_;
    std::uint64_t id_;
    std::string name_;
    Transform transform_{};
    Node* parent_ = nullptr;
    std::vector<std::unique_ptr<Component>> components_;
    Camera* camera_ = nullptr;
    Light* light_ = nullptr;
    MeshRenderer* mesh_renderer_ = nullptr;
};

} // namespace engine
