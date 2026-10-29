#include "runtime/scene/transform.hpp"

#include "runtime/scene/node.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace engine
{
namespace
{

constexpr std::array<double, 16> identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

bool finite_vector(const Vec3& value)
{
    return std::isfinite(value.x()) && std::isfinite(value.y()) && std::isfinite(value.z());
}

std::array<double, 16> local_matrix(const TransformSettings& transform)
{
    constexpr double radians = std::numbers::pi / 180.0;
    const double sx = std::sin(transform.rotation.x() * radians);
    const double cx = std::cos(transform.rotation.x() * radians);
    const double sy = std::sin(transform.rotation.y() * radians);
    const double cy = std::cos(transform.rotation.y() * radians);
    const double sz = std::sin(transform.rotation.z() * radians);
    const double cz = std::cos(transform.rotation.z() * radians);
    return {cz * cy * transform.scale.x(),
            sz * cy * transform.scale.x(),
            -sy * transform.scale.x(),
            0,
            (cz * sy * sx - sz * cx) * transform.scale.y(),
            (sz * sy * sx + cz * cx) * transform.scale.y(),
            cy * sx * transform.scale.y(),
            0,
            (cz * sy * cx + sz * sx) * transform.scale.z(),
            (sz * sy * cx - cz * sx) * transform.scale.z(),
            cy * cx * transform.scale.z(),
            0,
            transform.position.x(),
            transform.position.y(),
            transform.position.z(),
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

Transform::Transform(Node& node) : Component(node)
{
    mutable_data().payload.Set(TransformSettings{});
}

const TransformSettings& Transform::settings() const noexcept
{
    return *data().payload.AsTransformData();
}

bool Transform::set_settings(const TransformSettings& transform) noexcept
{
    if (!finite_vector(transform.position) || !finite_vector(transform.rotation) ||
        !finite_vector(transform.scale) || transform.scale.x() == 0.0 ||
        transform.scale.y() == 0.0 || transform.scale.z() == 0.0)
    {
        return false;
    }
    *mutable_data().payload.AsTransformData() = transform;
    return true;
}

bool Transform::world_matrix(float* output) const noexcept
{
    if (output == nullptr)
    {
        return false;
    }
    std::transform(identity.begin(), identity.end(), output,
                   [](double value) { return static_cast<float>(value); });
    std::array<double, 16> world = identity;
    for (const Node* current = &node(); current != nullptr; current = current->parent())
    {
        const Transform* transform = current->get_component<Transform>();
        if (transform == nullptr)
        {
            return false;
        }
        world = multiply(local_matrix(transform->settings()), world);
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

bool Transform::restore_data(const schema::ComponentDataT& data)
{
    const auto* settings = data.payload.AsTransformData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    set_enabled(data.enabled);
    return true;
}

} // namespace engine
