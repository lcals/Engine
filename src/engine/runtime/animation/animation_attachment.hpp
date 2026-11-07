#pragma once

#include "runtime/resource/resource_reference.hpp"
#include "runtime/resource/types/skeleton_resource.hpp"
#include "runtime/world/world_system.hpp"

#include "runtime/scene/animator_runtime.hpp"
#include "runtime/scene/bone_attachment.hpp"
#include "runtime/scene/bone_attachment_runtime.hpp"
#include "runtime/scene/character_motor.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/rigid_body.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"

namespace engine::animation_detail
{
inline bool attachment_drive_valid(Node owner, std::uint64_t instance) noexcept
{
    const auto attachment = owner.get_component<BoneAttachment>();
    if (!attachment || attachment.instance_id() != instance)
    {
        return false;
    }
    const auto entity = flecs::world(owner.world()->native_world()).entity(owner.id());
    const auto* stored = entity.try_get<ecs_detail::ComponentStorage<BoneAttachment::Data>>();
    const auto* follow = entity.try_get<BoneAttachment::Runtime>();
    if (!stored || !follow || !stored->state.flags.enabled || !stored->data.follow_enabled ||
        !follow->source || !follow->source.enabled() ||
        engine::ecs_detail::component_runtime(follow->source)->pose_revision_ == 0)
    {
        return false;
    }
    for (auto ancestor = follow->source.node(); ancestor; ancestor = ancestor.parent())
    {
        if (!ancestor.enabled() || !ancestor.get_component<Transform>().enabled())
        {
            return false;
        }
    }
    return true;
}

inline bool attachment_can_drive(Node node)
{
    for (auto current = node; current; current = current.parent())
    {
        const auto transform = current.get_component<Transform>();
        if (!transform || !transform.active())
        {
            return false;
        }
    }
    const auto motor = node.get_component<CharacterMotor>();
    const auto body = node.get_component<RigidBody>();
    const auto collider = node.get_component<Collider>();
    const auto navigation = node.get_component(ComponentType::NavigationAgentData);
    return (!motor || !motor.active()) && (!body || !body.active()) &&
           (!collider || !collider.active()) && (!navigation || !navigation.active());
}
inline void clear_attachment_drive(BoneAttachment attachment)
{
    if (!attachment)
    {
        return;
    }
    auto node = attachment.node();
    auto entity = flecs::world(node.world()->native_world()).entity(node.id());
    auto* drive = entity.try_get_mut<ecs_detail::TransformDrive>();
    if (drive && drive->owner_type == BoneAttachment::component_type &&
        drive->owner_instance_id == attachment.instance_id())
    {
        drive->active = false;
        node.world()->invalidate_scene_state();
    }
    engine::ecs_detail::component_runtime(attachment)->following = false;
}
inline bool resolve_attachment_source(BoneAttachment attachment, const BoneAttachment::Data& data,
                                      Animator& source, std::uint32_t& socket_index,
                                      std::uint64_t& generation)
{
    source = {};
    socket_index = 0;
    generation = 0;
    const auto owner = attachment.node();
    if (!attachment || !owner.scene() || data.source_node_id == 0)
    {
        return false;
    }
    const auto source_node = owner.scene()->find_local_node(data.source_node_id);
    const auto candidate = source_node ? source_node.get_component<Animator>() : Animator{};
    if (!candidate || source_node == owner)
    {
        return false;
    }
    // 包含源节点的父链不能由该附件驱动，避免递归的世界变换依赖。
    for (auto current = source_node; current; current = current.parent())
    {
        const auto parent_attachment = current.get_component<BoneAttachment>();
        if (current == owner || (parent_attachment && parent_attachment.active() &&
                                 parent_attachment.settings().follow_enabled))
        {
            return false;
        }
    }
    source = candidate;
    if (!data.follow_enabled && data.socket_name.empty())
    {
        return true;
    }
    auto mesh = source_node.get_component<MeshRenderer>();
    const auto* mesh_data = mesh ? mesh.mesh_data() : nullptr;
    auto* resources = source_node.world()->resource_loader();
    resource_detail::ResourceReference skeleton;
    if (!mesh_data || mesh_data->skeleton_guid.empty() || !resources ||
        !skeleton.dependency(resources, mesh.mesh_resource(), mesh_data->skeleton_guid) ||
        !skeleton.data<SkeletonResource>())
    {
        source = {};
        return false;
    }
    const auto& sockets = skeleton.data<SkeletonResource>()->sockets;
    for (std::size_t index = 0; index < sockets.size(); ++index)
    {
        if (sockets[index] && sockets[index]->name == data.socket_name)
        {
            socket_index = static_cast<std::uint32_t>(index);
            generation = skeleton->generation();
            return true;
        }
    }
    source = {};
    return false;
}
} // namespace engine::animation_detail
