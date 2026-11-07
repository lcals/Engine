#include "animation_fixture.hpp"
#include "scene_snapshot.hpp"

#include "runtime/scene/bone_attachment.hpp"

namespace
{
using namespace test_support::animation;
bool add_skeleton(Fixture& fixture, std::string_view guid, engine::schema::SkeletonDataT data)
{
    std::string error;
    if (!engine::editor::asset_pipeline::prepare_skeleton_sampling(data, error))
    {
        return false;
    }
    flatbuffers::FlatBufferBuilder builder;
    engine::schema::FinishSkeletonDataBuffer(builder,
                                             engine::schema::SkeletonData::Pack(builder, &data));
    return add(fixture.files, guid, data.path, engine::schema::AssetKind::skeleton, builder);
}

bool rigging()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto target = skeleton(false);
    auto socket = std::make_unique<engine::schema::SkeletonSocketDataT>();
    socket->name = "hand";
    socket->joint_path = "root/right";
    socket->offset = engine::schema::JointLocalPose(
        engine::Vec3(0.5, 0, 0), engine::Vec4(0, 0, 0, 1), engine::Vec3(1, 1, 1));
    target.sockets.push_back(std::move(socket));
    auto aim = std::make_unique<engine::schema::SkeletonAimIkDataT>();
    aim->name = "look";
    aim->joint_path = "root/right";
    aim->order = 1;
    target.aim_ik.push_back(std::move(aim));
    auto chain = std::make_unique<engine::schema::SkeletonTwoBoneIkDataT>();
    chain->name = "arm";
    chain->start_path = "root";
    chain->middle_path = "root/left";
    chain->end_path = "root/left/tip";
    target.two_bone_ik.push_back(std::move(chain));
    std::erase_if(fixture.files.catalog.entries,
                  [](const auto& entry) { return entry->guid == target_guid; });
    if (!add_skeleton(fixture, target_guid, target) ||
        !fixture.services.publish_and_load(fixture.files.catalog,
                                           {{"content", fixture.files.root}}))
    {
        return false;
    }
    auto node = fixture.create(false);
    auto animator = node.get_component<engine::Animator>();
    auto transform = node.get_component<engine::Transform>();
    engine::AnimationPlayRequest request;
    request.animation_path = clip_path;
    auto authored = test_support::read_settings(transform);
    authored.position = {10, 0, 0};
    if (!transform.set_settings(authored) || !animator.play_animation(request) ||
        !fixture.animation.tick(.25))
    {
        return false;
    }
    engine::AnimationPoseTransform bone, hand;
    if (!expect(animator.get_bone_pose("root/right", bone) &&
                    animator.get_socket_pose("hand", hand) && close(bone.model[12], 4) &&
                    close(bone.world[12], 12) && close(hand.world[12], 12.5),
                "bone and socket use committed models and inverse mesh binding, not palette"))
    {
        return false;
    }
    auto prop = fixture.world.scene()->create_node("Prop");
    auto prop_transform = prop.add_component<engine::Transform>();
    if (!prop_transform)
    {
        prop_transform = prop.get_component<engine::Transform>();
    }
    auto attachment = prop.add_component<engine::BoneAttachment>();
    auto data = test_support::read_settings(attachment);
    data.socket_name = "hand";
    if (!expect(attachment.set_source(animator) && !attachment.following(),
                "binding a source does not implicitly enable following"))
    {
        return false;
    }
    data = test_support::read_settings(attachment);
    data.socket_name = "hand";
    data.follow_enabled = true;
    if (!attachment.set_settings(data))
    {
        return false;
    }
    const auto before = test_support::read_data(prop_transform);
    std::array<float, 16> matrix{};
    if (!expect(animator.pause() && fixture.animation.tick(0) && attachment.following() &&
                    prop_transform.world_matrix(matrix.data()) && close(matrix[12], 12.5) &&
                    test_support::read_data(prop_transform) == before,
                "attachment updates the shared world transform without mutating author TRS"))
    {
        return false;
    }
    if (!expect(attachment.detach() && !attachment.following() &&
                    prop_transform.world_matrix(matrix.data()) && close(matrix[12], 12.5),
                "detach keeps the current pose in author transform"))
    {
        return false;
    }
    engine::AnimationIkTarget target_point;
    target_point.space = engine::AnimationIkSpace::model;
    target_point.point = {9, 0, 0};
    target_point.enabled = true;
    target_point.weight = 1;
    if (!animator.set_aim_ik_target("look", target_point) || !fixture.animation.tick(0) ||
        !animator.get_bone_pose("root/right", bone) ||
        !expect(close(bone.model[8], 0), "IK defaults off despite an enabled target"))
    {
        return false;
    }
    auto settings = test_support::read_settings(animator);
    settings.ik_enabled = true;
    if (!expect(animator.set_settings(settings) && animator.seek(.25) &&
                    fixture.animation.tick(0) && animator.get_bone_pose("root/right", bone) &&
                    close(bone.model[8], 1),
                "Ozz Aim IK rotates final joint forward toward the model-space target"))
    {
        return false;
    }
    target_point.enabled = false;
    if (!animator.set_aim_ik_target("look", target_point) || !animator.seek(0) ||
        !fixture.animation.tick(0))
    {
        return false;
    }
    target_point.point = {1.5, 2, 0};
    target_point.pole = {0, 0, 1};
    target_point.enabled = true;
    if (!expect(animator.set_two_bone_ik_target("arm", target_point) && animator.seek(0) &&
                    fixture.animation.tick(0) && animator.get_bone_pose("root/left/tip", bone) &&
                    close(bone.model[12], 1.5) && close(bone.model[13], 2),
                "Ozz two-bone IK reaches an accessible target"))
    {
        return false;
    }
    return true;
}

