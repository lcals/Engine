#include "editor/asset_pipeline/components/rigid_body_codec.hpp"
#include "runtime/scene/rigid_body.hpp"
#include <array>
#include <utility>
namespace engine::editor::asset_pipeline
{
schema::ComponentPayload RigidBodyCodec::type() const noexcept
{
    return schema::ComponentPayload::RigidBodyData;
}
std::string_view RigidBodyCodec::name() const noexcept
{
    return "rigid_body";
}
bool RigidBodyCodec::validate(const schema::ComponentDataT& component, std::string& error) const
{
    const auto* value = component.payload.AsRigidBodyData();
    if (value == nullptr || !engine::RigidBody::valid_settings(*value))
    {
        error = "invalid rigid_body settings";
        return false;
    }
    return true;
}
bool RigidBodyCodec::read(const toml::table& table, schema::ComponentDataT& component,
                          std::string& error) const
{
    schema::RigidBodyDataT value;
    if (table.contains("motion"))
    {
        const auto parsed = table["motion"].value<std::string_view>();
        if (parsed == "static_body")
        {
            value.motion = schema::BodyMotion::static_body;
        }
        else if (parsed == "kinematic")
        {
            value.motion = schema::BodyMotion::kinematic;
        }
        else if (parsed == "dynamic")
        {
            value.motion = schema::BodyMotion::dynamic;
        }
        else
        {
            error = "invalid rigid_body.motion";
            return false;
        }
    }
    if (table.contains("mass"))
    {
        if (!float_number(table.get("mass"), value.mass))
        {
            error = "rigid_body.mass must be a finite number";
            return false;
        }
    }
    if (table.contains("gravity_factor"))
    {
        if (!float_number(table.get("gravity_factor"), value.gravity_factor))
        {
            error = "rigid_body.gravity_factor must be a finite number";
            return false;
        }
    }
    if (table.contains("linear_damping"))
    {
        if (!float_number(table.get("linear_damping"), value.linear_damping))
        {
            error = "rigid_body.linear_damping must be a finite number";
            return false;
        }
    }
    if (table.contains("angular_damping"))
    {
        if (!float_number(table.get("angular_damping"), value.angular_damping))
        {
            error = "rigid_body.angular_damping must be a finite number";
            return false;
        }
    }
    if (table.contains("linear_velocity"))
    {
        std::array<double, 3> vector{};
        if (!numbers(table.get("linear_velocity"), vector))
        {
            error = "rigid_body.linear_velocity must be a vector";
            return false;
        }
        value.linear_velocity = schema::Vec3(vector[0], vector[1], vector[2]);
    }
    if (table.contains("angular_velocity"))
    {
        std::array<double, 3> vector{};
        if (!numbers(table.get("angular_velocity"), vector))
        {
            error = "rigid_body.angular_velocity must be a vector";
            return false;
        }
        value.angular_velocity = schema::Vec3(vector[0], vector[1], vector[2]);
    }
    component.payload.Set(std::move(value));
    return true;
}
void RigidBodyCodec::write(const schema::ComponentDataT& component, toml::table& table) const
{
    const auto* value = component.payload.AsRigidBodyData();
    table.insert("type", "rigid_body");
    table.insert("motion", schema::EnumNameBodyMotion(value->motion));
    table.insert("mass", value->mass);
    table.insert("gravity_factor", value->gravity_factor);
    table.insert("linear_damping", value->linear_damping);
    table.insert("angular_damping", value->angular_damping);
    table.insert("linear_velocity",
                 toml::array{value->linear_velocity.x(), value->linear_velocity.y(),
                             value->linear_velocity.z()});
    table.insert("angular_velocity",
                 toml::array{value->angular_velocity.x(), value->angular_velocity.y(),
                             value->angular_velocity.z()});
}
} // namespace engine::editor::asset_pipeline
