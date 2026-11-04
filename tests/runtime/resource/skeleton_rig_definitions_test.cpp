#include "resource_fixture.hpp"

#include "runtime/engine_config.hpp"
#include "runtime/resource/codecs/skeleton_resource_codec.hpp"

#include <cstdio>
#include <limits>

namespace
{
namespace schema = engine::schema;

bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "skeleton rig definitions: %s\n", message);
    }
    return value;
}

schema::SkeletonDataT rig()
{
    schema::SkeletonDataT result;
    result.path = "rigs/with-definitions.skeleton";
    for (int index = 0; index < 5; ++index)
    {
        auto joint = std::make_unique<schema::JointDataT>();
        joint->name = index == 0   ? "Root"
                      : index == 1 ? "Middle"
                      : index == 2 ? "Helper"
                      : index == 3 ? "End"
                                   : "Side";
        joint->parent = index == 0 ? -1 : index == 4 ? 0 : index - 1;
        joint->translation = {index == 0 ? 0.0 : 1.0, 0, 0};
        joint->inverse_bind = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        result.joints.push_back(std::move(joint));
    }
    auto socket = std::make_unique<schema::SkeletonSocketDataT>();
    socket->name = "Grip";
    socket->joint_path = "Root/Middle/Helper/End";
    result.sockets.push_back(std::move(socket));
    auto aim = std::make_unique<schema::SkeletonAimIkDataT>();
    aim->name = "Look";
    aim->joint_path = "Root/Middle";
    aim->order = 1;
    result.aim_ik.push_back(std::move(aim));
    auto chain = std::make_unique<schema::SkeletonTwoBoneIkDataT>();
    chain->name = "Arm";
    chain->start_path = "Root";
    chain->middle_path = "Root/Middle";
    chain->end_path = "Root/Middle/Helper/End";
    chain->order = 1;
    result.two_bone_ik.push_back(std::move(chain));
    return result;
}

bool exercise()
{
    const auto valid = rig();
    if (!expect(engine::SkeletonResourceCodec::validate(valid),
                "valid socket/aim/ancestor IK chain accepts equal stable execution orders"))
    {
        return false;
    }
    auto changed = valid;
    changed.sockets.push_back(
        std::make_unique<schema::SkeletonSocketDataT>(*valid.sockets.front()));
    if (!expect(!engine::SkeletonResourceCodec::validate(changed), "duplicate sockets fail"))
    {
        return false;
    }
    changed = valid;
    changed.sockets.front()->joint_path = "Root/Missing";
    if (!expect(!engine::SkeletonResourceCodec::validate(changed), "unknown socket joint fails"))
    {
        return false;
    }
    changed = valid;
    changed.sockets.front()->offset.mutable_scale() = {0, 1, 1};
    if (!expect(!engine::SkeletonResourceCodec::validate(changed), "singular socket offsets fail"))
    {
        return false;
    }
    changed = valid;
    changed.sockets.front()->offset.mutable_rotation() = {0, 0, 0, 2};
    if (!expect(!engine::SkeletonResourceCodec::validate(changed), "nonunit socket rotation fails"))
    {
        return false;
    }
    changed = valid;
    changed.aim_ik.front()->name = changed.two_bone_ik.front()->name;
    if (!expect(!engine::SkeletonResourceCodec::validate(changed),
                "IK names are unique across kinds"))
    {
        return false;
    }
    changed = valid;
    changed.aim_ik.front()->up_joint = changed.aim_ik.front()->forward_joint;
    if (!expect(!engine::SkeletonResourceCodec::validate(changed), "parallel aim axes fail"))
    {
        return false;
    }
    changed = valid;
    changed.aim_ik.front()->forward_joint = {0, 0, 2};
    if (!expect(!engine::SkeletonResourceCodec::validate(changed), "nonunit aim direction fails"))
    {
        return false;
    }
    changed = valid;
    changed.aim_ik.front()->offset_joint = {std::numeric_limits<double>::quiet_NaN(), 0, 0};
    if (!expect(!engine::SkeletonResourceCodec::validate(changed), "nonfinite aim offset fails"))
    {
        return false;
    }
    changed = valid;
    changed.two_bone_ik.front()->end_path = "Root/Side";
    if (!expect(!engine::SkeletonResourceCodec::validate(changed), "nonancestor chain fails"))
    {
        return false;
    }
    changed = valid;
    changed.two_bone_ik.front()->start_path = changed.two_bone_ik.front()->middle_path;
    if (!expect(!engine::SkeletonResourceCodec::validate(changed), "repeated chain joint fails"))
    {
        return false;
    }
    changed = valid;
    changed.two_bone_ik.front()->mid_axis_joint = {0, 0, 0};
    if (!expect(!engine::SkeletonResourceCodec::validate(changed), "zero bend axis fails"))
    {
        return false;
    }
    changed = valid;
    changed.two_bone_ik.front()->soften = 1.1F;
    if (!expect(!engine::SkeletonResourceCodec::validate(changed), "out of range soften fails"))
    {
        return false;
    }
    changed = valid;
    changed.two_bone_ik.front()->twist_radians = std::numeric_limits<float>::infinity();
    if (!expect(!engine::SkeletonResourceCodec::validate(changed), "nonfinite twist fails"))
    {
        return false;
    }
    test_support::ResourceFixture fixture;
    engine::ResourceSystem resources;
    engine::ResourcePtr loaded;
    return expect(resources.initialize({}) && fixture.add(valid) && fixture.publish(resources) &&
                      fixture.load(valid.path, loaded) && loaded && loaded->skeleton() &&
                      *loaded->skeleton() == valid,
                  "formal skeleton entity round-trips all rig definitions through the same codec");
}
} // namespace

int main()
{
    return exercise() ? 0 : 1;
}
