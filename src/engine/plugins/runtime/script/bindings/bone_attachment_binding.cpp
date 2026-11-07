#include "plugins/runtime/script/bindings/bone_attachment_binding.hpp"
#include "plugins/runtime/script/bindings/binding_helpers.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/animator.hpp"
#include "runtime/scene/bone_attachment.hpp"

#include <array>

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class BoneAttachment extends Component {
    constructor(token, scene, node) { super(token, scene, node, native.BoneAttachmentKind); }
    SetSource(animator) {
        if (!(animator instanceof Animator)) return false;
        const h = handles.get(this), s = handles.get(animator);
        return native.BoneAttachmentSource(h.scene, h.node, s.scene, s.node);
    }
    SetSettings(socketName, followEnabled = false, offset = {}) {
        if (!offset || typeof offset !== "object") return false;
        const h = handles.get(this), t = offset.translation ?? {x:0,y:0,z:0};
        const r = offset.rotation ?? {x:0,y:0,z:0,w:1}, s = offset.scale ?? {x:1,y:1,z:1};
        return native.BoneAttachmentSettings(h.scene,h.node,socketName,followEnabled,
            t.x,t.y,t.z,r.x,r.y,r.z,r.w,s.x,s.y,s.z);
    }
    Detach() { const h = handles.get(this); return native.BoneAttachmentDetach(h.scene,h.node); }
    get following() { const h = handles.get(this); return native.BoneAttachmentFollowing(h.scene,h.node); }
}
componentTypes.set(BoneAttachment, {add:native.BoneAttachmentAdd, kind:native.BoneAttachmentKind});
Object.freeze(BoneAttachment.prototype);
Object.freeze(BoneAttachment);
)js";
void add(const Arguments& arguments)
{
    auto node = BindingContext::node(arguments);
    arguments.set_return(arguments.count() == 2 && node && node.add_component<BoneAttachment>());
}
void set_source(const Arguments& arguments)
{
    auto value = binding_detail::component<BoneAttachment>(arguments, 4);
    std::uint64_t scene = 0, node = 0;
    auto* context = BindingContext::current(arguments);
    if (!value || !context || !BindingContext::identifier(arguments, 2, scene) ||
        !BindingContext::identifier(arguments, 3, node))
    {
        return;
    }
    const auto source = context->find_node(scene, node);
    arguments.set_return(source && value.set_source(source.get_component<Animator>()));
}
void settings(const Arguments& arguments)
{
    auto value = binding_detail::component<BoneAttachment>(arguments, 14);
    std::string socket;
    std::array<float, 10> offset{};
    if (!value || !BindingContext::text(arguments, 2, socket) || !arguments.is_boolean(3))
    {
        return;
    }
    for (int index = 0; index < 10; ++index)
    {
        if (!BindingContext::scalar(arguments, index + 4, offset[index]))
        {
            return;
        }
    }
    auto data = value.settings();
    data.socket_name = std::move(socket);
    data.follow_enabled = arguments.boolean(3);
    data.offset = schema::JointLocalPose(schema::Vec3(offset[0], offset[1], offset[2]),
                                         schema::Vec4(offset[3], offset[4], offset[5], offset[6]),
                                         schema::Vec3(offset[7], offset[8], offset[9]));
    arguments.set_return(value.set_settings(data));
}
void detach(const Arguments& arguments)
{
    auto value = binding_detail::component<BoneAttachment>(arguments, 2);
    arguments.set_return(value && value.detach());
}
void following(const Arguments& arguments)
{
    const auto value = binding_detail::component<BoneAttachment>(arguments, 2);
    arguments.set_return(value && value.following());
}
} // namespace
std::string_view BoneAttachmentBinding::name() const noexcept
{
    return "BoneAttachment";
}
std::string_view BoneAttachmentBinding::source() const noexcept
{
    return javascript;
}
bool BoneAttachmentBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.integer("BoneAttachmentKind", static_cast<int>(BoneAttachment::component_type)) &&
           vm.function("BoneAttachmentAdd", add) &&
           vm.function("BoneAttachmentSource", set_source) &&
           vm.function("BoneAttachmentSettings", settings) &&
           vm.function("BoneAttachmentDetach", detach) &&
           vm.function("BoneAttachmentFollowing", following);
}
} // namespace engine::plugins::script::bindings
