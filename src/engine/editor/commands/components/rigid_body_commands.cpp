#include "editor/commands/components/component_edit.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/rigid_body.hpp"
namespace engine::editor::components
{
namespace
{
bool read(const Json& arguments, schema::RigidBodyDataT& value, std::string& error)
{
    if (!fields(arguments,
                {"enabled", "motion", "mass", "gravityFactor", "linearDamping", "angularDamping",
                 "linearVelocity", "angularVelocity"},
                error))
    {
        return false;
    }
    if (arguments.contains("motion"))
    {
        if (arguments["motion"] == "static_body")
        {
            value.motion = schema::BodyMotion::static_body;
        }
        else if (arguments["motion"] == "kinematic")
        {
            value.motion = schema::BodyMotion::kinematic;
        }
        else if (arguments["motion"] == "dynamic")
        {
            value.motion = schema::BodyMotion::dynamic;
        }
        else
        {
            error = "Invalid motion";
            return false;
        }
    }
    if (arguments.contains("mass") && !number(arguments["mass"], value.mass))
    {
        error = "Invalid mass";
        return false;
    }
    if (arguments.contains("gravityFactor") &&
        !number(arguments["gravityFactor"], value.gravity_factor))
    {
        error = "Invalid gravityFactor";
        return false;
    }
    if (arguments.contains("linearDamping") &&
        !number(arguments["linearDamping"], value.linear_damping))
    {
        error = "Invalid linearDamping";
        return false;
    }
    if (arguments.contains("angularDamping") &&
        !number(arguments["angularDamping"], value.angular_damping))
    {
        error = "Invalid angularDamping";
        return false;
    }
    if (arguments.contains("linearVelocity") &&
        !vector(arguments["linearVelocity"], value.linear_velocity))
    {
        error = "Invalid linearVelocity";
        return false;
    }
    if (arguments.contains("angularVelocity") &&
        !vector(arguments["angularVelocity"], value.angular_velocity))
    {
        error = "Invalid angularVelocity";
        return false;
    }
    return true;
}
Json write(const schema::RigidBodyDataT& value)
{
    Json result = Json::object();
    result["motion"] = schema::EnumNameBodyMotion(value.motion);
    result["mass"] = value.mass;
    result["gravityFactor"] = value.gravity_factor;
    result["linearDamping"] = value.linear_damping;
    result["angularDamping"] = value.angular_damping;
    result["linearVelocity"] = Json::array(
        {value.linear_velocity.x(), value.linear_velocity.y(), value.linear_velocity.z()});
    result["angularVelocity"] = Json::array(
        {value.angular_velocity.x(), value.angular_velocity.y(), value.angular_velocity.z()});
    return result;
}
} // namespace
bool register_rigid_body(WorldSystem& world, EditorCommands& commands, NodeAliases& aliases)
{
    if (!register_component<engine::RigidBody, schema::RigidBodyDataT>(
            world, commands, "rigid_body", aliases, read, write))
    {
        return false;
    }
    return commands.register_command(
        "add_impulse", false,
        [&world](const Json& arguments, Json& result, std::unique_ptr<EditorEdit>&,
                 std::string& error)
        {
            if (!fields(arguments, {"impulse"}, error))
            {
                return false;
            }
            auto* node = target(world, arguments, error);
            auto* body = node ? node->get_component<RigidBody>() : nullptr;
            Vec3 impulse;
            if (!body || !arguments.contains("impulse") || !vector(arguments["impulse"], impulse) ||
                !body->add_impulse(impulse))
            {
                if (error.empty())
                {
                    error = "Impulse requires an enabled dynamic rigid body and a finite vector";
                }
                return false;
            }
            result = {{"accepted", true}};
            return true;
        });
}
} // namespace engine::editor::components
