#include "runtime/scene/bone_attachment.hpp"

#include "runtime/scene/animator_runtime.hpp"
#include "runtime/scene/bone_attachment_runtime.hpp"

#include "runtime/animation/animation_attachment.hpp"
#include "runtime/animation/animation_math.hpp"
#include "runtime/scene/ecs_storage.hpp"

namespace engine
{
bool BoneAttachment::register_type(ComponentRegistry& registry)
{
    return register_component_storage<BoneAttachment, Data, Runtime>(registry);
}
BoneAttachment::Runtime* BoneAttachment::runtime() const noexcept
{
    return runtime_state<Runtime>();
}
bool BoneAttachment::valid_settings(const Data& data) noexcept
{
    return model_value_utils::vector(data.offset.translation()) &&
           model_value_utils::scale(data.offset.scale()) &&
           model_value_utils::quaternion(data.offset.rotation()) &&
           (!data.follow_enabled || (data.source_node_id != 0 && !data.socket_name.empty()));
}
BoneAttachment::Data BoneAttachment::settings() const noexcept
{
    const auto* value = read_data<Data>();
    return value ? *value : Data{};
}
bool BoneAttachment::get_settings(Data& output) const noexcept
{
    const auto* value = read_data<Data>();
    output = value ? *value : Data{};
    return value != nullptr;
}
bool BoneAttachment::set_settings(const Data& data)
{
    if (!writable() || !valid_settings(data) ||
        (data.follow_enabled && !animation_detail::attachment_can_drive(node())))
    {
        return false;
    }
    Animator source;
    std::uint32_t index = 0;
    std::uint64_t generation = 0;
    if (data.source_node_id != 0 &&
        !animation_detail::resolve_attachment_source(*this, data, source, index, generation))
    {
        return false;
    }
    animation_detail::clear_attachment_drive(*this);
    *write_data<Data>() = data;
    *runtime() = {};
    runtime()->source = source;
    runtime()->resolved_socket_index = index;
    runtime()->skeleton_generation = generation;
    modified();
    return true;
}
bool BoneAttachment::set_source(Animator source)
{
    if (!writable() || !source || source.node().scene() != node().scene() ||
        source.node() == node())
    {
        return false;
    }
    const auto info = source.node().info();
    if (!info.properties)
    {
        return false;
    }
    auto data = settings();
    data.source_node_id = info.properties->id;
    return set_settings(data);
}
bool BoneAttachment::following() const noexcept
{
    if (!valid() || !active() || !settings().follow_enabled || !runtime()->source ||
        !runtime()->source.active() ||
        !engine::ecs_detail::component_runtime(runtime()->source)->pose_revision_ ||
        !runtime()->following)
    {
        return false;
    }
    auto entity = flecs::world(node().world()->native_world()).entity(node().id());
    const auto* drive = entity.try_get<ecs_detail::TransformDrive>();
    return drive && drive->active && drive->owner_type == component_type &&
           drive->owner_instance_id == instance_id();
}
bool BoneAttachment::detach()
{
    if (!writable() || !following() || !animation_detail::attachment_can_drive(node()))
    {
        return false;
    }
    auto transform = node().get_component<Transform>();
    std::array<float, 16> world;
    if (!transform || !transform.world_matrix(world.data()))
    {
        return false;
    }
    auto local = animation_detail::load_matrix(world);
    if (const auto parent = node().parent())
    {
        std::array<float, 16> parent_world;
        ozz::math::Float4x4 inverse;
        if (!parent.get_component<Transform>().world_matrix(parent_world.data()) ||
            !animation_detail::invert_matrix(animation_detail::load_matrix(parent_world), inverse))
        {
            return false;
        }
        local = inverse * local;
    }
    TransformSettings authored;
    if (!animation_detail::decompose_authored(local, authored) || !transform.set_settings(authored))
    {
        return false;
    }
    auto* data = write_data<Data>();
    data->follow_enabled = false;
    data->source_node_id = 0;
    animation_detail::clear_attachment_drive(*this);
    *runtime() = {};
    modified();
    return node().world()->refresh_scene_state();
}
bool BoneAttachment::restore_data(const schema::ComponentDataT& input)
{
    const auto* data = input.payload.AsBoneAttachmentData();
    if (!writable() || !input.flags || !data || !valid_settings(*data))
    {
        return false;
    }
    animation_detail::clear_attachment_drive(*this);
    *write_data<Data>() = *data;
    *runtime() = {};
    runtime()->binding_pending = data->source_node_id != 0;
    modified();
    return restore_flags(input);
}
bool BoneAttachment::remap_node_references(schema::ComponentDataT& input,
                                           std::span<const schema::NodeIdMappingT> mapping) const
{
    auto* data = input.payload.AsBoneAttachmentData();
    if (!data)
    {
        return false;
    }
    if (data->source_node_id == 0)
    {
        return true;
    }
    for (const auto& item : mapping)
    {
        if (item.source_id == data->source_node_id)
        {
            if (item.retired || item.target_id == 0)
            {
                return false;
            }
            data->source_node_id = item.target_id;
            return true;
        }
    }
    return false;
}
} // namespace engine
