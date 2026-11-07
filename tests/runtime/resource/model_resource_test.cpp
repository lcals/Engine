#include "animation_fixture.hpp"
#include "scene_snapshot.hpp"

#include "runtime/resource/codecs/animation_resource_codec.hpp"
#include "runtime/resource/codecs/mesh_resource_codec.hpp"
#include "runtime/resource/codecs/model_resource_codec.hpp"
#include "runtime/resource/codecs/skeleton_resource_codec.hpp"

#include <algorithm>
#include <string_view>

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
    bad_model.schema_version = 1;
    if (!expect(!engine::ModelResourceCodec::validate(bad_model),
                "old Model version 1 requires explicit reimport"))
    {
        return false;
    }
    bad_model = good_model;
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
        !fixture.services.publish_and_load(fixture.files.catalog,
                                           {{"content", fixture.files.root}}))
    {
        return false;
    }
    std::unique_ptr<engine::Resource> package;
    std::unique_ptr<engine::Resource> mesh_resource;
    std::unique_ptr<engine::Resource> skeleton_resource;
    std::unique_ptr<engine::Resource> animation_resource;
    std::unique_ptr<engine::Resource> material_resource;
    const auto material_guid = model().nodes[1]->material_guids.front();
    if (!expect(test_support::read_resource(fixture.resources, model_guid, package) &&
                    engine::resource_detail::data<engine::ModelResource>(package.get()) &&
                    !engine::resource_detail::data<engine::MeshResource>(package.get()) &&
                    !engine::resource_detail::data<engine::AnimationResource>(package.get()) &&
                    !engine::resource_detail::data<engine::SkeletonResource>(package.get()),
                "model entity contains references, never embeds action or mesh resources") ||
        !expect(
            test_support::read_resource(fixture.resources, mesh_guid, mesh_resource) &&
                test_support::read_resource(fixture.resources, target_guid, skeleton_resource) &&
                test_support::read_resource(fixture.resources, clip_guid, animation_resource) &&
                test_support::read_resource(fixture.resources, material_guid, material_resource),
            "independent resources can be loaded by GUID") ||
        !expect(
            !test_support::validate_dependencies(fixture.resources, *package, {}) &&
                package->info().dependencies ==
                    std::vector<std::string>{mesh_guid, material_guid} &&
                test_support::validate_dependencies(fixture.resources, *package,
                                                    {mesh_resource.get(), material_resource.get()}),
            "Model depends only on its referenced mesh and material, not skeleton or action "
            "siblings") ||
        !expect(test_support::validate_dependencies(fixture.resources, *mesh_resource,
                                                    {skeleton_resource.get()}),
                "skin indices validate against the actual target skeleton"))
    {
        return false;
    }
    auto& scene = *fixture.world.scene();
    auto existing = scene.create_node("Existing");
    auto root = scene.instantiate_model(model_path, "Character");
    if (!expect(existing && root && test_support::scene_nodes(scene).size() == 4 &&
                    test_support::scene_nodes(scene)[2].parent() == root &&
                    test_support::scene_nodes(scene)[3].parent() ==
                        test_support::scene_nodes(scene)[2] &&
                    test_support::scene_nodes(scene)[3]
                            .get_component<engine::Transform>()
                            .settings()
                            .position.y() == 2,
                "model expands hierarchy and local transforms beneath an ordinary root"))
    {
        return false;
    }
    auto renderer = test_support::scene_nodes(scene)[3].get_component<engine::MeshRenderer>();
    auto animator = test_support::scene_nodes(scene)[3].add_component<engine::Animator>();
    if (!expect(renderer && renderer.mesh_path() == "meshes/skinned.mesh" && animator &&
                    animator.set_animation(clip_path) && fixture.animation.tick(0.5) &&
                    renderer.deformed_vertices().empty() &&
                    renderer.skinning_palette().size() == 4 &&
                    close(sampled_vertex(renderer).x(), 2),
                "instantiated target mesh plays an action from a different source skeleton"))
    {
        return false;
    }
    const auto count = test_support::scene_nodes(scene).size();
    if (!expect(!scene.instantiate_model("builtin://meshes/cube", "Wrong type") &&
                    !scene.instantiate_model("missing.fbx", "Missing") &&
                    test_support::scene_nodes(scene).size() == count,
                "wrong resource types never mutate the existing scene"))
    {
        return false;
    }
    std::ofstream corrupt(fixture.files.root / "gen" / mesh_guid,
                          std::ios::binary | std::ios::trunc);
    corrupt << "corrupted mesh";
    corrupt.close();
    return expect(scene.instantiate_model(model_path, "Loaded candidate") &&
                      test_support::scene_nodes(scene).size() == count + 3 &&
                      scene.find_node(existing.id()) == existing,
                  "instantiation uses its loaded immutable closure without reading modified files");
}

