#include "resource_fixture.hpp"

#include "plugins/runtime/animation_retarget/animation_retarget.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/codecs/animation_retarget_resource_codec.hpp"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <future>
#include <limits>
#include <numbers>
#include <vector>

namespace
{
namespace schema = engine::schema;
using Pose = schema::JointLocalPose;

bool expect(bool value, const char* message, const std::string& error = {})
{
    if (!value)
    {
        std::fprintf(stderr, "animation retarget: %s: %s\n", message, error.c_str());
    }
    return value;
}

bx::Quaternion rotation(float x, float y, float z, float degrees)
{
    return bx::fromAxisAngle({x, y, z}, degrees * std::numbers::pi_v<float> / 180);
}

schema::Vec4 stored(const bx::Quaternion& value)
{
    return {value.x, value.y, value.z, value.w};
}

bx::Quaternion quaternion(const schema::Vec4& value)
{
    return {static_cast<float>(value.x()), static_cast<float>(value.y()),
            static_cast<float>(value.z()), static_cast<float>(value.w())};
}

bool near_rotation(const schema::Vec4& value, bx::Quaternion expected)
{
    return std::abs(std::abs(bx::dot(quaternion(value), expected)) - 1) < 0.00001;
}

schema::SkeletonDataT skeleton(bool target)
{
    schema::SkeletonDataT result;
    result.path = target ? "rigs/target.skeleton" : "rigs/source.skeleton";
    const auto joint = [&](const char* name, int parent, schema::Vec3 t, bx::Quaternion r)
    {
        auto value = std::make_unique<schema::JointDataT>();
        value->name = name;
        value->parent = parent;
        value->translation = t;
        value->rotation = stored(r);
        value->inverse_bind = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        result.joints.push_back(std::move(value));
    };
    if (target)
    {
        joint("Rig", -1, {0, 0, 0}, rotation(0, 1, 0, 90));
        joint("Pelvis", 0, {0, 2, 0}, {0, 0, 0, 1});
        joint("ExtraChest", 1, {0, 0.5, 0}, rotation(0, 0, 1, 15));
        joint("Upper", 2, {1.5, 0, 0}, rotation(0, 0, 1, -30));
        joint("Wrist", 3, {2, 0, 0}, rotation(0, 1, 0, 20));
    }
    else
    {
        joint("Armature", -1, {0, 0, 0}, rotation(0, 0, 1, 90));
        joint("Hips", 0, {0, 1, 0}, {0, 0, 0, 1});
        joint("Arm", 1, {1, 0, 0}, rotation(0, 0, 1, 45));
        joint("Hand", 2, {1, 0, 0}, {0, 0, 0, 1});
    }
    return result;
}

std::vector<Pose> rest(const schema::SkeletonDataT& skeleton)
{
    std::vector<Pose> result;
    for (const auto& joint : skeleton.joints)
    {
        result.emplace_back(joint->translation, joint->rotation, joint->scale);
    }
    return result;
}

schema::AnimationRetargetProfileDataT profile()
{
    schema::AnimationRetargetProfileDataT result;
    result.path = "rigs/humanoid.retarget";
    result.source_skeleton_guid = "10000000-0000-4000-8000-000000000001";
    result.target_skeleton_guid = "10000000-0000-4000-8000-000000000002";
    for (const auto& pair :
         {std::pair{"Armature", "Rig"}, std::pair{"Armature/Hips", "Rig/Pelvis"},
          std::pair{"Armature/Hips/Arm", "Rig/Pelvis/ExtraChest/Upper"},
          std::pair{"Armature/Hips/Arm/Hand", "Rig/Pelvis/ExtraChest/Upper/Wrist"}})
    {
        auto mapping = std::make_unique<schema::RetargetJointMappingDataT>();
        mapping->source_joint_path = pair.first;
        mapping->target_joint_path = pair.second;
        result.joint_mappings.push_back(std::move(mapping));
    }
    return result;
}

bool exercise()
{
    const auto source = skeleton(false);
    const auto target = skeleton(true);
    auto settings = profile();
    auto provider = engine::plugins::animation_retarget::create_animation_retargeter();
    std::shared_ptr<const engine::AnimationRetargetBinding> binding;
    std::string error;
    auto input = rest(source);
    auto output = rest(target);
    if (!expect(provider && provider->prepare(settings, source, target, binding, error) &&
                    binding && binding->retarget(input, output),
                "prepare and transform complete source reference to target reference", error))
    {
        return false;
    }
    for (std::size_t index = 0; index < output.size(); ++index)
    {
        if (!expect(near_rotation(output[index].rotation(),
                                  quaternion(target.joints[index]->rotation)) &&
                        output[index].translation() == target.joints[index]->translation &&
                        output[index].scale() == target.joints[index]->scale,
                    "reference mapping retains each target rest axis, bone length and scale"))
        {
            return false;
        }
    }
    input[2].mutable_rotation() = stored(bx::mul(rotation(0, 0, 1, 45), rotation(0, 1, 0, 40)));
    if (!expect(binding->retarget(input, output),
                "transfer rotation across different parent chains"))
    {
        return false;
    }
    // Analytic local result includes both rigs' different reference axes and the extra target
    // chest.
    const auto expected_upper =
        bx::mul(bx::mul(rotation(0, 0, 1, 30), rotation(0, 1, 0, 40)), rotation(0, 0, 1, -60));
    if (!expect(near_rotation(output[3].rotation(), expected_upper) &&
                    near_rotation(output[2].rotation(), rotation(0, 0, 1, 15)) &&
                    near_rotation(output[4].rotation(), rotation(0, 1, 0, 20)) &&
                    output[4].translation().x() == 2,
                "model-space reference compensation is not a same-index or local-delta copy"))
    {
        return false;
    }
    input[1].mutable_translation() = {0, 2, 0};
    if (!expect(binding->retarget(input, output) && output[1].translation().y() == 2,
                "default InPlace hips policy retains target reference translation"))
    {
        return false;
    }
    settings.hips_translation_mode = schema::HipsTranslationMode::scaled_source_delta;
    settings.source_hips_path = "Armature/Hips";
    settings.target_hips_path = "Rig/Pelvis";
    settings.hips_translation_scale = 2;
    if (!expect(provider->prepare(settings, source, target, binding, error) &&
                    binding->retarget(input, output) &&
                    std::abs(output[1].translation().y() - 4) < 0.00001,
                "explicit hips delta aligns model frames and applies the declared body ratio",
                error))
    {
        return false;
    }
    const auto expected = output;
    std::vector<std::future<bool>> jobs;
    for (unsigned index = 0; index < 8; ++index)
    {
        jobs.push_back(std::async(std::launch::async,
                                  [binding, &input, &expected]
                                  {
                                      auto result = expected;
                                      return binding->retarget(input, result) && result == expected;
                                  }));
    }
    for (auto& job : jobs)
    {
        if (!expect(job.get(), "shared immutable binding isolates concurrent pose outputs"))
        {
            return false;
        }
    }
    input.back().mutable_rotation().mutate_w(std::numeric_limits<double>::quiet_NaN());
    if (!expect(!binding->retarget(input, output) && output == expected &&
                    !binding->retarget(std::span(input).first(1), output) && output == expected,
                "invalid late source joint and wrong span sizes preserve the complete target"))
    {
        return false;
    }
    auto shared_pose = rest(target);
    if (!expect(!binding->retarget(std::span(shared_pose).first(source.joints.size()), shared_pose),
                "in-place/overlapping buffers are rejected"))
    {
        return false;
    }
    auto old_binding = binding;
    settings.joint_mappings.front()->source_joint_path = "Missing";
    if (!expect(!provider->prepare(settings, source, target, binding, error) && !binding &&
                    !error.empty() && old_binding,
                "failed preparation clears its candidate without destroying an active binding"))
    {
        return false;
    }
    settings = profile();
    settings.target_reference_pose = rest(target);
    settings.target_reference_pose.back().mutable_scale() = {1, 2, 1};
    if (!expect(!provider->prepare(settings, source, target, binding, error) && !binding,
                "unsupported nonuniform reference scale fails instead of silently approximating"))
    {
        return false;
    }
    test_support::ResourceFixture fixture;
    engine::EngineConfig config;
    engine::ResourceSystem resources;
    settings = profile();
    engine::ResourcePtr loaded, loaded_source, loaded_target;
    if (!expect(
            resources.initialize(config) && fixture.add(source) && fixture.add(target) &&
                fixture.add(settings) && fixture.publish(resources) &&
                fixture.load(settings.path, loaded) && loaded && loaded->retarget_profile() &&
                fixture.load(source.path, loaded_source) &&
                fixture.load(target.path, loaded_target) &&
                resources.validate_dependencies(
                    *loaded, std::array<engine::ResourcePtr, 2>{loaded_source, loaded_target}) &&
                loaded->info().dependencies.size() == 2,
            "formal retarget resource loads by GUID/mount with precisely two skeleton dependencies",
            resources.last_error()))
    {
        return false;
    }
    std::printf("animation retarget: distinct names/reference axes/extra parent/target "
                "lengths/hips/concurrency/resource passed\n");
    return true;
}
} // namespace

int main()
{
    return exercise() ? 0 : 1;
}
