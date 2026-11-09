#include "plugins/runtime/camera/modules/lens_camera_module.hpp"
#include "plugins/runtime/camera/camera_math.hpp"
namespace engine::plugins::camera
{
bool LensCameraModule::validate(const Data& d, const schema::CameraControllerDataT& def,
                                std::string& e)
{
    if (d.projection < schema::Projection::MIN || d.projection > schema::Projection::MAX ||
        !detail::valid_input(d.fov.get(), def) ||
        !detail::valid_input(d.orthographic_size.get(), def) ||
        !detail::valid_input(d.near_clip.get(), def) || !detail::valid_input(d.far_clip.get(), def))
    {
        return detail::fail(e, "lens: projection or inputs");
    }
    std::vector<schema::CameraParameterValueT> defaults;
    for (const auto& p : def.parameters)
    {
        if (!p.default_value)
        {
            return detail::fail(e, "lens: missing parameter default");
        }
        defaults.push_back(*p.default_value);
    }
    CameraEvaluationContext context;
    context.parameters = defaults;
    Runtime history;
    CameraPose pose;
    return evaluate(d, history, history, context, pose) ||
           detail::fail(e, "lens: invalid default projection values");
}
bool LensCameraModule::evaluate(const Data& d, const Runtime&, Runtime& next,
                                const CameraEvaluationContext& c, CameraPose& p)
{
    p.lens.projection = d.projection;
    if (!detail::input(d.fov.get(), c, p.lens.fov_degrees) ||
        !detail::input(d.orthographic_size.get(), c, p.lens.orthographic_size) ||
        !detail::input(d.near_clip.get(), c, p.lens.near_clip) ||
        !detail::input(d.far_clip.get(), c, p.lens.far_clip) || !detail::valid_lens(p.lens))
    {
        return false;
    }
    next.pose = p;
    return true;
}
} // namespace engine::plugins::camera
