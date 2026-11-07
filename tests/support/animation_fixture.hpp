#include "resource_services.hpp"
#include "scene_snapshot.hpp"
#pragma once

#include "resource_fixture.hpp"

#include "runtime/animation/animation_system.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/scene/animator.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/world/world_system.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace test_support::animation
{
constexpr const char* source_guid = "40000000-0000-4000-8000-000000000001";
constexpr const char* target_guid = "40000000-0000-4000-8000-000000000002";
constexpr const char* clip_guid = "40000000-0000-4000-8000-000000000003";
constexpr const char* scale_guid = "40000000-0000-4000-8000-000000000004";
constexpr const char* reflected_guid = "40000000-0000-4000-8000-000000000005";
constexpr const char* clip_path = "animations/move.animation";
constexpr const char* scale_path = "animations/scale.animation";
constexpr const char* reflected_path = "animations/reflected.animation";
constexpr const char* mesh_guid = "40000000-0000-4000-8000-000000000006";
constexpr const char* model_guid = "40000000-0000-4000-8000-000000000007";
constexpr const char* model_path = "models/character.fbx";

// 测试端 CPU 参考计算，用同一 palette 校验位置、非均匀缩放与镜像；生产只上传骨骼。
inline engine::Vec3 skin_vector(engine::MeshRenderer renderer, std::size_t index,
                                engine::Vec3 value, bool position, bool normal = false)
{
    const auto& weights = renderer.mesh_data()->skin[index];
    const std::array<std::uint16_t, 4> joints{weights.j0(), weights.j1(), weights.j2(),
                                              weights.j3()};
    const std::array<float, 4> factors{weights.w0(), weights.w1(), weights.w2(), weights.w3()};
    const auto matrices = normal ? renderer.skinning_normal_palette() : renderer.skinning_palette();
    if (matrices.empty())
    {
        return value;
    }
    double x = 0, y = 0, z = 0;
    for (std::size_t influence = 0; influence < joints.size(); ++influence)
    {
        const auto& matrix = matrices[joints[influence]];
        x += factors[influence] * (matrix[0] * value.x() + matrix[4] * value.y() +
                                   matrix[8] * value.z() + (position ? matrix[12] : 0));
        y += factors[influence] * (matrix[1] * value.x() + matrix[5] * value.y() +
                                   matrix[9] * value.z() + (position ? matrix[13] : 0));
        z += factors[influence] * (matrix[2] * value.x() + matrix[6] * value.y() +
                                   matrix[10] * value.z() + (position ? matrix[14] : 0));
    }
    return {x, y, z};
}

inline engine::schema::MeshVertex sampled_vertex(engine::MeshRenderer renderer,
                                                 std::size_t index = 0)
{
    const auto& bind = renderer.mesh_data()->vertices[index];
    const auto position = skin_vector(renderer, index, {bind.x(), bind.y(), bind.z()}, true);
    const auto normal =
        skin_vector(renderer, index, {bind.nx(), bind.ny(), bind.nz()}, false, true);
    const auto length = std::hypot(normal.x(), normal.y(), normal.z());
    return {static_cast<float>(position.x()),        static_cast<float>(position.y()),
            static_cast<float>(position.z()),        static_cast<float>(normal.x() / length),
            static_cast<float>(normal.y() / length), static_cast<float>(normal.z() / length)};
}

inline engine::Vec4 sampled_tangent(engine::MeshRenderer renderer, std::size_t index = 0)
{
    const auto& bind = renderer.mesh_data()->vertices[index];
    const auto& tangent = renderer.mesh_data()->tangents[index];
    const auto normal = sampled_vertex(renderer, index);
    auto linear = skin_vector(renderer, index, {tangent.x(), tangent.y(), tangent.z()}, false);
    const double dot =
        normal.nx() * linear.x() + normal.ny() * linear.y() + normal.nz() * linear.z();
    linear = {linear.x() - normal.nx() * dot, linear.y() - normal.ny() * dot,
              linear.z() - normal.nz() * dot};
    const double length = std::hypot(linear.x(), linear.y(), linear.z());
    linear = {linear.x() / length, linear.y() / length, linear.z() / length};
    const auto bitangent =
        skin_vector(renderer, index,
                    {(bind.ny() * tangent.z() - bind.nz() * tangent.y()) * tangent.w(),
                     (bind.nz() * tangent.x() - bind.nx() * tangent.z()) * tangent.w(),
                     (bind.nx() * tangent.y() - bind.ny() * tangent.x()) * tangent.w()},
                    false);
    const double handedness =
        (normal.ny() * linear.z() - normal.nz() * linear.y()) * bitangent.x() +
        (normal.nz() * linear.x() - normal.nx() * linear.z()) * bitangent.y() +
        (normal.nx() * linear.y() - normal.ny() * linear.x()) * bitangent.z();
    return {linear.x(), linear.y(), linear.z(), handedness < 0.0 ? -1.0 : 1.0};
}

inline bool expect(bool condition, const char* description)
{
    if (!condition)
    {
        std::fprintf(stderr, "animation test: %s\n", description);
    }
    return condition;
}

inline bool close(double left, double right)
{
    return std::abs(left - right) < 0.003;
}

inline std::vector<float> translation(float x, float y, float z)
{
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1};
}

