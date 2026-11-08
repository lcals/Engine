#include "runtime/resource/codecs/animation_retarget_resource_codec.hpp"
#include "runtime/resource/codecs/resource_decode.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/resource/codecs/animation_retarget_validation.hpp"
#include "runtime/resource/codecs/model_value_utils.hpp"
#include "runtime/resource/types/skeleton_resource.hpp"
#include <set>

#include <algorithm>
#include <cmath>
#include <utility>

namespace engine
{
namespace
{
bool joint_path(std::string_view value)
{
    return !value.empty() && value.size() <= 4096 && value.front() != '/' && value.back() != '/' &&
           value.find("//") == std::string_view::npos &&
           std::none_of(value.begin(), value.end(),
                        [](unsigned char ch) { return ch < 32 || ch == 127; });
}

bool reference_pose(const std::vector<schema::JointLocalPose>& poses)
{
    return poses.size() <= 1024 &&
           std::all_of(poses.begin(), poses.end(),
                       [](const auto& pose)
                       {
                           const auto& scale = pose.scale();
                           return model_value_utils::vector(pose.translation()) &&
                                  model_value_utils::quaternion(pose.rotation()) &&
                                  model_value_utils::vector(scale) && scale.x() > 1.0e-8 &&
                                  std::abs(scale.y() - scale.x()) < 1.0e-6 * scale.x() &&
                                  std::abs(scale.z() - scale.x()) < 1.0e-6 * scale.x();
                       });
}
} // namespace

bool AnimationRetargetResourceCodec::validate(const schema::AnimationRetargetProfileDataT& data)
{
    if (data.schema_version != 1 || !valid_path(data.path) ||
        !GuidMap::valid_guid(data.source_skeleton_guid) ||
        !GuidMap::valid_guid(data.target_skeleton_guid) || data.joint_mappings.empty() ||
        data.joint_mappings.size() > 1024 || !reference_pose(data.source_reference_pose) ||
        !reference_pose(data.target_reference_pose) ||
        !std::isfinite(data.hips_translation_scale) || data.hips_translation_scale <= 0 ||
        data.hips_translation_scale > 10000 ||
        (data.hips_translation_mode != schema::HipsTranslationMode::target_reference &&
         data.hips_translation_mode != schema::HipsTranslationMode::scaled_source_delta))
    {
        return false;
    }
    std::set<std::string_view> targets;
    bool hips_mapping = false;
    for (const auto& mapping : data.joint_mappings)
    {
        if (!mapping || !joint_path(mapping->source_joint_path) ||
            !joint_path(mapping->target_joint_path) ||
            !targets.insert(mapping->target_joint_path).second)
        {
            return false;
        }
        hips_mapping = hips_mapping || (mapping->source_joint_path == data.source_hips_path &&
                                        mapping->target_joint_path == data.target_hips_path);
    }
    if (data.source_hips_path.empty() && data.target_hips_path.empty())
    {
        return data.hips_translation_mode == schema::HipsTranslationMode::target_reference;
    }
    return joint_path(data.source_hips_path) && joint_path(data.target_hips_path) && hips_mapping;
}

schema::AssetKind AnimationRetargetResourceCodec::kind() const noexcept
{
    return schema::AssetKind::animation_retarget_profile;
}

bool AnimationRetargetResourceCodec::decode(const schema::AssetEntryT& entry, ByteView payload,
                                            std::unique_ptr<Resource>& output,
                                            std::string& error) const
{
    const auto validate = [](ByteView payload, std::string& error)
    {
        return resource_decode_detail::verify_flatbuffer(
            payload, schema::VerifyAnimationRetargetProfileDataBuffer,
            "Invalid AnimationRetarget FlatBuffer", error);
    };
    return resource_decode_detail::decode_flatbuffer<ResourceType>(
        entry, payload, 4U * 1024U * 1024U, validate, [](const std::uint8_t* bytes)
        { return schema::UnPackAnimationRetargetProfileData(bytes); }, output, error);
}

bool AnimationRetargetResourceCodec::validate_data(const Resource& resource,
                                                   std::string& error) const
{
    const auto* data = resource_detail::data<ResourceType>(&resource);
    if (!data || !(data->path == resource.info().path && validate(*data) &&
                   resource.info().dependencies.size() ==
                       (data->source_skeleton_guid == data->target_skeleton_guid ? 1U : 2U) &&
                   declares(resource, data->source_skeleton_guid) &&
                   declares(resource, data->target_skeleton_guid)))
    {
        error = "Invalid AnimationRetarget data or declared dependencies";
        return false;
    }
    error.clear();
    return true;
}

bool AnimationRetargetResourceCodec::validate_dependencies(const Resource& resource,
                                                           ResourceView dependencies,
                                                           std::string& error) const
{
    const auto* data = resource_detail::data<AnimationRetargetResource>(&resource);
    const auto* source = data ? dependency(dependencies, data->source_skeleton_guid) : nullptr;
    const auto* target = data ? dependency(dependencies, data->target_skeleton_guid) : nullptr;
    return source && resource_detail::data<SkeletonResource>(source) && target &&
           resource_detail::data<SkeletonResource>(target) &&
           retarget_resource_detail::binding_shape(
               *data, *resource_detail::data<SkeletonResource>(source),
               *resource_detail::data<SkeletonResource>(target));
}
} // namespace engine
