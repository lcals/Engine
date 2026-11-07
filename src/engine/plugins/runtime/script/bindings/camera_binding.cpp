#include "plugins/runtime/script/bindings/camera_binding.hpp"
#include "plugins/runtime/script/bindings/binding_helpers.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/node.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class Camera extends Component {
    constructor(token, scene, node) { super(token, scene, node, native.CameraKind); }
    get frustumCullingEnabled() {
        const h = handles.get(this); return native.CameraGetFrustumCulling(h.scene, h.node);
    }
    SetFrustumCulling(enabled) {
        const h = handles.get(this); return native.CameraSetFrustumCulling(h.scene, h.node, enabled);
    }
    SetOrthographic(size, near, far) {
        const h = handles.get(this); return native.CameraOrthographic(h.scene, h.node, size, near, far);
    }
    SetPerspective(fov, near, far) {
        const h = handles.get(this); return native.CameraSettings(h.scene, h.node, fov, near, far);
    }
    ScreenPointToRay(x, y, width, height) {
        const h = handles.get(this);
        return native.CameraScreenPointToRay(h.scene, h.node, x, y, width, height);
    }
    ViewportPointToRay(u, v, aspect) {
        const h = handles.get(this);
        return native.CameraViewportPointToRay(h.scene, h.node, u, v, aspect);
    }
}
componentTypes.set(Camera, { add: native.CameraAdd, kind: native.CameraKind });
Object.freeze(Camera.prototype);
Object.freeze(Camera);
)js";

void settings(const Arguments& arguments)
{
    arguments.set_return(false);
    auto node = BindingContext::node(arguments);
    auto camera = !node ? Camera{} : node.get_component<Camera>();
    CameraSettings value;
    if (!camera.get_settings(value))
    {
        return;
    }
    value.projection = schema::Projection::perspective;
    if (camera.valid() && arguments.count() == 5 &&
        BindingContext::scalar(arguments, 2, value.fov_degrees) &&
        BindingContext::scalar(arguments, 3, value.near_clip) &&
        BindingContext::scalar(arguments, 4, value.far_clip))
    {
        arguments.set_return(camera.set_settings(value));
    }
}
void orthographic(const Arguments& arguments)
{
    arguments.set_return(false);
    auto node = BindingContext::node(arguments);
    auto camera = !node ? Camera{} : node.get_component<Camera>();
    CameraSettings value;
    if (!camera.get_settings(value))
    {
        return;
    }
    value.projection = schema::Projection::orthographic;
    if (camera.valid() && arguments.count() == 5 &&
        BindingContext::scalar(arguments, 2, value.orthographic_size) &&
        BindingContext::scalar(arguments, 3, value.near_clip) &&
        BindingContext::scalar(arguments, 4, value.far_clip))
    {
        arguments.set_return(camera.set_settings(value));
    }
}

void get_frustum_culling(const Arguments& arguments)
{
    const auto node = BindingContext::node(arguments);
    const auto camera = node ? node.get_component<Camera>() : Camera{};
    CameraSettings settings;
    arguments.set_return(arguments.count() == 2 && camera.get_settings(settings) &&
                         settings.frustum_culling_enabled);
}

void set_frustum_culling(const Arguments& arguments)
{
    arguments.set_return(false);
    const auto node = BindingContext::node(arguments);
    auto camera = node ? node.get_component<Camera>() : Camera{};
    if (!camera || arguments.count() != 3 || !arguments.is_boolean(2))
    {
        return;
    }
    Camera::Data settings{};
    if (!camera.get_settings(settings))
    {
        arguments.set_return(false);
        return;
    }
    settings.frustum_culling_enabled = arguments.boolean(2);
    arguments.set_return(camera.set_settings(settings));
}

void point_to_ray(const Arguments& arguments)
{
    arguments.set_return({});
    const auto node = BindingContext::node(arguments);
    const auto camera = !node ? Camera{} : node.get_component<Camera>();
    const bool screen = arguments.data() == 0;
    double x = 0.0;
    double y = 0.0;
    double width_or_aspect = 0.0;
    double height = 0.0;
    if (!camera || arguments.count() != (screen ? 6 : 5) ||
        !BindingContext::number(arguments, 2, x) || !BindingContext::number(arguments, 3, y) ||
        !BindingContext::number(arguments, 4, width_or_aspect) ||
        (screen && !BindingContext::number(arguments, 5, height)))
    {
        return;
    }
    Vec3 origin;
    Vec3 direction;
    const bool success =
        screen ? camera.screen_point_to_ray({x, y}, {width_or_aspect, height}, origin, direction)
               : camera.viewport_point_to_ray({x, y}, width_or_aspect, origin, direction);
    if (!success)
    {
        return;
    }
    arguments.set_return(vm::NativeValue::Object{
        {"origin",
         vm::NativeValue::Object{{"x", origin.x()}, {"y", origin.y()}, {"z", origin.z()}}},
        {"direction", vm::NativeValue::Object{
                          {"x", direction.x()}, {"y", direction.y()}, {"z", direction.z()}}}});
}
} // namespace

std::string_view CameraBinding::name() const noexcept
{
    return "Camera";
}

std::string_view CameraBinding::source() const noexcept
{
    return javascript;
}

bool CameraBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.integer("CameraKind", static_cast<int>(Camera::component_type)) &&
           vm.function("CameraAdd", binding_detail::add_component<Camera>) &&
           vm.function("CameraSettings", settings) &&
           vm.function("CameraOrthographic", orthographic) &&
           vm.function("CameraGetFrustumCulling", get_frustum_culling) &&
           vm.function("CameraSetFrustumCulling", set_frustum_culling) &&
           vm.function("CameraScreenPointToRay", point_to_ray, 0) &&
           vm.function("CameraViewportPointToRay", point_to_ray, 1);
}

} // namespace engine::plugins::script::bindings
