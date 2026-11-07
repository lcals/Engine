#include "editor/commands/components/component_edit.hpp"

#include "runtime/scene/transform.hpp"

namespace engine::editor::components
{
namespace
{
constexpr std::uint64_t position_field = std::uint64_t{1} << 0;
constexpr std::uint64_t rotation_field = std::uint64_t{1} << 1;
constexpr std::uint64_t scale_field = std::uint64_t{1} << 2;

[[nodiscard]] bool change_transform(const Target& target, const TransformSettings& expected,
                                    const TransformSettings& replacement, std::uint64_t selected,
                                    std::string& error)
{
    auto node = target.node(error);
    auto component = node.valid() ? node.get_component<Transform>() : Transform{};
    if (!component)
    {
        error = "Transform target no longer exists";
        return false;
    }
    TransformSettings current;
    if (!component.get_settings(current))
    {
        error = "Cannot read component settings";
        return false;
    }
    if (((selected & position_field) && !same(current.position, expected.position)) ||
        ((selected & rotation_field) && !same(current.rotation, expected.rotation)) ||
        ((selected & scale_field) && !same(current.scale, expected.scale)))
    {
        error = "Transform changed outside this edit; undo or redo is stale";
        return false;
    }
    if (selected & position_field)
    {
        current.position = replacement.position;
    }
    if (selected & rotation_field)
    {
        current.rotation = replacement.rotation;
    }
    if (selected & scale_field)
    {
        current.scale = replacement.scale;
    }
    if (!component.set_settings(current))
    {
        error = "Invalid transform values";
        return false;
    }
    return true;
}

} // namespace

bool prepare_transform(WorldSystem& world, NodeAliases& aliases, const Json& arguments,
                       Json& result, std::unique_ptr<EditorEdit>& output, std::string& error)
{
    Target target(world, &aliases);
    if (!fields(arguments, {"sceneId", "nodeId", "position", "rotation", "scale"}) ||
        arguments.size() <= 2 || !target.read(arguments, error))
    {
        if (error.empty())
        {
            error = "set_transform requires sceneId, nodeId and transform fields";
        }
        return false;
    }
    const auto component = target.node(error).get_component<Transform>();
    if (!component)
    {
        error = "Node has no Transform";
        return false;
    }
    TransformSettings before;
    if (!component.get_settings(before))
    {
        error = "Cannot read component settings";
        return false;
    }
    auto after = before;
    std::uint64_t selected = 0;
    for (const char* name : {"position", "rotation", "scale"})
    {
        if (!arguments.contains(name))
        {
            continue;
        }
        std::array<double, 4> values{};
        if (!vector(arguments[name], 3, values))
        {
            error = "Transform fields require three finite numbers";
            return false;
        }
        const Vec3 value{values[0], values[1], values[2]};
        if (std::string_view(name) == "position")
        {
            selected |= position_field;
            after.position = value;
        }
        else if (std::string_view(name) == "rotation")
        {
            selected |= rotation_field;
            after.rotation = value;
        }
        else
        {
            selected |= scale_field;
            after.scale = value;
        }
    }
    result = {{"sceneId", std::to_string(target.scene_id)}};
    output = std::make_unique<PropertyEdit<schema::TransformDataT>>(
        std::move(target), before, std::move(after), selected, change_transform);
    return true;
}

} // namespace engine::editor::components