inline std::unique_ptr<engine::schema::JointDataT>
joint(const char* name, int parent, engine::Vec3 position, std::vector<float> inverse_bind)
{
    auto result = std::make_unique<engine::schema::JointDataT>();
    result->name = name;
    result->parent = parent;
    result->translation = position;
    result->inverse_bind = std::move(inverse_bind);
    return result;
}

inline engine::schema::SkeletonDataT skeleton(bool source)
{
    engine::schema::SkeletonDataT data;
    data.path = source ? "skeletons/source.skeleton" : "skeletons/target.skeleton";
    data.joints.push_back(joint("root", -1, {}, translation(0, 0, 0)));
    // 两份骨架的 GUID 和索引不同；目标次序还特意不同于 ozz DFS。
    if (source)
    {
        data.joints.push_back(joint("right", 0, {3, 0, 0}, translation(-3, 0, 0)));
        data.joints.push_back(joint("left", 0, {0, 1, 0}, translation(0, -1, 0)));
        data.joints.push_back(joint("tip", 2, {0, 2, 0}, translation(0, -3, 0)));
    }
    else
    {
        data.joints.push_back(joint("left", 0, {0, 1, 0}, translation(0, -1, 0)));
        data.joints.push_back(joint("right", 0, {3, 0, 0}, translation(-3, 0, 0)));
        data.joints.push_back(joint("tip", 1, {0, 2, 0}, translation(0, -3, 0)));
    }
    return data;
}

inline std::unique_ptr<engine::schema::AnimationTrackDataT>
track(std::uint32_t index, engine::Vec3 from, engine::Vec3 to, engine::Vec3 scale = {1, 1, 1})
{
    auto result = std::make_unique<engine::schema::AnimationTrackDataT>();
    result->joint = index;
    result->translations = {{0, from}, {1, to}};
    result->rotations = {{0, {0, 0, 0, 1}}};
    result->scales = {{0, scale}};
    return result;
}

inline engine::schema::AnimationDataT clip(const char* path, bool scale, bool reflect)
{
    engine::schema::AnimationDataT data;
    data.path = path;
    data.name = "Animation test";
    data.skeleton_guid = source_guid;
    data.duration = 1;
    data.tracks.push_back(track(1, {3, 0, 0}, scale ? engine::Vec3(3, 0, 0) : engine::Vec3(7, 0, 0),
                                {reflect ? -2.0
                                 : scale ? 2.0
                                         : 1.0,
                                 1, 1}));
    data.tracks.push_back(track(3, {0, 2, 0}, {0, 6, 0}));
    // root/left 不提供轨道，必须恢复目标骨架绑定局部姿态。
    return data;
}

inline engine::schema::MeshDataT mesh(std::uint16_t joint_index = 2)
{
    const float diagonal = static_cast<float>(std::sqrt(0.5));
    engine::schema::MeshDataT result;
    result.path = "meshes/skinned.mesh";
    result.vertices = {{0, 0, 0, diagonal, diagonal, 0},
                       {1, -1, 0, diagonal, diagonal, 0},
                       {0, 0, -1, diagonal, diagonal, 0}};
    result.indices = {0, 1, 2};
    result.texcoords = {{0, 0}, {1, 0}, {0, 1}};
    result.tangents.assign(3, {diagonal, -diagonal, 0, 1});
    result.skeleton_guid = target_guid;
    result.skin.assign(3, {joint_index, 0, 0, 0, 1, 0, 0, 0});
    result.mesh_to_skeleton = translation(2, 0, 0);
    return result;
}

inline bool add_bytes(test_support::ResourceFixture& files, std::string_view guid,
                      std::string_view path, engine::schema::AssetKind kind,
                      engine::ByteView payload, std::vector<std::string> dependencies = {})
{
    auto entry = std::make_unique<engine::schema::AssetEntryT>();
    entry->guid = guid;
    entry->path = path;
    entry->kind = kind;
    entry->dependencies = std::move(dependencies);
    engine::ByteBuffer entity;
    if (!engine::ResourceSystem::encode_resource(guid, kind, payload, entity, entry->header_size))
    {
        return false;
    }
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto value : entity)
    {
        hash = (hash ^ std::to_integer<std::uint8_t>(value)) * 1099511628211ULL;
    }
    char digest[17]{};
    std::snprintf(digest, sizeof(digest), "%016llx", static_cast<unsigned long long>(hash));
    entry->content_hash = digest;
    entry->content_size = entity.size();
    entry->location = std::make_unique<engine::schema::FileLocationT>();
    entry->location->mount = "content";
    entry->location->path = "gen/" + entry->guid;
    entry->location->length = entity.size();
    std::ofstream output(files.root / entry->location->path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(entity.data()),
                 static_cast<std::streamsize>(entity.size()));
    output.close();
    if (!output)
    {
        return false;
    }
    files.catalog.entries.push_back(std::move(entry));
    return true;
}

