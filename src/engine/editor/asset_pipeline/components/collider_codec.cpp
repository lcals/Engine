#include "editor/asset_pipeline/components/collider_codec.hpp"
#include "runtime/scene/collider.hpp"
#include <array>
#include <utility>
namespace engine::editor::asset_pipeline
{
schema::ComponentPayload ColliderCodec::type() const noexcept
{
    return schema::ComponentPayload::ColliderData;
}
std::string_view ColliderCodec::name() const noexcept
{
    return "collider";
}
bool ColliderCodec::validate(const schema::ComponentDataT& component, std::string& error) const
{
    const auto* value = component.payload.AsColliderData();
    if (value == nullptr || !engine::Collider::valid_settings(*value))
    {
        error = "invalid collider settings";
        return false;
    }
    return true;
}
bool ColliderCodec::read(const toml::table& table, schema::ComponentDataT& component,
                         std::string& error) const
{
    schema::ColliderDataT value;
    if (table.contains("shape"))
    {
        const auto parsed = table["shape"].value<std::string_view>();
        if (parsed == "box")
        {
            value.shape = schema::ColliderShape::box;
        }
        else if (parsed == "sphere")
        {
            value.shape = schema::ColliderShape::sphere;
        }
        else if (parsed == "capsule")
        {
            value.shape = schema::ColliderShape::capsule;
        }
        else if (parsed == "mesh")
        {
            value.shape = schema::ColliderShape::mesh;
        }
        else
        {
            error = "invalid collider.shape";
            return false;
        }
    }
    if (table.contains("mesh"))
    {
        const auto mesh = table["mesh"].value<std::string>();
        if (!mesh)
        {
            error = "collider.mesh must be a Mesh GUID";
            return false;
        }
        value.mesh_guid = *mesh;
    }
    if (table.contains("half_extents"))
    {
        std::array<double, 3> vector{};
        if (!numbers(table.get("half_extents"), vector))
        {
            error = "collider.half_extents must be a vector";
            return false;
        }
        value.half_extents = schema::Vec3(vector[0], vector[1], vector[2]);
    }
    if (table.contains("radius"))
    {
        if (!float_number(table.get("radius"), value.radius))
        {
            error = "collider.radius must be a finite number";
            return false;
        }
    }
    if (table.contains("half_height"))
    {
        if (!float_number(table.get("half_height"), value.half_height))
        {
            error = "collider.half_height must be a finite number";
            return false;
        }
    }
    if (table.contains("center"))
    {
        std::array<double, 3> vector{};
        if (!numbers(table.get("center"), vector))
        {
            error = "collider.center must be a vector";
            return false;
        }
        value.center = schema::Vec3(vector[0], vector[1], vector[2]);
    }
    if (table.contains("friction"))
    {
        if (!float_number(table.get("friction"), value.friction))
        {
            error = "collider.friction must be a finite number";
            return false;
        }
    }
    if (table.contains("restitution"))
    {
        if (!float_number(table.get("restitution"), value.restitution))
        {
            error = "collider.restitution must be a finite number";
            return false;
        }
    }
    if (table.contains("is_trigger"))
    {
        const auto parsed = table["is_trigger"].value<bool>();
        if (!parsed)
        {
            error = "collider.is_trigger has an invalid type";
            return false;
        }
        value.is_trigger = *parsed;
    }
    component.payload.Set(std::move(value));
    return true;
}
void ColliderCodec::write(const schema::ComponentDataT& component, toml::table& table) const
{
    const auto* value = component.payload.AsColliderData();
    table.insert("type", "collider");
    table.insert("shape", schema::EnumNameColliderShape(value->shape));
    if (!value->mesh_guid.empty())
    {
        table.insert("mesh", value->mesh_guid);
    }
    table.insert("half_extents", toml::array{value->half_extents.x(), value->half_extents.y(),
                                             value->half_extents.z()});
    table.insert("radius", value->radius);
    table.insert("half_height", value->half_height);
    table.insert("center", toml::array{value->center.x(), value->center.y(), value->center.z()});
    table.insert("friction", value->friction);
    table.insert("restitution", value->restitution);
    table.insert("is_trigger", value->is_trigger);
}
} // namespace engine::editor::asset_pipeline
