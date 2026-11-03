#include "editor/asset_pipeline/components/collider_codec.hpp"
#include "runtime/scene/collider.hpp"
#include <array>
#include <memory>
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
        else if (parsed == "compound")
        {
            value.shape = schema::ColliderShape::compound;
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
    if (table.contains("hulls"))
    {
        const auto* hulls = table["hulls"].as_array();
        if (hulls == nullptr || hulls->size() > 4096)
        {
            error = "collider.hulls must contain at most 4096 convex hulls";
            return false;
        }
        std::size_t total_vertices = 0;
        for (const auto& entry : *hulls)
        {
            const auto* hull_table = entry.as_table();
            const auto* vertices = hull_table ? (*hull_table)["vertices"].as_array() : nullptr;
            if (hull_table == nullptr || hull_table->size() != 1 || vertices == nullptr ||
                vertices->size() < 4 || vertices->size() > 1024U * 1024U - total_vertices)
            {
                error = "collider hull requires vertices; each hull needs 4 points, total <= 1M";
                return false;
            }
            total_vertices += vertices->size();
            auto hull = std::make_unique<schema::ColliderHullDataT>();
            hull->vertices.reserve(vertices->size());
            for (const auto& vertex : *vertices)
            {
                std::array<double, 3> vector{};
                if (!numbers(&vertex, vector))
                {
                    error = "collider hull vertices must be finite three-component vectors";
                    return false;
                }
                hull->vertices.emplace_back(vector[0], vector[1], vector[2]);
            }
            value.hulls.push_back(std::move(hull));
        }
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
std::span<const std::string_view> ColliderCodec::resource_fields() const noexcept
{
    static constexpr std::string_view fields[]{"mesh"};
    return fields;
}

void ColliderCodec::write(const schema::ComponentDataT& component, toml::table& table) const
{
    const auto* value = component.payload.AsColliderData();
    table.insert("type", "collider");
    table.insert("shape", schema::EnumNameColliderShape(value->shape));
    table.insert("mesh", value->mesh_guid);
    table.insert("half_extents", toml::array{value->half_extents.x(), value->half_extents.y(),
                                             value->half_extents.z()});
    table.insert("radius", value->radius);
    table.insert("half_height", value->half_height);
    table.insert("center", toml::array{value->center.x(), value->center.y(), value->center.z()});
    table.insert("friction", value->friction);
    table.insert("restitution", value->restitution);
    table.insert("is_trigger", value->is_trigger);
    toml::array hulls;
    for (const auto& hull : value->hulls)
    {
        toml::array vertices;
        for (const auto& vertex : hull->vertices)
        {
            vertices.push_back(toml::array{vertex.x(), vertex.y(), vertex.z()});
        }
        toml::table entry;
        entry.insert("vertices", std::move(vertices));
        hulls.push_back(std::move(entry));
    }
    table.insert("hulls", std::move(hulls));
}
} // namespace engine::editor::asset_pipeline
