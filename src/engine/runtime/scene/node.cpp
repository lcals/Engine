#include "runtime/scene/node.hpp"

#include "runtime/scene/scene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <numbers>
#include <utility>

namespace engine
{
namespace
{

constexpr std::array<double, 16> identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

bool finite_vector(const Vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

std::array<double, 16> local_matrix(const Transform& transform)
{
    constexpr double radians = std::numbers::pi / 180.0;
    const double sx = std::sin(transform.rotation.x * radians);
    const double cx = std::cos(transform.rotation.x * radians);
    const double sy = std::sin(transform.rotation.y * radians);
    const double cy = std::cos(transform.rotation.y * radians);
    const double sz = std::sin(transform.rotation.z * radians);
    const double cz = std::cos(transform.rotation.z * radians);
    return {cz * cy * transform.scale.x,
            sz * cy * transform.scale.x,
            -sy * transform.scale.x,
            0,
            (cz * sy * sx - sz * cx) * transform.scale.y,
            (sz * sy * sx + cz * cx) * transform.scale.y,
            cy * sx * transform.scale.y,
            0,
            (cz * sy * cx + sz * sx) * transform.scale.z,
            (sz * sy * cx - cz * sx) * transform.scale.z,
            cy * cx * transform.scale.z,
            0,
            transform.position.x,
            transform.position.y,
            transform.position.z,
            1};
}

std::array<double, 16> multiply(const std::array<double, 16>& left,
                                const std::array<double, 16>& right)
{
    std::array<double, 16> result{};
    for (std::size_t column = 0; column < 4; ++column)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            for (std::size_t index = 0; index < 4; ++index)
            {
                result[column * 4 + row] += left[index * 4 + row] * right[column * 4 + index];
            }
        }
    }
    return result;
}

} // namespace

Node::Node(Scene& scene, std::uint64_t id, std::string_view name)
    : scene_(scene), id_(id), name_(name)
{
}

Node::~Node() noexcept
{
    components_.clear();
    scene_.destroy_entity(id_);
}

std::uint64_t Node::id() const noexcept
{
    return id_;
}

std::string_view Node::name() const noexcept
{
    return name_;
}

Scene& Node::scene() noexcept
{
    return scene_;
}

const Scene& Node::scene() const noexcept
{
    return scene_;
}

const Transform& Node::transform() const noexcept
{
    return transform_;
}

bool Node::set_transform(const Transform& transform) noexcept
{
    if (!finite_vector(transform.position) || !finite_vector(transform.rotation) ||
        !finite_vector(transform.scale) || transform.scale.x == 0.0 || transform.scale.y == 0.0 ||
        transform.scale.z == 0.0)
    {
        return false;
    }
    transform_ = transform;
    return true;
}

Node* Node::parent() noexcept
{
    return parent_;
}

const Node* Node::parent() const noexcept
{
    return parent_;
}

bool Node::set_parent(Node* parent) noexcept
{
    if (parent != nullptr && &parent->scene() != &scene_)
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
    parent_ = parent;
    return true;
}

bool Node::world_matrix(float* output) const noexcept
{
    if (output == nullptr)
    {
        return false;
    }
    std::transform(identity.begin(), identity.end(), output,
                   [](double value) { return static_cast<float>(value); });
    std::array<double, 16> world = identity;
    for (const Node* node = this; node != nullptr; node = node->parent())
    {
        world = multiply(local_matrix(node->transform()), world);
        if (!std::all_of(world.begin(), world.end(),
                         [](double value) { return std::isfinite(value); }))
        {
            return false;
        }
    }
    constexpr double max_float = std::numeric_limits<float>::max();
    if (!std::all_of(world.begin(), world.end(),
                     [](double value) { return value >= -max_float && value <= max_float; }))
    {
        return false;
    }
    std::transform(world.begin(), world.end(), output,
                   [](double value) { return static_cast<float>(value); });
    return true;
}

Camera* Node::add_camera()
{
    if (camera_ != nullptr)
    {
        return nullptr;
    }
    std::unique_ptr<Camera> component(new (std::nothrow) Camera(*this));
    if (component == nullptr)
    {
        return nullptr;
    }
    camera_ = component.get();
    components_.push_back(std::move(component));
    if (scene_.active_camera() == nullptr && !scene_.set_active_camera(camera_))
    {
        components_.pop_back();
        camera_ = nullptr;
    }
    return camera_;
}

Light* Node::add_light()
{
    if (light_ != nullptr)
    {
        return nullptr;
    }
    std::unique_ptr<Light> component(new (std::nothrow) Light(*this));
    if (component == nullptr)
    {
        return nullptr;
    }
    light_ = component.get();
    components_.push_back(std::move(component));
    return light_;
}

MeshRenderer* Node::add_mesh_renderer()
{
    if (mesh_renderer_ != nullptr)
    {
        return nullptr;
    }
    std::unique_ptr<MeshRenderer> component(new (std::nothrow) MeshRenderer(*this));
    if (component == nullptr)
    {
        return nullptr;
    }
    mesh_renderer_ = component.get();
    components_.push_back(std::move(component));
    return mesh_renderer_;
}

Camera* Node::camera() noexcept
{
    return camera_;
}

const Camera* Node::camera() const noexcept
{
    return camera_;
}

Light* Node::light() noexcept
{
    return light_;
}

const Light* Node::light() const noexcept
{
    return light_;
}

MeshRenderer* Node::mesh_renderer() noexcept
{
    return mesh_renderer_;
}

const MeshRenderer* Node::mesh_renderer() const noexcept
{
    return mesh_renderer_;
}

} // namespace engine
