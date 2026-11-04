#include "editor/commands/components/component_edit.hpp"

#include "runtime/scene/transform.hpp"

namespace engine::editor::components
{
namespace
{
class TransformEdit final : public EditorEdit
{
public:
    Target target;
    TransformSettings before;
    TransformSettings after;
    bool position = false;
    bool rotation = false;
    bool scale = false;

    explicit TransformEdit(Target value) : target(value)
    {
    }

    bool apply(std::string& error) override
    {
        return change(before, after, error);
    }

    bool revert(std::string& error) override
    {
        return change(after, before, error);
    }

private:
    bool change(const TransformSettings& expected, const TransformSettings& replacement,
                std::string& error)
    {
        auto node = target.node(error);
        auto component = node.valid() ? node.get_component<Transform>() : Transform{};
        if (!component)
        {
            error = "Transform target no longer exists";
            return false;
        }
        auto current = component.settings();
        if ((position && !same(current.position, expected.position)) ||
            (rotation && !same(current.rotation, expected.rotation)) ||
            (scale && !same(current.scale, expected.scale)))
        {
            error = "Transform changed outside this edit; undo or redo is stale";
            return false;
        }
        if (position)
        {
            current.position = replacement.position;
        }
        if (rotation)
        {
            current.rotation = replacement.rotation;
        }
        if (scale)
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
};

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
    auto edit = std::make_unique<TransformEdit>(target);
    edit->before = component.settings();
    edit->after = edit->before;
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
            edit->position = true;
            edit->after.position = value;
        }
        else if (std::string_view(name) == "rotation")
        {
            edit->rotation = true;
            edit->after.rotation = value;
        }
        else
        {
            edit->scale = true;
            edit->after.scale = value;
        }
    }
    result = {{"sceneId", std::to_string(target.scene_id)}};
    output = std::move(edit);
    return true;
}

} // namespace engine::editor::components