inline bool add(test_support::ResourceFixture& files, std::string_view guid, std::string_view path,
                engine::schema::AssetKind kind, const flatbuffers::FlatBufferBuilder& builder,
                std::vector<std::string> dependencies = {})
{
    return add_bytes(
        files, guid, path, kind,
        {reinterpret_cast<const std::byte*>(builder.GetBufferPointer()), builder.GetSize()},
        std::move(dependencies));
}

inline engine::schema::ModelDataT model()
{
    engine::schema::ModelDataT result;
    result.schema_version = 2;
    result.path = model_path;
    auto parent = std::make_unique<engine::schema::ModelNodeDataT>();
    parent->name = "Assembly";
    result.nodes.push_back(std::move(parent));
    auto child = std::make_unique<engine::schema::ModelNodeDataT>();
    child->name = "Body";
    child->parent = 0;
    child->position = {0, 2, 0};
    child->mesh_guid = mesh_guid;
    for (const auto& entry : engine::builtin_resource_catalog().entries)
    {
        if (entry->path == "builtin://materials/default-pbr")
        {
            child->material_guids.push_back(entry->guid);
        }
    }
    result.nodes.push_back(std::move(child));
    return result;
}

inline bool add_model(test_support::ResourceFixture& files)
{
    const auto mesh_data = mesh();
    flatbuffers::FlatBufferBuilder mesh_builder;
    engine::schema::FinishMeshDataBuffer(mesh_builder,
                                         engine::schema::MeshData::Pack(mesh_builder, &mesh_data));
    if (!add(files, mesh_guid, mesh_data.path, engine::schema::AssetKind::mesh, mesh_builder,
             {target_guid}))
    {
        return false;
    }
    const auto model_data = model();
    flatbuffers::FlatBufferBuilder model_builder;
    engine::schema::FinishModelDataBuffer(
        model_builder, engine::schema::ModelData::Pack(model_builder, &model_data));
    std::vector<std::string> dependencies{mesh_guid};
    for (const auto& node : model_data.nodes)
    {
        dependencies.insert(dependencies.end(), node->material_guids.begin(),
                            node->material_guids.end());
    }
    return add(files, model_guid, model_path, engine::schema::AssetKind::model, model_builder,
               dependencies);
}

struct Fixture
{
    test_support::ResourceFixture files;
    test_support::ResourceServices services;
    engine::ResourceSystem& resources = services.core;
    engine::WorldSystem world{&services.loader};
    engine::AnimationSystem animation;

    explicit Fixture(engine::TaskSystem* tasks = nullptr,
                     const engine::AnimationConfig& animation_config = {},
                     engine::ThreadSystem* threads = nullptr)
        : services(threads), animation(&world, tasks, animation_config)
    {
    }

    bool initialize()
    {
        const engine::EngineConfig config;
        if (files.root.empty() || !services.initialize(config) || !world.initialize(config) ||
            !animation.initialize(config) || !world.create_scene("Animation"))
        {
            return false;
        }
        for (bool source : {false, true})
        {
            const auto data = skeleton(source);
            flatbuffers::FlatBufferBuilder builder;
            engine::schema::FinishSkeletonDataBuffer(
                builder, engine::schema::SkeletonData::Pack(builder, &data));
            if (!add(files, source ? source_guid : target_guid, data.path,
                     engine::schema::AssetKind::skeleton, builder))
            {
                return false;
            }
        }
        for (int index = 0; index < 3; ++index)
        {
            const auto* path = index == 0 ? clip_path : index == 1 ? scale_path : reflected_path;
            const auto* guid = index == 0 ? clip_guid : index == 1 ? scale_guid : reflected_guid;
            const auto data = clip(path, index != 0, index == 2);
            flatbuffers::FlatBufferBuilder builder;
            engine::schema::FinishAnimationDataBuffer(
                builder, engine::schema::AnimationData::Pack(builder, &data));
            if (!add(files, guid, path, engine::schema::AssetKind::animation, builder,
                     {source_guid}))
            {
                return false;
            }
        }
        return services.publish_and_load(files.catalog, {{"content", files.root}});
    }

    engine::Node create(bool auto_play = true)
    {
        auto node = world.scene()->create_node("Animated");
        auto renderer = node ? node.add_component<engine::MeshRenderer>() : engine::MeshRenderer{};
        auto animator = node ? node.add_component<engine::Animator>() : engine::Animator{};
        if (!renderer || !animator || !renderer.set_mesh_data(mesh()))
        {
            return {};
        }
        auto settings = test_support::read_settings(animator);
        settings.animation_guid = clip_guid;
        settings.play_on_awake = auto_play;
        return animator.set_settings(settings) ? node : engine::Node{};
    }
};

} // namespace test_support::animation
