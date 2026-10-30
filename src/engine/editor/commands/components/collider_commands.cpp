#include "editor/commands/components/component_edit.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/collider.hpp"
namespace engine::editor::components
{
namespace
{
bool read(const Json& arguments, schema::ColliderDataT& value, std::string& error)
{
    if (!fields(arguments,
                {"enabled", "shape", "mesh", "halfExtents", "radius", "halfHeight", "center",
                 "friction", "restitution", "isTrigger"},
                error))
    {
        return false;
    }
    if (arguments.contains("shape"))
    {
        if (arguments["shape"] == "box")
        {
            value.shape = schema::ColliderShape::box;
        }
        else if (arguments["shape"] == "sphere")
        {
            value.shape = schema::ColliderShape::sphere;
        }
        else if (arguments["shape"] == "capsule")
        {
            value.shape = schema::ColliderShape::capsule;
        }
        else if (arguments["shape"] == "mesh")
        {
            value.shape = schema::ColliderShape::mesh;
        }
        else
        {
            error = "Invalid shape";
            return false;
        }
    }
    if (arguments.contains("mesh"))
    {
        if (!arguments["mesh"].is_string() ||
            (arguments.contains("shape") && value.shape != schema::ColliderShape::mesh))
        {
            error = "mesh requires a resource path and shape mesh";
            return false;
        }
        if (!ResourceSystem::path_to_guid(arguments["mesh"].get<std::string>(), value.mesh_guid))
        {
            error = "Mesh resource path not found";
            return false;
        }
        value.shape = schema::ColliderShape::mesh;
    }
    if (value.shape != schema::ColliderShape::mesh)
    {
        value.mesh_guid.clear();
    }
    if (arguments.contains("halfExtents") && !vector(arguments["halfExtents"], value.half_extents))
    {
        error = "Invalid halfExtents";
        return false;
    }
    if (arguments.contains("radius") && !number(arguments["radius"], value.radius))
    {
        error = "Invalid radius";
        return false;
    }
    if (arguments.contains("halfHeight") && !number(arguments["halfHeight"], value.half_height))
    {
        error = "Invalid halfHeight";
        return false;
    }
    if (arguments.contains("center") && !vector(arguments["center"], value.center))
    {
        error = "Invalid center";
        return false;
    }
    if (arguments.contains("friction") && !number(arguments["friction"], value.friction))
    {
        error = "Invalid friction";
        return false;
    }
    if (arguments.contains("restitution") && !number(arguments["restitution"], value.restitution))
    {
        error = "Invalid restitution";
        return false;
    }
    if (arguments.contains("isTrigger"))
    {
        if (!arguments["isTrigger"].is_boolean())
        {
            error = "isTrigger must be boolean";
            return false;
        }
        value.is_trigger = arguments["isTrigger"].get<bool>();
    }
    return true;
}
Json write(const schema::ColliderDataT& value)
{
    Json result = Json::object();
    result["shape"] = schema::EnumNameColliderShape(value.shape);
    result["meshGuid"] = value.mesh_guid;
    std::string path;
    if (!value.mesh_guid.empty())
    {
        (void)ResourceSystem::guid_to_path(value.mesh_guid, path);
    }
    result["mesh"] = std::move(path);
    result["halfExtents"] =
        Json::array({value.half_extents.x(), value.half_extents.y(), value.half_extents.z()});
    result["radius"] = value.radius;
    result["halfHeight"] = value.half_height;
    result["center"] = Json::array({value.center.x(), value.center.y(), value.center.z()});
    result["friction"] = value.friction;
    result["restitution"] = value.restitution;
    result["isTrigger"] = value.is_trigger;
    return result;
}
} // namespace
bool register_collider(WorldSystem& world, EditorCommands& commands, NodeAliases& aliases)
{
    if (!register_component<engine::Collider, schema::ColliderDataT>(world, commands, "collider",
                                                                     aliases, read, write))
    {
        return false;
    }
    return true;
}
} // namespace engine::editor::components
