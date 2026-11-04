#include "plugins/editor/asset_import/fbx_document.hpp"

#include <ozz/animation/offline/motion_extractor.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/raw_track.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/runtime/skeleton.h>

#include <algorithm>
#include <cmath>

namespace engine::plugins::asset_import
{
bool apply_animation_motion(const FbxDocument& source, const FbxDocument& skeleton,
                            const ufbx_anim_stack& stack, schema::AnimationDataT& animation,
                            std::string& error)
{
    const AnimationMotionSettings* settings = nullptr;
    for (const auto& [key, motion] : source.animation_motion)
    {
        const auto found = source.animations.find(key);
        if (found != source.animations.end() && found->second == &stack)
        {
            settings = &motion;
            break;
        }
    }
    if (settings == nullptr)
    {
        return true;
    }
    const auto node = skeleton.nodes.find(settings->joint);
    if (node == skeleton.nodes.end() || !skeleton.joint_indices.contains(node->second))
    {
        error = "FBX animation_motion references a missing target joint: " + settings->joint;
        return false;
    }
    const auto joint = skeleton.joint_indices.at(node->second);
    const auto found = std::find_if(animation.tracks.begin(), animation.tracks.end(),
                                  [joint](const auto& track)
                                  { return track != nullptr && track->joint == joint; });
    if (found == animation.tracks.end() || (*found)->translations.empty() ||
        (*found)->rotations.empty())
    {
        error = "FBX animation_motion requires a sampled target track: " + settings->joint;
        return false;
    }
    const auto has_axis = [](const auto& axes)
    { return std::any_of(axes.begin(), axes.end(), [](bool selected) { return selected; }); };
    const bool rotation_selected = has_axis(settings->rotation);
    if (!has_axis(settings->position) && !rotation_selected)
    {
        return true;
    }
    auto& track = **found;
    // MotionExtractor 的 kAnimation 参考只读选中关节的局部轨道；单关节临时数据不改变
    // 运行资源的骨架索引，也避免往返转换未选骨骼而丢失 double 精度。
    ozz::animation::offline::RawSkeleton raw_skeleton;
    raw_skeleton.roots.resize(1);
    raw_skeleton.roots[0].name = settings->joint.c_str();
    raw_skeleton.roots[0].transform = ozz::math::Transform::identity();
    const auto local_skeleton = ozz::animation::offline::SkeletonBuilder{}(raw_skeleton);
    if (!local_skeleton)
    {
        error = "Cannot prepare FBX animation_motion extraction skeleton";
        return false;
    }
    ozz::animation::offline::RawAnimation input;
    input.duration = animation.duration;
    input.tracks.resize(1);
    auto& input_track = input.tracks.front();
    input_track.translations.reserve(track.translations.size());
    input_track.rotations.reserve(track.rotations.size());
    for (const auto& key : track.translations)
    {
        const auto& value = key.value();
        const ozz::math::Float3 translation{static_cast<float>(value.x()),
                                            static_cast<float>(value.y()),
                                            static_cast<float>(value.z())};
        if (!std::isfinite(translation.x) || !std::isfinite(translation.y) ||
            !std::isfinite(translation.z))
        {
            error = "FBX animation_motion position exceeds finite float range";
            return false;
        }
        input_track.translations.push_back({key.time(), translation});
    }
    for (const auto& key : track.rotations)
    {
        const auto& value = key.value();
        ozz::math::Quaternion rotation{static_cast<float>(value.x()),
                                       static_cast<float>(value.y()),
                                       static_cast<float>(value.z()),
                                       static_cast<float>(value.w())};
        const auto length_squared = rotation.x * rotation.x + rotation.y * rotation.y +
                                    rotation.z * rotation.z + rotation.w * rotation.w;
        if (!std::isfinite(length_squared) || length_squared < 0.000001F)
        {
            error = "FBX animation_motion rotation must be a finite nonzero quaternion";
            return false;
        }
        input_track.rotations.push_back({key.time(), ozz::math::Normalize(rotation)});
    }
    ozz::animation::offline::MotionExtractor extractor;
    using Reference = ozz::animation::offline::MotionExtractor::Reference;
    extractor.position_settings = {settings->position[0], settings->position[1],
                                   settings->position[2], Reference::kAnimation, true, false};
    // ozz 0.16 的 ToEuler/FromEuler 实际分量为 YZX，而 MotionExtractor 的掩码
    // 按 settings.y/x/z 排列。交换传入的 X/Z，保持 meta 的局部 XYZ 契约。
    extractor.rotation_settings = {settings->rotation[2], settings->rotation[1],
                                   settings->rotation[0], Reference::kAnimation, true, false};
    ozz::animation::offline::RawFloat3Track motion_position;
    ozz::animation::offline::RawQuaternionTrack motion_rotation;
    ozz::animation::offline::RawAnimation output;
    if (!extractor(input, *local_skeleton, &motion_position, &motion_rotation, &output))
    {
        error = "Cannot bake FBX animation_motion with ozz MotionExtractor: " + settings->joint;
        return false;
    }
    const auto& baked = output.tracks.front();
    for (std::size_t index = 0; index < track.translations.size(); ++index)
    {
        const auto& key = track.translations[index];
        const auto& original = key.value();
        const auto& value = baked.translations[index].value;
        track.translations[index] = {
            key.time(),
            {rotation_selected || settings->position[0] ? value.x : original.x(),
             rotation_selected || settings->position[1] ? value.y : original.y(),
             rotation_selected || settings->position[2] ? value.z : original.z()}};
    }
    if (rotation_selected)
    {
        // 上游旋转提取包含对平移的反旋转；必须成对保留完整 bake 结果。
        for (std::size_t index = 0; index < track.rotations.size(); ++index)
        {
            const auto& value = baked.rotations[index].value;
            track.rotations[index] = {track.rotations[index].time(),
                                     {value.x, value.y, value.z, value.w}};
        }
    }
    return true;
}
} // namespace engine::plugins::asset_import
