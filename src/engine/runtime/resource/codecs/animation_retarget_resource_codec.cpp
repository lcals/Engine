#include "runtime/resource/codecs/animation_retarget_resource_codec.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/resource/codecs/animation_retarget_validation.hpp"
#include "runtime/resource/codecs/model_value_utils.hpp"

#include <flatbuffers/verifier.h>

#include <algorithm>
#include <cmath>
#include <set>
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

bool AnimationRetargetResourceCodec::decode(const ResourceCatalog& catalog, ByteView payload,
                                            Resource& output) const
{
    if (payload.empty() || payload.size() > 4U * 1024U * 1024U)
    {
        return false;
    }
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
    flatbuffers::Verifier verifier(bytes, payload.size());
    if (!schema::VerifyAnimationRetargetProfileDataBuffer(verifier))
    {
        return false;
    }
    auto data = schema::UnPackAnimationRetargetProfileData(bytes);
    const auto dependency_count =
        data->source_skeleton_guid == data->target_skeleton_guid ? 1U : 2U;
    if (data->path != output.info().path || !validate(*data) ||
        output.info().dependencies.size() != dependency_count ||
        !declares(catalog, output, data->source_skeleton_guid, schema::AssetKind::skeleton) ||
        !declares(catalog, output, data->target_skeleton_guid, schema::AssetKind::skeleton))
    {
        return false;
    }
    store(output, std::move(data));
    return true;
}

bool AnimationRetargetResourceCodec::validate_dependencies(
    const ResourceCatalog&, const Resource& resource,
    std::span<const ResourcePtr> dependencies) const
{
    const auto* data = resource.retarget_profile();
    const auto* source = data ? dependency(dependencies, data->source_skeleton_guid) : nullptr;
    const auto* target = data ? dependency(dependencies, data->target_skeleton_guid) : nullptr;
    return source && source->skeleton() && target && target->skeleton() &&
           retarget_resource_detail::binding_shape(*data, *source->skeleton(), *target->skeleton());
}
} // namespace engine