bool events()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto node = fixture.create(false);
    auto animator = node.get_component<engine::Animator>();
    if (!animator.set_animation(clip_path))
    {
        return false;
    }
    std::vector<engine::schema::AnimationEventMarkerDataT> markers(3);
    for (std::size_t i = 0; i < markers.size(); ++i)
    {
        markers[i].marker_id = static_cast<std::uint32_t>(i + 1);
        markers[i].name = "marker";
        markers[i].time = static_cast<float>(i) * .5F;
    }
    std::vector<engine::AnimationEvent> received;
    fixture.animation.set_event_sink(
        [&](std::span<const engine::AnimationEvent> batch)
        {
            received.insert(received.end(), batch.begin(), batch.end());
            return true;
        });
    engine::AnimationPlayRequest request;
    request.animation_path = clip_path;
    if (!animator.set_event_markers(markers) || !animator.play_animation(request) ||
        !fixture.animation.tick(0))
    {
        return false;
    }
    engine::AnimatorPlaybackInfo info;
    if (!expect(received.size() == 1 && received[0].marker.marker_id == 1 &&
                    animator.get_playback_info(info) &&
                    received[0].playback_id == info.playback_id &&
                    received[0].animation_guid == info.animation_guid &&
                    received[0].resource_generation == info.resource_generation &&
                    received[0].track_revision == info.track_revision,
                "zero marker fires once on actual start with matching committed playback identity"))
    {
        return false;
    }
    if (!fixture.animation.tick(2.25) ||
        !expect(received.size() == 7 && received[3].marker.marker_id == 1 &&
                    received[3].cycle == 1 && received[6].cycle == 2,
                "multiple loops preserve end and next-start ordering without dropping markers"))
    {
        return false;
    }
    const auto count = received.size();
    if (!animator.pause() || !fixture.animation.tick(1) || received.size() != count ||
        !animator.seek(.75) || !fixture.animation.tick(0) || received.size() != count)
    {
        return false;
    }
    if (!animator.play() || !animator.get_playback_info(info))
    {
        return false;
    }
    const auto saved_time = info.time;
    const auto saved_normalized = info.normalized_time;
    const auto received_before_failure = received.size();
    if (!expect(!fixture.animation.tick(2000) && animator.get_playback_info(info) &&
                    close(info.time, saved_time) && close(info.normalized_time, saved_normalized) &&
                    received.size() == received_before_failure,
                "event budget failure leaves committed clock and emitted events unchanged"))
    {
        return false;
    }
    request.loop = false;
    if (!animator.play_animation(request) || !fixture.animation.tick(2) ||
        !animator.get_playback_info(info) ||
        !expect(info.completed && info.playback == engine::AnimationPlayback::paused,
                "natural completion is explicit and independent from manual pause"))
    {
        return false;
    }
    const auto completed_id = info.playback_id;
    request.animation_path = "missing.animation";
    return expect(!animator.play_animation(request) && animator.get_playback_info(info) &&
                      info.completed && info.playback_id == completed_id,
                  "failed atomic play preserves completion, clock and playback identity");
}
bool attachment_identity()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto skeleton_data = skeleton(false);
    auto socket = std::make_unique<engine::schema::SkeletonSocketDataT>();
    socket->name = "hand";
    socket->joint_path = "root/right";
    skeleton_data.sockets.push_back(std::move(socket));
    std::erase_if(fixture.files.catalog.entries,
                  [](const auto& entry) { return entry->guid == target_guid; });
    if (!add_skeleton(fixture, target_guid, skeleton_data) ||
        !fixture.services.publish_and_load(fixture.files.catalog,
                                           {{"content", fixture.files.root}}))
    {
        return false;
    }
    auto actor = fixture.create(false);
    auto animator = actor.get_component<engine::Animator>();
    engine::AnimationPlayRequest request;
    request.animation_path = clip_path;
    auto prop = fixture.world.scene()->create_node("Attachment");
    auto attachment = prop.add_component<engine::BoneAttachment>();
    if (!attachment.set_source(animator))
    {
        return false;
    }
    auto settings = test_support::read_settings(attachment);
    settings.socket_name = "hand";
    settings.follow_enabled = true;
    if (!attachment.set_settings(settings) || !animator.play_animation(request) ||
        !fixture.animation.tick(.25))
    {
        return false;
    }
    auto child = fixture.world.scene()->create_node("Child");
    auto child_transform = child.get_component<engine::Transform>();
    auto child_settings = test_support::read_settings(child_transform);
    child_settings.position = {0, 2, 0};
    if (!child.set_parent(prop) || !child_transform.set_settings(child_settings))
    {
        return false;
    }
    std::array<float, 16> matrix;
    if (!expect(child_transform.world_matrix(matrix.data()) && close(matrix[12], 2) &&
                    close(matrix[13], 2),
                "attachment descendants consume the same driven WorldTransform"))
    {
        return false;
    }
    const auto saved_animator = test_support::read_settings(animator);
    auto source_parent = fixture.world.scene()->create_node("Source parent");
    auto source_transform = actor.get_component<engine::Transform>();
    if (!source_parent || !actor.set_parent(source_parent) || !source_parent.set_enabled(false) ||
        !expect(child_transform.world_matrix(matrix.data()) && close(matrix[12], 0),
                "producer validation immediately rejects a disabled source ancestor") ||
        !source_parent.set_enabled(true) ||
        !expect(child_transform.world_matrix(matrix.data()) && close(matrix[12], 2),
                "re-enabling the same source ancestor restores its drive") ||
        !source_transform.set_enabled(false) ||
        !expect(child_transform.world_matrix(matrix.data()) && close(matrix[12], 0),
                "producer validation immediately rejects a disabled source Transform") ||
        !source_transform.set_enabled(true) ||
        !expect(child_transform.world_matrix(matrix.data()) && close(matrix[12], 2),
                "the source Transform can resume without changing animation identity"))
    {
        return false;
    }
    if (!actor.remove_component(engine::Animator::component_type))
    {
        return false;
    }
    animator = actor.add_component<engine::Animator>();
    if (!animator.set_settings(saved_animator) || !animator.play_animation(request) ||
        !fixture.animation.tick(.25))
    {
        return false;
    }
    if (!expect(
            !attachment.following() && child_transform.world_matrix(matrix.data()) &&
                close(matrix[12], 0),
            "removing and recreating a source Animator cannot revive an old attachment identity"))
    {
        return false;
    }
    if (!attachment.set_source(animator) || !fixture.animation.tick(0) || !attachment.following())
    {
        return false;
    }
    if (!attachment.set_enabled(false) || !child_transform.world_matrix(matrix.data()) ||
        !close(matrix[12], 0))
    {
        return false;
    }
    if (!attachment.set_enabled(true) || !fixture.animation.tick(0) || !attachment.following())
    {
        return false;
    }
    engine::AnimationPoseTransform pose;
    return expect(
        animator.stop() && !animator.get_bone_pose("root/right", pose) && !attachment.following() &&
            child_transform.world_matrix(matrix.data()) && close(matrix[12], 0) &&
            fixture.animation.tick(0) && animator.get_bone_pose("root/right", pose) &&
            close(pose.model[12], 3) && attachment.following(),
        "stop invalidates old bone/drive immediately and next safe point publishes rest pose");
}
bool same_frame_attachment_ik()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    for (bool source : {false, true})
    {
        auto data = skeleton(source);
        data.joints.push_back(joint("palm", 3, {0, 1, 0}, translation(0, -4, 0)));
        if (!source)
        {
            auto socket = std::make_unique<engine::schema::SkeletonSocketDataT>();
            socket->name = "weapon";
            socket->joint_path = "root/right";
            socket->offset = engine::schema::JointLocalPose({0, 0, -1}, {0, 0, 0, 1}, {1, 1, 1});
            data.sockets.push_back(std::move(socket));
            auto aim = std::make_unique<engine::schema::SkeletonAimIkDataT>();
            aim->name = "dominant";
            aim->joint_path = "root/right";
            aim->order = 0;
            data.aim_ik.push_back(std::move(aim));
            auto arm = std::make_unique<engine::schema::SkeletonTwoBoneIkDataT>();
            arm->name = "offhand";
            arm->start_path = "root/left";
            arm->middle_path = "root/left/tip";
            arm->end_path = "root/left/tip/palm";
            arm->order = 1;
            data.two_bone_ik.push_back(std::move(arm));
        }
        const auto* guid = source ? source_guid : target_guid;
        std::erase_if(fixture.files.catalog.entries,
                      [&](const auto& entry) { return entry->guid == guid; });
        if (!add_skeleton(fixture, guid, data))
        {
            return false;
        }
    }
    // 源骨架增加关节后，固定动画必须重新离线编译对应的采样轨道和通道掩码。
    auto source_rig = skeleton(true);
    source_rig.joints.push_back(joint("palm", 3, {0, 1, 0}, translation(0, -4, 0)));
    for (int index = 0; index < 3; ++index)
    {
        const auto* path = index == 0 ? clip_path : index == 1 ? scale_path : reflected_path;
        const auto* guid = index == 0 ? clip_guid : index == 1 ? scale_guid : reflected_guid;
        auto data = clip(path, index != 0, index == 2);
        std::string error;
        if (!engine::editor::asset_pipeline::prepare_animation_sampling(data, source_rig, error))
        {
            return false;
        }
        flatbuffers::FlatBufferBuilder builder;
        engine::schema::FinishAnimationDataBuffer(
            builder, engine::schema::AnimationData::Pack(builder, &data));
        std::erase_if(fixture.files.catalog.entries,
                      [&](const auto& entry) { return entry->guid == guid; });
        if (!add(fixture.files, guid, path, engine::schema::AssetKind::animation, builder,
                 {source_guid}))
        {
            return false;
        }
    }
    if (!fixture.services.publish_and_load(fixture.files.catalog,
                                           {{"content", fixture.files.root}}))
    {
        return false;
    }
    auto actor = fixture.create(false);
    auto animator = actor.get_component<engine::Animator>();
    auto settings = test_support::read_settings(animator);
    settings.ik_enabled = true;
    if (!animator.set_settings(settings))
    {
        return false;
    }
    auto weapon = fixture.world.scene()->create_node("Weapon");
    auto attachment = weapon.add_component<engine::BoneAttachment>();
    if (!attachment.set_source(animator))
    {
        return false;
    }
    auto follow = test_support::read_settings(attachment);
    follow.socket_name = "weapon";
    follow.follow_enabled = true;
    if (!attachment.set_settings(follow))
    {
        return false;
    }
    auto grip = fixture.world.scene()->create_node("Grip");
    if (!grip.set_parent(weapon))
    {
        return false;
    }
    engine::AnimationIkTarget dominant;
    dominant.space = engine::AnimationIkSpace::model;
    dominant.point = {8, 0, 0};
    dominant.enabled = true;
    dominant.weight = 1;
    engine::AnimationIkTarget offhand;
    offhand.kind = engine::AnimationIkTargetKind::node;
    offhand.space = engine::AnimationIkSpace::model;
    offhand.target_node = grip;
    offhand.pole = {0, 0, 1};
    offhand.enabled = true;
    offhand.weight = 1;
    engine::AnimationPlayRequest request;
    request.animation_path = clip_path;
    if (!expect(animator.play_animation(request), "start same-frame actor") ||
        !expect(animator.set_aim_ik_target("dominant", dominant),
                "set independent dominant target") ||
        !expect(animator.set_two_bone_ik_target("offhand", offhand),
                "set socket descendant offhand target") ||
        !expect(fixture.animation.tick(0), "sample same-frame ordered attachment target"))
    {
        return false;
    }
    engine::AnimationPoseTransform end, socket;
    if (!expect(animator.get_bone_pose("root/left/tip/palm", end) &&
                    animator.get_socket_pose("weapon", socket) &&
                    close(end.model[12], socket.model[12]) &&
                    close(end.model[13], socket.model[13]) &&
                    close(end.model[14], socket.model[14]),
                "offhand IK resolves this frame's dominant-hand socket instead of stale node world "
                "transform"))
    {
        return false;
    }
    return expect(!animator.set_aim_ik_target("dominant", offhand),
                  "IK rejects feedback to its own animated attachment subtree");
}

} // namespace
int main()
{
    return expect(rigging(), "rigging integration") && expect(events(), "event integration") &&
                   expect(attachment_identity(), "attachment identity integration") &&
                   expect(same_frame_attachment_ik(), "same-frame IK integration")
               ? 0
               : 1;
}
