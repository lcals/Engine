#include "plugins/editor/mcp/component_mcp_tools.hpp"
namespace engine::plugins::mcp
{
bool register_physics_tools(McpSystem& system, EditorMcpTools& editor)
{
    using Json = nlohmann::json;
    const Json number = {{"type", "number"}};
    const Json vector = {{"type", "array"}, {"items", number}, {"minItems", 3}, {"maxItems", 3}};
    const Json enabled = {{"type", "boolean"}};
    return tools::component_tool(
               system, editor, "set_character_motor",
               "Add or edit a physical capsule motor; undoable. Character origin is its feet.",
               {{"enabled", enabled},
                {"radius", number},
                {"halfHeight", number},
                {"maxSlopeDegrees", number},
                {"stepHeight", number},
                {"groundSnapDistance", number},
                {"gravityFactor", number}}) &&
           tools::component_tool(system, editor, "get_character_motor",
                                 "Read character motor collision settings.", Json::object()) &&
           tools::component_tool(
               system, editor, "set_collider",
               "Add or edit a collider; undoable. Mesh colliders require a static body or no body.",
               {{"enabled", enabled},
                {"shape", {{"type", "string"}, {"enum", {"box", "sphere", "capsule", "mesh"}}}},
                {"mesh", {{"type", "string"}}},
                {"halfExtents", vector},
                {"radius", number},
                {"halfHeight", number},
                {"center", vector},
                {"friction", number},
                {"restitution", number},
                {"isTrigger", enabled}}) &&
           tools::component_tool(system, editor, "get_collider", "Read collider settings.",
                                 Json::object()) &&
           tools::component_tool(
               system, editor, "set_rigid_body", "Add or edit rigid-body settings; undoable.",
               {{"enabled", enabled},
                {"motion", {{"type", "string"}, {"enum", {"static_body", "kinematic", "dynamic"}}}},
                {"mass", number},
                {"gravityFactor", number},
                {"linearDamping", number},
                {"angularDamping", number},
                {"linearVelocity", vector},
                {"angularVelocity", vector}}) &&
           tools::component_tool(system, editor, "get_rigid_body",
                                 "Read rigid-body settings and velocity.", Json::object()) &&
           tools::component_tool(system, editor, "add_impulse",
                                 "Queue an impulse for a dynamic rigid body; not undoable.",
                                 {{"impulse", vector}}, {"impulse"});
}
} // namespace engine::plugins::mcp
