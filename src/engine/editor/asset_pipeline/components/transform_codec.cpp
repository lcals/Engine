#include "editor/asset_pipeline/components/transform_codec.hpp"

#include "runtime/core/guid_map.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace engine::asset_pipeline
{

schema::ComponentPayload TransformCodec::type() const noexcept
{
    return schema::ComponentPayload::TransformData;
}

std::string_view TransformCodec::name() const noexcept
{
    return "transform";
}

bool TransformCodec::validate(const schema::ComponentDataT& component, std::string& error) const
{

    const auto* value = component.payload.AsTransformData();
    const auto finite = [](const schema::Vec3& vector)
    { return std::isfinite(vector.x()) && std::isfinite(vector.y()) && std::isfinite(vector.z()); };
    if (value == nullptr || !finite(value->position) || !finite(value->rotation) ||
        !finite(value->scale) || value->scale.x() == 0 || value->scale.y() == 0 ||
        value->scale.z() == 0)
    {
        error = "invalid scene transform";
        return false;
    }

    return true;
}

bool TransformCodec::read(const toml::table& table, schema::ComponentDataT& component,
                          std::string& error) const
{

    std::array<double, 3> position{0, 0, 0};
    std::array<double, 3> rotation{0, 0, 0};
    std::array<double, 3> scale{1, 1, 1};
    if ((table.contains("position") && !numbers(table.get("position"), position)) ||
        (table.contains("rotation") && !numbers(table.get("rotation"), rotation)) ||
        (table.contains("scale") && !numbers(table.get("scale"), scale)))
    {
        error = "transform values must be finite three-element arrays";
        return false;
    }
    schema::TransformDataT value;
    value.position = {position[0], position[1], position[2]};
    value.rotation = {rotation[0], rotation[1], rotation[2]};
    value.scale = {scale[0], scale[1], scale[2]};
    component.payload.Set(std::move(value));

    return true;
}

void TransformCodec::write(const schema::ComponentDataT& component, toml::table& value) const
{

    const auto* transform = component.payload.AsTransformData();
    value.insert("type", "transform");
    value.insert("position", toml::array{transform->position.x(), transform->position.y(),
                                         transform->position.z()});
    value.insert("rotation", toml::array{transform->rotation.x(), transform->rotation.y(),
                                         transform->rotation.z()});
    value.insert("scale",
                 toml::array{transform->scale.x(), transform->scale.y(), transform->scale.z()});
}

} // namespace engine::asset_pipeline
