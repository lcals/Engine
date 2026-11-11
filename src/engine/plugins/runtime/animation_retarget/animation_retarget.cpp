#include "plugins/runtime/animation_retarget/animation_retarget.hpp"

#include "runtime/resource/codecs/animation_retarget_resource_codec.hpp"
#include "runtime/resource/codecs/skeleton_resource_codec.hpp"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

// Reference model-space transfer: Wicked Engine Scene::RetargetAnimation, MIT.
// Pinned source and the TRS/target-bone-length adaptation are documented in
// third_party/notices/wicked-animation-retarget.md.
namespace engine::plugins::animation_retarget
{
namespace
{
struct ModelPose
{
    bx::Vec3 translation{0, 0, 0};
    bx::Quaternion rotation{0, 0, 0, 1};
    float scale = 1;
};

bx::Vec3 vector(const schema::Vec3& value)
{
    return {static_cast<float>(value.x()), static_cast<float>(value.y()),
            static_cast<float>(value.z())};
}

bx::Quaternion quaternion(const schema::Vec4& value)
{
    return {static_cast<float>(value.x()), static_cast<float>(value.y()),
            static_cast<float>(value.z()), static_cast<float>(value.w())};
}

bool finite(const bx::Vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool local_valid(const schema::JointLocalPose& value)
{
    const auto& t = value.translation();
    const auto& r = value.rotation();
    const auto& s = value.scale();
    const double q_length = r.x() * r.x() + r.y() * r.y() + r.z() * r.z() + r.w() * r.w();
    const auto number = [](double v) { return std::isfinite(v) && std::abs(v) <= 1.0e12; };
    return number(t.x()) && number(t.y()) && number(t.z()) && number(q_length) &&
           std::abs(q_length - 1) < 0.001 && number(s.x()) && number(s.y()) && number(s.z()) &&
           s.x() > 1.0e-8 && std::abs(s.y() - s.x()) < 1.0e-6 * s.x() &&
           std::abs(s.z() - s.x()) < 1.0e-6 * s.x();
}

bool compose(const schema::JointLocalPose& local, const ModelPose& parent, ModelPose& output)
{
    output.translation =
        bx::add(parent.translation,
                bx::mul(bx::mul(vector(local.translation()), parent.scale), parent.rotation));
    output.rotation = bx::normalize(bx::mul(parent.rotation, quaternion(local.rotation())));
    output.scale = parent.scale * static_cast<float>(local.scale().x());
    return finite(output.translation) && std::isfinite(bx::dot(output.rotation, output.rotation)) &&
           std::isfinite(output.scale) && output.scale > 1.0e-12F;
}

bool prepare_skeleton(const schema::SkeletonDataT& skeleton,
                      const std::vector<schema::JointLocalPose>& override_pose,
                      std::vector<std::int32_t>& parents,
                      std::vector<schema::JointLocalPose>& reference, std::vector<ModelPose>& model,
                      std::map<std::string, std::size_t, std::less<>>& indices)
{
    if (!SkeletonResourceCodec::validate(skeleton) ||
        (!override_pose.empty() && override_pose.size() != skeleton.joints.size()))
    {
        return false;
    }
    std::size_t roots = 0;
    std::vector<std::string> paths;
    for (std::size_t index = 0; index < skeleton.joints.size(); ++index)
    {
        const auto& joint = *skeleton.joints[index];
        roots += joint.parent < 0;
        const std::string path =
            joint.parent < 0 ? joint.name : paths[joint.parent] + "/" + joint.name;
        if (!indices.emplace(path, index).second)
        {
            return false;
        }
        paths.push_back(path);
        parents.push_back(joint.parent);
        reference.push_back(
            override_pose.empty()
                ? schema::JointLocalPose(joint.translation, joint.rotation, joint.scale)
                : override_pose[index]);
        model.emplace_back();
        if (!local_valid(reference.back()) ||
            !compose(reference.back(), joint.parent < 0 ? ModelPose{} : model[joint.parent],
                     model.back()))
        {
            return false;
        }
    }
    // A single reference root defines the model-frame alignment for this profile.
    return roots == 1;
}

class PreparedRetargetBinding final : public AnimationRetargetBinding
{
public:
    bool prepare(const schema::AnimationRetargetProfileDataT& profile,
                 const schema::SkeletonDataT& source, const schema::SkeletonDataT& target,
                 std::string& error)
    {
        std::map<std::string, std::size_t, std::less<>> source_indices, target_indices;
        std::vector<schema::JointLocalPose> source_reference;
        if (!prepare_skeleton(source, profile.source_reference_pose, source_parents_,
                              source_reference, source_reference_model_, source_indices) ||
            !prepare_skeleton(target, profile.target_reference_pose, target_parents_,
                              target_reference_, target_reference_model_, target_indices))
        {
            error = "Retargeting requires valid single-root skeletons, complete reference poses "
                    "and positive uniform scales";
            return false;
        }
        frame_alignment_ =
            bx::normalize(bx::mul(target_reference_model_.front().rotation,
                                  bx::conjugate(source_reference_model_.front().rotation)));
        mapped_sources_.assign(target.joints.size(), -1);
        post_rotations_.resize(target.joints.size(), schema::Vec4{0, 0, 0, 1});
        for (const auto& mapping : profile.joint_mappings)
        {
            const auto source_index = source_indices.find(mapping->source_joint_path);
            const auto target_index = target_indices.find(mapping->target_joint_path);
            if (source_index == source_indices.end() || target_index == target_indices.end())
            {
                error = "Retarget joint mapping cannot be resolved: " + mapping->source_joint_path +
                        " -> " + mapping->target_joint_path;
                return false;
            }
            const auto src = source_index->second;
            const auto dst = target_index->second;
            mapped_sources_[dst] = static_cast<std::int32_t>(src);
            // Column-vector equivalent of the upstream reference-model transfer:
            // target_model = A * source_model * inverse(source_reference_model)
            //                * inverse(A) * target_reference_model.
            const auto rotation =
                bx::normalize(bx::mul(bx::mul(bx::conjugate(source_reference_model_[src].rotation),
                                              bx::conjugate(frame_alignment_)),
                                      target_reference_model_[dst].rotation));
            post_rotations_[dst] = schema::Vec4{rotation.x, rotation.y, rotation.z, rotation.w};
        }
        hips_mode_ = profile.hips_translation_mode;
        hips_scale_ = static_cast<float>(profile.hips_translation_scale);
        if (hips_mode_ == schema::HipsTranslationMode::scaled_source_delta)
        {
            const auto src = source_indices.find(profile.source_hips_path);
            const auto dst = target_indices.find(profile.target_hips_path);
            if (src == source_indices.end() || dst == target_indices.end() ||
                mapped_sources_[dst->second] != static_cast<std::int32_t>(src->second))
            {
                error = "Retarget hips policy requires a resolved explicit hips mapping";
                return false;
            }
            source_hips_ = src->second;
            target_hips_ = dst->second;
        }
        return true;
    }

    bool retarget(std::span<const schema::JointLocalPose> source_local,
                  std::span<schema::JointLocalPose> target_local) const override
    {
        if (source_local.size() != source_parents_.size() ||
            target_local.size() != target_parents_.size() || !source_local.data() ||
            !target_local.data())
        {
            return false;
        }
        const auto source_begin = reinterpret_cast<std::uintptr_t>(source_local.data());
        const auto target_begin = reinterpret_cast<std::uintptr_t>(target_local.data());
        if (source_begin < target_begin + target_local.size_bytes() &&
            target_begin < source_begin + source_local.size_bytes())
        {
            return false;
        }
        std::vector<ModelPose> source_model(source_local.size());
        for (std::size_t index = 0; index < source_local.size(); ++index)
        {
            const auto parent = source_parents_[index];
            if (!local_valid(source_local[index]) ||
                !compose(source_local[index], parent < 0 ? ModelPose{} : source_model[parent],
                         source_model[index]))
            {
                return false;
            }
        }
        auto candidate = target_reference_;
        std::vector<ModelPose> target_model(target_local.size());
        for (std::size_t index = 0; index < candidate.size(); ++index)
        {
            const auto parent_index = target_parents_[index];
            const auto parent = parent_index < 0 ? ModelPose{} : target_model[parent_index];
            const auto source_index = mapped_sources_[index];
            if (source_index >= 0)
            {
                const auto desired_model_rotation = bx::normalize(
                    bx::mul(bx::mul(frame_alignment_, source_model[source_index].rotation),
                            quaternion(post_rotations_[index])));
                const auto local_rotation =
                    bx::normalize(bx::mul(bx::conjugate(parent.rotation), desired_model_rotation));
                candidate[index].mutable_rotation() = schema::Vec4(
                    local_rotation.x, local_rotation.y, local_rotation.z, local_rotation.w);
            }
            if (hips_mode_ == schema::HipsTranslationMode::scaled_source_delta &&
                index == target_hips_)
            {
                const auto source_delta =
                    bx::sub(source_model[source_hips_].translation,
                            source_reference_model_[source_hips_].translation);
                const auto target_delta =
                    bx::mul(bx::mul(source_delta, frame_alignment_), hips_scale_);
                const auto desired_position =
                    bx::add(target_reference_model_[index].translation, target_delta);
                const auto local_position =
                    bx::mul(bx::mul(bx::sub(desired_position, parent.translation),
                                    bx::conjugate(parent.rotation)),
                            1.0F / parent.scale);
                candidate[index].mutable_translation() =
                    schema::Vec3(local_position.x, local_position.y, local_position.z);
            }
            if (!local_valid(candidate[index]) ||
                !compose(candidate[index], parent, target_model[index]))
            {
                return false;
            }
        }
        std::copy(candidate.begin(), candidate.end(), target_local.begin());
        return true;
    }

private:
    std::vector<std::int32_t> source_parents_, target_parents_, mapped_sources_;
    std::vector<schema::JointLocalPose> target_reference_;
    std::vector<ModelPose> source_reference_model_, target_reference_model_;
    std::vector<schema::Vec4> post_rotations_;
    bx::Quaternion frame_alignment_{0, 0, 0, 1};
    schema::HipsTranslationMode hips_mode_ = schema::HipsTranslationMode::target_reference;
    float hips_scale_ = 1;
    std::size_t source_hips_ = 0, target_hips_ = 0;
};

class ReferencePoseRetargeter final : public AnimationRetargeter
{
public:
    bool prepare(const schema::AnimationRetargetProfileDataT& profile,
                 const schema::SkeletonDataT& source, const schema::SkeletonDataT& target,
                 std::shared_ptr<const AnimationRetargetBinding>& output,
                 std::string& error) override
    {
        output.reset();
        error.clear();
        if (!AnimationRetargetResourceCodec::validate(profile))
        {
            error = "Invalid retarget profile references, mapping, reference pose or hips policy";
            return false;
        }
        auto prepared = std::make_shared<PreparedRetargetBinding>();
        if (!prepared->prepare(profile, source, target, error))
        {
            return false;
        }
        output = std::move(prepared);
        return true;
    }
};
} // namespace

std::unique_ptr<AnimationRetargeter> create_animation_retargeter()
{
    return std::make_unique<ReferencePoseRetargeter>();
}
} // namespace engine::plugins::animation_retarget
