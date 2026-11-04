#include "editor/commands/components/component_edit.hpp"

#include "runtime/scene/camera.hpp"

namespace engine::editor::components
{
namespace
{
class CameraEdit final : public EditorEdit
{
public:
    explicit CameraEdit(Target value) : target(value)
    {
    }
    Target target;
    CameraSettings before;
    CameraSettings after;
    bool projection = false;
    bool fov = false;
    bool near_clip = false;
    bool far_clip = false;
    bool size = false;
    bool frustum_culling = false;
    bool apply(std::string& error) override
    {
        return change(before, after, error);
    }
    bool revert(std::string& error) override
    {
        return change(after, before, error);
    }

private:
    bool change(const CameraSettings& expected, const CameraSettings& replacement,
                std::string& error)
    {
        auto node = target.node(error);
        auto camera = node ? node.get_component<Camera>() : Camera{};
        if (!camera)
        {
            error = "Camera target no longer exists";
            return false;
        }
        auto current = camera.settings();
        if ((projection && current.projection != expected.projection) ||
            (fov && current.fov_degrees != expected.fov_degrees) ||
            (near_clip && current.near_clip != expected.near_clip) ||
            (far_clip && current.far_clip != expected.far_clip) ||
            (size && current.orthographic_size != expected.orthographic_size) ||
            (frustum_culling &&
             current.frustum_culling_enabled != expected.frustum_culling_enabled))
        {
            error = "Camera changed outside this edit; undo or redo is stale";
            return false;
        }
        if (projection)
        {
            current.projection = replacement.projection;
        }
        if (fov)
        {
            current.fov_degrees = replacement.fov_degrees;
        }
        if (near_clip)
        {
            current.near_clip = replacement.near_clip;
        }
        if (far_clip)
        {
            current.far_clip = replacement.far_clip;
        }
        if (size)
        {
            current.orthographic_size = replacement.orthographic_size;
        }
        if (frustum_culling)
        {
            current.frustum_culling_enabled = replacement.frustum_culling_enabled;
        }
        if (!camera.set_settings(current))
        {
            error = "Invalid camera settings";
            return false;
        }
        return true;
    }
};

} // namespace

bool prepare_camera(WorldSystem& world, NodeAliases& aliases, const Json& arguments, Json& result,
                    std::unique_ptr<EditorEdit>& output, std::string& error)
{
    Target target(world, &aliases);
    if (!fields(arguments, {"sceneId", "nodeId", "projection", "fovDegrees", "nearClip", "farClip",
                            "orthographicSize", "frustumCullingEnabled"}) ||
        arguments.size() <= 2 || !target.read(arguments, error))
    {
        if (error.empty())
        {
            error = "set_camera requires camera fields";
        }
        return false;
    }
    const auto camera = target.node(error).get_component<Camera>();
    if (!camera)
    {
        error = "Node has no Camera";
        return false;
    }
    auto edit = std::make_unique<CameraEdit>(target);
    edit->before = camera.settings();
    edit->after = edit->before;
    if (arguments.contains("projection"))
    {
        if (arguments["projection"] == "perspective")
        {
            edit->after.projection = schema::Projection::perspective;
        }
        else if (arguments["projection"] == "orthographic")
        {
            edit->after.projection = schema::Projection::orthographic;
        }
        else
        {
            error = "Unknown camera projection";
            return false;
        }
        edit->projection = true;
    }
    if (arguments.contains("frustumCullingEnabled"))
    {
        if (!arguments["frustumCullingEnabled"].is_boolean())
        {
            error = "frustumCullingEnabled must be boolean";
            return false;
        }
        edit->frustum_culling = true;
        edit->after.frustum_culling_enabled = arguments["frustumCullingEnabled"].get<bool>();
    }
    for (const char* name : {"fovDegrees", "nearClip", "farClip", "orthographicSize"})
    {
        if (!arguments.contains(name))
        {
            continue;
        }
        double value = 0;
        if (!scalar(arguments[name], value) || std::abs(value) > std::numeric_limits<float>::max())
        {
            error = "Camera fields require finite floats";
            return false;
        }
        const auto number = static_cast<float>(value);
        if (std::string_view(name) == "fovDegrees")
        {
            edit->fov = true;
            edit->after.fov_degrees = number;
        }
        else if (std::string_view(name) == "nearClip")
        {
            edit->near_clip = true;
            edit->after.near_clip = number;
        }
        else if (std::string_view(name) == "farClip")
        {
            edit->far_clip = true;
            edit->after.far_clip = number;
        }
        else
        {
            edit->size = true;
            edit->after.orthographic_size = number;
        }
    }
    result = {{"sceneId", std::to_string(target.scene_id)}};
    output = std::move(edit);
    return true;
}

} // namespace engine::editor::components
