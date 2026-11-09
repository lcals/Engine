#include "plugins/runtime/character_controller/character_camera.hpp"
#include "plugins/runtime/camera/camera_controller.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include <cmath>
#include <set>
namespace engine::plugins::character_controller
{
bool CharacterCamera::register_type(ComponentRegistry& r)
{
    return register_component_storage<CharacterCamera, Data>(r);
}
bool CharacterCamera::valid_settings(const Data& d) noexcept
{
    const auto finite = [](const Vec2& v) { return std::isfinite(v.x()) && std::isfinite(v.y()); };
    if (!finite(d.pointer_scale_degrees) || !finite(d.rate_degrees_per_second))
    {
        return false;
    }
    if (d.subject_slot.empty() || d.yaw_parameter.empty() || d.pitch_parameter.empty())
    {
        return d.subject_slot.empty() && d.yaw_parameter.empty() && d.pitch_parameter.empty() &&
               d.first_person_parameter.empty() && d.status_bindings.empty() &&
               d.first_person_states.empty();
    }
    std::set<std::string> names;
    names.insert(d.yaw_parameter);
    if (!names.insert(d.pitch_parameter).second)
    {
        return false;
    }
    if (!d.first_person_parameter.empty() && !names.insert(d.first_person_parameter).second)
    {
        return false;
    }
    for (const auto& b : d.status_bindings)
    {
        if (b.parameter.empty() || !names.insert(b.parameter).second ||
            b.source < schema::CharacterCameraSource::MIN ||
            b.source > schema::CharacterCameraSource::MAX || !std::isfinite(b.scale) ||
            !std::isfinite(b.offset))
        {
            return false;
        }
        bool named = b.source == schema::CharacterCameraSource::action_held ||
                     b.source == schema::CharacterCameraSource::ability_active;
        if (named == b.source_name.empty())
        {
            return false;
        }
        if (b.source >= schema::CharacterCameraSource::grounded && (b.scale != 1 || b.offset != 0))
        {
            return false;
        }
    }
    std::set<std::string> states;
    for (const auto& state : d.first_person_states)
    {
        if (state.empty() || !states.insert(state).second)
        {
            return false;
        }
    }
    return true;
}
bool CharacterCamera::get_settings(Data& out) const
{
    const auto* d = read_data<Data>();
    out = d ? *d : Data{};
    return d;
}
bool CharacterCamera::set_settings(const Data& d)
{
    if (!writable() || !valid_settings(d))
    {
        return false;
    }
    *write_data<Data>() = d;
    modified();
    return true;
}
bool CharacterCamera::restore_data(const schema::ComponentDataT& d)
{
    const auto* data = d.payload.AsCharacterCameraData();
    return d.flags && data && set_settings(*data) && restore_flags(d);
}
bool CharacterCamera::get_angles(double& yaw, double& pitch) const
{
    yaw = 0;
    pitch = 0;
    if (!active())
    {
        return false;
    }
    const auto* d = read_data<Data>();
    auto solver = node().get_component<camera::CameraController>();
    schema::CameraParameterValueT a, b;
    if (!solver.active() || !solver.get_parameter(d->yaw_parameter, a) ||
        !solver.get_parameter(d->pitch_parameter, b) ||
        a.type != schema::CameraParameterType::float_value || b.type != a.type)
    {
        return false;
    }
    yaw = a.float_value;
    pitch = b.float_value;
    return true;
}
bool CharacterCamera::set_angles(double yaw, double pitch)
{
    if (!writable() || !active() || !std::isfinite(yaw) || !std::isfinite(pitch))
    {
        return false;
    }
    const auto& d = *read_data<Data>();
    auto solver = node().get_component<camera::CameraController>();
    camera::CameraController::Data config;
    if (!solver.active() || !solver.get_settings(config))
    {
        return false;
    }
    const schema::CameraParameterDataT *y = nullptr, *p = nullptr;
    for (const auto& v : config.parameters)
    {
        if (v.name == d.yaw_parameter)
        {
            y = &v;
        }
        if (v.name == d.pitch_parameter)
        {
            p = &v;
        }
    }
    if (!y || !p || y->type != schema::CameraParameterType::float_value || p->type != y->type ||
        (y->has_range && (y->minimum > -180 || y->maximum < 180)) || !p->has_range)
    {
        return false;
    }
    schema::CameraParameterValueT values[2];
    values[0].name = y->name;
    values[1].name = p->name;
    values[0].type = values[1].type = schema::CameraParameterType::float_value;
    double angle = std::fmod(yaw + 180, 360);
    if (angle < 0)
    {
        angle += 360;
    }
    values[0].float_value = static_cast<float>(angle - 180);
    values[1].float_value = static_cast<float>(std::clamp(pitch, p->minimum, p->maximum));
    return solver.set_parameters(values);
}
bool CharacterCamera::apply_look(const Vec2& delta, const Vec2& rate, double dt)
{
    double yaw, pitch;
    if (!std::isfinite(dt) || dt < 0 || !get_angles(yaw, pitch))
    {
        return false;
    }
    const auto& d = *read_data<Data>();
    return set_angles(yaw + delta.x() * d.pointer_scale_degrees.x() +
                          rate.x() * d.rate_degrees_per_second.x() * dt,
                      pitch + delta.y() * d.pointer_scale_degrees.y() +
                          rate.y() * d.rate_degrees_per_second.y() * dt);
}
bool CharacterCamera::toggle_view()
{
    if (!writable() || !active())
    {
        return false;
    }
    auto solver = node().get_component<camera::CameraController>();
    const auto& d = *read_data<Data>();
    schema::CameraParameterValueT value;
    if (!solver.active() || !solver.get_parameter(d.first_person_parameter, value) ||
        value.type != schema::CameraParameterType::bool_value)
    {
        return false;
    }
    value.bool_value = !value.bool_value;
    return solver.set_parameters({&value, 1});
}
} // namespace engine::plugins::character_controller
