#include "animation_fixture.hpp"

#include "runtime/resource/codecs/animation_resource_codec.hpp"
#include "runtime/resource/codecs/mesh_resource_codec.hpp"
#include "runtime/resource/codecs/model_resource_codec.hpp"
#include "runtime/resource/codecs/skeleton_resource_codec.hpp"

namespace
{
using namespace test_support::animation;

bool validation()
{
    const auto good_skeleton = skeleton(false);
    const auto good_clip = clip(clip_path, false, false);
    const auto good_model = model();
    if (!expect(engine::SkeletonResourceCodec::validate(good_skeleton) &&
                    engine::AnimationResourceCodec::validate(good_clip) &&
                    engine::ModelResourceCodec::validate(good_model) &&
                    engine::MeshResourceCodec::validate(mesh()),
                "accept independent model, mesh, skeleton and action models"))
    {
        return false;
    }
    auto bad_skeleton = good_skeleton;
    bad_skeleton.joints[1]->parent = 1;
    if (!expect(!engine::SkeletonResourceCodec::validate(bad_skeleton),
                "reject cyclic or non-topological parent indices"))
    {
        return false;
    }
    bad_skeleton = good_skeleton;
    bad_skeleton.joints[2]->name = bad_skeleton.joints[1]->name;
    if (!expect(!engine::SkeletonResourceCodec::validate(bad_skeleton),
                "reject ambiguous joint names under the same parent"))
    {
        return false;
    }
    bad_skeleton = good_skeleton;
    bad_skeleton.joints[1]->inverse_bind[0] = 0;
    if (!expect(!engine::SkeletonResourceCodec::validate(bad_skeleton),
                "reject singular inverse bind matrices"))
    {
        return false;
    }
    auto bad_model = good_model;
    bad_model.nodes[1]->parent = 50;
    if (!expect(!engine::ModelResourceCodec::validate(bad_model), "reject missing model parent"))
    {
        return false;
    }
    bad_model = good_model;
    bad_model.nodes[0]->scale = {1, 0, 1};
    if (!expect(!engine::ModelResourceCodec::validate(bad_model), "reject zero model scale"))
    {
        return false;
    }
    auto bad_clip = good_clip;
    bad_clip.tracks[1]->joint = bad_clip.tracks[0]->joint;
    if (!expect(!engine::AnimationResourceCodec::validate(bad_clip), "reject duplicate tracks"))
    {
        return false;
    }
    bad_clip = good_clip;
    bad_clip.tracks[0]->translations[1] = {0, {0, 0, 0}};
    if (!expect(!engine::AnimationResourceCodec::validate(bad_clip),
                "reject repeated keyframe timestamps"))
    {
        return false;
    }
    bad_clip = good_clip;
    bad_clip.tracks[0]->rotations[0] = {0, {0, 0, 0, 0}};
    if (!expect(!engine::AnimationResourceCodec::validate(bad_clip),
                "reject nonunit rotation keys"))
    {
        return false;
    }
    bad_clip = good_clip;
    bad_clip.tracks[0]->translations.clear();
    bad_clip.tracks[0]->translations.emplace_back(0, engine::Vec3(65520, 0, 0));
    if (!expect(!engine::AnimationResourceCodec::validate(bad_clip),
                "reject vectors that would compress to float16 infinity"))
    {
        return false;
    }
    bad_clip = good_clip;
    bad_clip.duration = std::numeric_limits<float>::denorm_min();
    for (auto& track : bad_clip.tracks)
    {
        track->translations.resize(1);
    }
    if (!expect(!engine::AnimationResourceCodec::validate(bad_clip),
                "reject durations whose reciprocal overflows runtime float precision"))
    {
        return false;
    }
    bad_clip = good_clip;
    bad_clip.tracks[0]->translations.clear();
    for (std::uint32_t index = 0; index < 65536; ++index)
    {
        bad_clip.tracks[0]->translations.emplace_back(static_cast<float>(index) / 65535.0F,
                                                      engine::Vec3{});
    }
    if (!expect(!engine::AnimationResourceCodec::validate(bad_clip),
                "reject too many unique timestamps before creating an ozz runtime object"))
    {
        return false;
    }
    auto bad_mesh = mesh();
    bad_mesh.skin[0] = {0, 0, 0, 0, 1.1F, -0.1F, 0, 0};
    return expect(!engine::MeshResourceCodec::validate(bad_mesh),
                  "weights must be finite nonnegative normalized values");
}

bool loading_and_instantiation()
{
    Fixture fixture;
    if (!fixture.initialize() || !add_model(fixture.files) ||
        !fixture.files.publish(fixture.resources))
    {
        return false;
    }
    engine::ResourcePtr package;
    engine::ResourcePtr mesh_resource;
    engine::ResourcePtr skeleton_resource;
    engine::ResourcePtr animation_resource;
    if (!expect(fixture.resources.load(model_guid, package) && package->model() &&
                    !package->mesh() && !package->animation() && !package->skeleton(),
                "model entity contains references, never embeds action or mesh resources") ||
        !expect(fixture.resources.load(mesh_guid, mesh_resource) &&
                    fixture.resources.load(target_guid, skeleton_resource) &&
                    fixture.resources.load(clip_guid, animation_resource),
                "independent resources can be loaded by GUID") ||
        !expect(!fixture.resources.validate_dependencies(*package, {}) &&
                    fixture.resources.validate_dependencies(
                        *package, std::vector<engine::ResourcePtr>{skeleton_resource, mesh_resource,
                                                                   animation_resource}),
                "package validates explicitly loaded direct references") ||
        !expect(fixture.resources.validate_dependencies(
                    *mesh_resource, std::vector<engine::ResourcePtr>{skeleton_resource}),
                "skin indices validate against the actual target skeleton"))
    {
        return false;
    }
    auto& scene = *fixture.world.scene();
    auto* existing = scene.create_node("Existing");
    auto* root = scene.instantiate_model(model_path, "Character");
    if (!expect(
            existing && root && scene.nodes().size() == 4 && scene.nodes()[2]->parent() == root &&
                scene.nodes()[3]->parent() == scene.nodes()[2].get() &&
                scene.nodes()[3]->get_component<engine::Transform>()->settings().position.y() == 2,
            "model expands hierarchy and local transforms beneath an ordinary root"))
    {
        return false;
    }
    auto* renderer = scene.nodes()[3]->get_component<engine::MeshRenderer>();
    auto* animator = scene.nodes()[3]->add_component<engine::Animator>();
    if (!expect(renderer && renderer->mesh_path() == "project/meshes/skinned.mesh" && animator &&
                    animator->set_animation(clip_path) && fixture.animation.tick(0.5) &&
                    close(renderer->deformed_vertices()[0].x(), 2),
                "instantiated target mesh plays an action from a different source skeleton"))
    {
        return false;
    }
    const auto count = scene.nodes().size();
    if (!expect(!scene.instantiate_model("builtin/meshes/cube", "Wrong type") &&
                    !scene.instantiate_model("project/missing.fbx", "Missing") &&
                    scene.nodes().size() == count,
                "wrong resource types never mutate the existing scene"))
    {
        return false;
    }
    std::ofstream corrupt(fixture.files.root / "gen" / mesh_guid,
                          std::ios::binary | std::ios::trunc);
    corrupt << "corrupted mesh";
    corrupt.close();
    return expect(!scene.instantiate_model(model_path, "Failed candidate") &&
                      scene.nodes().size() == count && scene.find_node(existing->id()) == existing,
                  "mid-instantiation resource failure rolls back the whole candidate hierarchy");
}
} // namespace

int main()
{
    return validation() && loading_and_instantiation() ? 0 : 1;
}
