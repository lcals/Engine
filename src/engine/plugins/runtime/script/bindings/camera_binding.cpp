#include "plugins/runtime/script/bindings/camera_binding.hpp"

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
    SetOrthographic(size, near, far) {
        const h = handles.get(this); return native.CameraOrthographic(h.scene, h.node, size, near, far);
    }
    SetPerspective(fov, near, far) {
        const h = handles.get(this); return native.CameraSettings(h.scene, h.node, fov, near, far);
    }
}
componentTypes.set(Camera, { add: native.CameraAdd, kind: native.CameraKind });
Object.freeze(Camera.prototype);
Object.freeze(Camera);
)js";

void add(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    arguments.set_return(arguments.count() == 2 && node != nullptr &&
                         node->add_component<Camera>() != nullptr);
}

void settings(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* node = BindingContext::node(arguments);
    auto* camera = node == nullptr ? nullptr : node->get_component<Camera>();
    CameraSettings value;
    if (camera != nullptr && arguments.count() == 5 &&
        BindingContext::scalar(arguments, 2, value.fov_degrees) &&
        BindingContext::scalar(arguments, 3, value.near_clip) &&
        BindingContext::scalar(arguments, 4, value.far_clip))
    {
        arguments.set_return(camera->set_settings(value));
    }
}
void orthographic(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* node = BindingContext::node(arguments);
    auto* camera = node == nullptr ? nullptr : node->get_component<Camera>();
    CameraSettings value;
    value.projection = schema::Projection::orthographic;
    if (camera != nullptr && arguments.count() == 5 &&
        BindingContext::scalar(arguments, 2, value.orthographic_size) &&
        BindingContext::scalar(arguments, 3, value.near_clip) &&
        BindingContext::scalar(arguments, 4, value.far_clip))
    {
        arguments.set_return(camera->set_settings(value));
    }
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
           vm.function("CameraAdd", add) && vm.function("CameraSettings", settings) &&
           vm.function("CameraOrthographic", orthographic);
}

} // namespace engine::plugins::script::bindings