bool migration_and_dependency_contract()
{
    Fixture fixture;
    if (!fixture.initialize() || !add_model(fixture.files))
    {
        return false;
    }
    for (const auto* guid : {model_guid, mesh_guid, clip_guid, target_guid})
    {
        auto found =
            std::find_if(fixture.files.catalog.entries.begin(), fixture.files.catalog.entries.end(),
                         [guid](const auto& entry) { return entry->guid == guid; });
        if (found == fixture.files.catalog.entries.end())
        {
            return false;
        }
        // 额外引用不形成环，必须由资源类型自身拒绝，而非目录的环检测代替。
        auto& dependencies = (*found)->dependencies;
        dependencies.push_back(std::string_view(guid) == clip_guid ? model_guid : clip_guid);
        std::unique_ptr<engine::Resource> loaded;
        if (!expect(fixture.files.publish(fixture.resources) &&
                        !test_support::read_resource(fixture.resources, guid, loaded) && !loaded,
                    "Model, Mesh, Animation and Skeleton reject unrelated source dependencies"))
        {
            return false;
        }
        dependencies.pop_back();
    }

    auto old_model = model();
    old_model.schema_version = 1;
    flatbuffers::FlatBufferBuilder old_builder;
    engine::schema::FinishModelDataBuffer(old_builder,
                                          engine::schema::ModelData::Pack(old_builder, &old_model));
    if (!expect(
            engine::schema::ModelDataT{}.schema_version == 1 &&
                engine::schema::GetModelData(old_builder.GetBufferPointer())->schema_version() == 1,
            "omitted legacy version field retains its original default of 1"))
    {
        return false;
    }
    std::erase_if(fixture.files.catalog.entries,
                  [](const auto& entry) { return entry->guid == model_guid; });
    std::unique_ptr<engine::Resource> loaded;
    if (!expect(add(fixture.files, model_guid, model_path, engine::schema::AssetKind::model,
                    old_builder, {mesh_guid}) &&
                    fixture.files.publish(fixture.resources) &&
                    !test_support::read_resource(fixture.resources, model_guid, loaded) && !loaded,
                "old Model payload is rejected until the editor rebuilds the runtime package"))
    {
        return false;
    }

    auto static_mesh = mesh();
    static_mesh.skeleton_guid.clear();
    static_mesh.skin.clear();
    static_mesh.mesh_to_skeleton.clear();
    flatbuffers::FlatBufferBuilder mesh_builder;
    engine::schema::FinishMeshDataBuffer(
        mesh_builder, engine::schema::MeshData::Pack(mesh_builder, &static_mesh));
    std::erase_if(fixture.files.catalog.entries,
                  [](const auto& entry) { return entry->guid == mesh_guid; });
    return expect(add(fixture.files, mesh_guid, static_mesh.path, engine::schema::AssetKind::mesh,
                      mesh_builder, {clip_guid}) &&
                      fixture.files.publish(fixture.resources) &&
                      !test_support::read_resource(fixture.resources, mesh_guid, loaded) && !loaded,
                  "static Mesh cannot pull an unrelated animation into its dependency graph");
}
} // namespace

int main()
{
    return validation() && loading_and_instantiation() && migration_and_dependency_contract() ? 0
                                                                                              : 1;
}
