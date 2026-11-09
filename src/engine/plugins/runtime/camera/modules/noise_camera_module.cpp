#include "plugins/runtime/camera/modules/noise_camera_module.hpp"
#include "plugins/runtime/camera/camera_math.hpp"
namespace engine::plugins::camera
{
bool NoiseCameraModule::validate(const Data& d, const schema::CameraControllerDataT& def,
                                 std::string& e)
{
    return (detail::nonnegative(d.position_amplitude) &&
            detail::nonnegative(d.rotation_amplitude) && detail::bounded(d.frequency, 0, 1000) &&
            detail::bounded(d.decay, 0, 1000) && detail::valid_input(d.strength.get(), def) &&
            detail::valid_input(d.impulse.get(), def)) ||
           detail::fail(e, "noise: amplitudes, frequency, decay or inputs");
}
bool NoiseCameraModule::evaluate(const Data& d, const Runtime& current, Runtime& next,
                                 const CameraEvaluationContext& c, CameraPose& p)
{
    float strength, impulse;
    if (!detail::input(d.strength.get(), c, strength) ||
        !detail::input(d.impulse.get(), c, impulse) || strength < 0 || impulse < 0)
    {
        return false;
    }
    next.time = current.time + c.delta_time;
    next.impulse = current.impulse * std::exp(-d.decay * c.delta_time) + impulse;
    const double amplitude = strength + next.impulse;
    const auto wave = [&](int axis)
    {
        return std::sin(next.time * d.frequency * 2 * std::numbers::pi * (1 + axis * 0.173) +
                        double(d.seed % 65521) * 0.071 + axis * 2.17) *
               amplitude;
    };
    p.position = detail::add(p.position, {d.position_amplitude.x() * wave(0),
                                          d.position_amplitude.y() * wave(1),
                                          d.position_amplitude.z() * wave(2)});
    p.rotation_xyzw = detail::normalized(detail::product(
        p.rotation_xyzw,
        detail::rotation({d.rotation_amplitude.x() * wave(3), d.rotation_amplitude.y() * wave(4),
                          d.rotation_amplitude.z() * wave(5)})));
    next.pose = p;
    return true;
}
} // namespace engine::plugins::camera
