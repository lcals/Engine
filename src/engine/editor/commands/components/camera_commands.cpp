#include "editor/commands/components/component_edit.hpp"

#include "runtime/scene/camera.hpp"

namespace engine::editor::components
{
namespace
{
constexpr std::uint64_t projection_field = std::uint64_t{1} << 0;
constexpr std::uint64_t fov_field = std::uint64_t{1} << 1;
constexpr std::uint64_t near_clip_field = std::uint64_t{1} << 2;
constexpr std::uint64_t far_clip_field = std::uint64_t{1} << 3;
constexpr std::uint64_t size_field = std::uint64_t{1} << 4;
constexpr std::uint64_t frustum_culling_field = std::uint64_t{1} << 5;

[[nodiscard]] bool change_camera(const Target& target, const CameraSettings& expected,
                                 const CameraSettings& replacement, std::uint64_t selected,
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
    if (((selected & projection_field) && current.projection != expected.projection) ||
        ((selected & fov_field) && current.fov_degrees != expected.fov_degrees) ||
        ((selected & near_clip_field) && current.near_clip != expected.near_clip) ||
        ((selected & far_clip_field) && current.far_clip != expected.far_clip) ||
        ((selected & size_field) && current.orthographic_size != expected.orthographic_size) ||
        ((selected & frustum_culling_field) &&
         current.frustum_culling_enabled != expected.frustum_culling_enabled))
    {
        error = "Camera changed outside this edit; undo or redo is stale";
        return false;
    }
    if (selected & projection_field)
    {
        current.projection = replacement.projection;
    }
    if (selected & fov_field)
    {
        current.fov_degrees = replacement.fov_degrees;
    }
    if (selected & near_clip_field)
    {
        current.near_clip = replacement.near_clip;
    }
    if (selected & far_clip_field)
    {
        current.far_clip = replacement.far_clip;
    }
    if (selected & size_field)
    {
        current.orthographic_size = replacement.orthographic_size;
    }
    if (selected & frustum_culling_field)
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
    const auto before = camera.settings();
    auto after = before;
    std::uint64_t selected = 0;
    if (arguments.contains("projection"))
    {
        if (arguments["projection"] == "perspective")
        {
            after.projection = schema::Projection::perspective;
        }
        else if (arguments["projection"] == "orthographic")
        {
            after.projection = schema::Projection::orthographic;
        }
        else
        {
            error = "Unknown camera projection";
            return false;
        }
        selected |= projection_field;
    }
    if (arguments.contains("frustumCullingEnabled"))
    {
        if (!arguments["frustumCullingEnabled"].is_boolean())
        {
            error = "frustumCullingEnabled must be boolean";
            return false;
        }
        selected |= frustum_culling_field;
        after.frustum_culling_enabled = arguments["frustumCullingEnabled"].get<bool>();
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
            selected |= fov_field;
            after.fov_degrees = number;
        }
        else if (std::string_view(name) == "nearClip")
        {
            selected |= near_clip_field;
            after.near_clip = number;
        }
        else if (std::string_view(name) == "farClip")
        {
            selected |= far_clip_field;
            after.far_clip = number;
        }
        else
        {
            selected |= size_field;
            after.orthographic_size = number;
        }
    }
    result = {{"sceneId", std::to_string(target.scene_id)}};
    output = std::make_unique<PropertyEdit<schema::CameraDataT>>(
        std::move(target), before, std::move(after), selected, change_camera);
    return true;
}

} // namespace engine::editor::components
