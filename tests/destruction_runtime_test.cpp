#include "plugins/runtime/destruction/destruction_component.hpp"
#include "plugins/runtime/destruction/destruction_system.hpp"
#include "resource_fixture.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/scene/collider.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/rigid_body.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"
#include "runtime/world/world_system.hpp"

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <semaphore>
#include <string>
#include <utility>
#include <vector>

namespace
{
using namespace engine;
using plugins::destruction::DestructionComponent;
using plugins::destruction::DestructionSystem;
constexpr const char* asset_guid = "82000000-0000-4000-8000-000000000001";

bool expect(bool condition, const char* message, const std::string& error = {})
{
    if (!condition)
    {
        std::fprintf(stderr, "destruction runtime: %s (%s)\n", message, error.c_str());
    }
    return condition;
}

schema::DestructionAssetDataT chain()
{
    schema::DestructionAssetDataT asset;
    for (std::uint32_t index = 0; index < 3; ++index)
    {
        auto chunk = std::make_unique<schema::DestructionChunkDataT>();
        chunk->centroid = {static_cast<double>(index), 0, 0};
        chunk->anchored = index == 0;
        chunk->mesh = std::make_unique<schema::MeshDataT>();
        for (const float z : {-0.5F, 0.5F})
        {
            for (const float y : {-0.5F, 0.5F})
            {
                for (const float x : {-0.5F, 0.5F})
                {
                    chunk->mesh->vertices.emplace_back(static_cast<float>(index) + x, y, z, 0, 1,
                                                       0);
                }
            }
        }
        chunk->mesh->indices = {0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4,
                                2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5};
        asset.chunks.push_back(std::move(chunk));
    }
    for (std::uint32_t index = 0; index < 2; ++index)
    {
        auto bond = std::make_unique<schema::DestructionBondDataT>();
        bond->chunk_a = index;
        bond->chunk_b = index + 1;
        bond->centroid = {static_cast<double>(index) + 0.5, 0, 0};
        bond->normal = {1, 0, 0};
        asset.bonds.push_back(std::move(bond));
    }
    return asset;
}

bool add_asset(test_support::ResourceFixture& fixture)
{
    const auto asset = chain();
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishDestructionAssetDataBuffer(builder,
                                             schema::DestructionAssetData::Pack(builder, &asset));
    auto entry = std::make_unique<schema::AssetEntryT>();
    entry->guid = asset_guid;
    entry->path = "test.destruction";
    entry->kind = schema::AssetKind::destruction;
    ByteBuffer bytes;
    const ByteView payload(reinterpret_cast<const std::byte*>(builder.GetBufferPointer()),
                           builder.GetSize());
    if (fixture.root.empty() || !ResourceSystem::encode_resource(entry->guid, entry->kind, payload,
                                                                 bytes, entry->header_size))
    {
        return false;
    }
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto value : bytes)
    {
        hash = (hash ^ std::to_integer<std::uint8_t>(value)) * 1099511628211ULL;
    }
    char digest[17]{};
    std::snprintf(digest, sizeof(digest), "%016llx", static_cast<unsigned long long>(hash));
    entry->content_hash = digest;
    entry->content_size = bytes.size();
    entry->location = std::make_unique<schema::FileLocationT>();
    entry->location->mount = "content";
    entry->location->path = "gen/" + entry->guid;
    entry->location->length = bytes.size();
    std::ofstream stream(fixture.root / entry->location->path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    stream.close();
    if (!stream)
    {
        return false;
    }
    fixture.catalog.entries.push_back(std::move(entry));
    return true;
}

ByteBuffer pack(const schema::DestructionStateDataT& state)
{
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishDestructionStateDataBuffer(builder,
                                             schema::DestructionStateData::Pack(builder, &state));
    const auto* begin = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
    return {begin, begin + builder.GetSize()};
}

std::unique_ptr<schema::DestructionStateDataT> unpack(const ByteBuffer& bytes)
{
    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
    flatbuffers::Verifier verifier(data, bytes.size());
    return schema::VerifyDestructionStateDataBuffer(verifier)
               ? schema::UnPackDestructionStateData(data)
               : nullptr;
}

std::vector<std::uint64_t> generated_ids(const Scene& scene)
{
    std::vector<std::uint64_t> result;
    for (const auto& node : scene.nodes())
    {
        if (node->transient())
        {
            result.push_back(node->id());
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

Node* dynamic_group(Scene& scene)
{
    for (const auto& node : scene.nodes())
    {
        const auto* body = node->get_component<RigidBody>();
        if (node->transient() && body && body->settings().motion == schema::BodyMotion::dynamic)
        {
            return node.get();
        }
    }
    return nullptr;
}

bool generated_enabled(const Scene& scene, bool enabled)
{
    bool found = false;
    for (const auto& node : scene.nodes())
    {
        if (!node->transient())
        {
            continue;
        }
        const auto* renderer = node->get_component<MeshRenderer>();
        const auto* collider = node->get_component<Collider>();
        if (!renderer || !collider || renderer->enabled() != enabled ||
            collider->enabled() != enabled)
        {
            return false;
        }
        found = true;
    }
    return found;
}

struct Fixture
{
    ResourceSystem resources;
    WorldSystem world;
    PhysicsSystem physics{&world};
    DestructionSystem* destruction = nullptr;
    test_support::ResourceFixture files;

    ~Fixture()
    {
        // 子系统借用 PhysicsSystem，显式先关闭世界，包含失败提前返回的路径。
        world.shutdown();
        physics.shutdown();
        resources.shutdown();
    }

    bool initialize(TaskSystem* tasks = nullptr, std::size_t max_pending_requests = 64)
    {
        auto owned =
            std::make_unique<DestructionSystem>(world, physics, tasks, max_pending_requests);
        destruction = owned.get();
        std::unique_ptr<System> child = std::move(owned);
        return !destruction->synchronize() && world.add_subsystem(child) &&
               resources.initialize({}) && physics.initialize({}) && world.initialize({}) &&
               add_asset(files) && files.publish(resources);
    }

    bool advance(unsigned frames)
    {
        for (unsigned frame = 0; frame < frames; ++frame)
        {
            if (!world.tick(1.0 / 60.0) || !physics.tick(1.0 / 60.0))
            {
                return false;
            }
        }
        return true;
    }

    bool high_collision(double x, bool expected)
    {
        float fraction = 0;
        return physics.cast_sphere({x, 8, 0}, {0, -4, 0}, 0.1F, fraction) &&
               (expected ? fraction < 1.0F : fraction == 1.0F);
    }
};

bool runtime_contract()
{
    Fixture fixture;
    if (!expect(fixture.initialize(), "initialize resource catalog and world-owned subsystem"))
    {
        return false;
    }
    auto* scene = fixture.world.create_scene("Destruction contracts");
    auto* floor = scene ? scene->create_node("Floor") : nullptr;
    auto* owner = scene ? scene->create_node("Source") : nullptr;
    auto* collider = floor ? floor->add_component<Collider>() : nullptr;
    auto* component = owner ? owner->add_component<DestructionComponent>() : nullptr;
    if (!expect(collider && component && !component->ready(), "empty component has no actors"))
    {
        return false;
    }
    auto floor_pose = floor->get_component<Transform>()->settings();
    floor_pose.position = {10, -0.5, 0};
    auto floor_shape = collider->settings();
    floor_shape.half_extents = {10, 0.5, 10};
    auto source_pose = owner->get_component<Transform>()->settings();
    source_pose.position = {10, 5, 0};
    owner->get_component<Transform>()->set_enabled(false);
    auto settings = component->settings();
    settings.destruction_guid = asset_guid;
    settings.density = 1;
    settings.restitution = 0;
    if (!expect(floor->get_component<Transform>()->set_settings(floor_pose) &&
                    fixture.physics.set_collider_data(*collider, floor_shape) &&
                    owner->get_component<Transform>()->set_settings(source_pose) &&
                    component->set_settings(settings) && !component->ready() &&
                    !fixture.destruction->tick(0) && fixture.destruction->synchronize() &&
                    component->ready() && component->chunk_count() == 3 &&
                    component->group_count() == 1 && component->dynamic_groups() == 0 &&
                    fixture.physics.body_count() == 1 && scene->nodes().size() == 3 &&
                    generated_enabled(*scene, false) && fixture.high_collision(10, false) &&
                    fixture.high_collision(12, false),
                "initially disabled source transform keeps generated renderers and collision off",
                component->last_error()))
    {
        return false;
    }
    owner->get_component<Transform>()->set_enabled(true);
    if (!expect(fixture.destruction->synchronize() && fixture.physics.body_count() == 2 &&
                    generated_enabled(*scene, true) && fixture.high_collision(10, true) &&
                    fixture.high_collision(12, true),
                "enabling source transform publishes collision immediately in preview"))
    {
        return false;
    }
    const auto initialized_revision = component->revision();
    schema::SceneDataT persisted;
    if (!expect(fixture.world.tick(0) && component->revision() == initialized_revision &&
                    scene->build_data(persisted) && persisted.nodes.size() == 2 &&
                    generated_ids(*scene).size() == 1,
                "normal parent ticking reuses actors and scene snapshot omits generated nodes"))
    {
        return false;
    }
    ByteBuffer partial;
    if (!expect(component->damage_sphere({10.5, 5, 0}, 0.1F, 0.25F) &&
                    component->group_count() == 1 && component->dynamic_groups() == 0 &&
                    fixture.physics.body_count() == 2 && component->snapshot(partial),
                "partial world-space bond damage retains connectivity", component->last_error()))
    {
        return false;
    }
    auto partial_state = unpack(partial);
    ByteBuffer restored_partial;
    if (!expect(partial_state && partial_state->bond_health.size() == 3 &&
                    std::abs(partial_state->bond_health[0] - 0.75F) < 0.0001F &&
                    component->reset() && component->restore(partial) &&
                    component->snapshot(restored_partial) && restored_partial == partial,
                "snapshot restores partial bond health after reset", component->last_error()))
    {
        return false;
    }
    if (!expect(component->damage_sphere({10.5, 5, 0}, 0.1F, 1) && component->group_count() == 2 &&
                    component->dynamic_groups() == 1 && fixture.physics.body_count() == 3 &&
                    generated_ids(*scene).size() == 2,
                "breaking A-B leaves anchored A and one dynamic B-C group",
                component->last_error()))
    {
        return false;
    }
    auto* moving = dynamic_group(*scene);
    if (!expect(moving && moving->parent() == nullptr && moving->transient() &&
                    moving->get_component<Collider>()->settings().hulls.size() == 2 &&
                    moving->get_component<MeshRenderer>()->mesh_data()->indices.size() == 72 &&
                    fixture.advance(240) && component->group_count() == 2 &&
                    component->dynamic_groups() == 1 && fixture.physics.body_count() == 3 &&
                    std::abs(moving->get_component<Transform>()->settings().position.y() - 0.5) <
                        0.1 &&
                    fixture.high_collision(10, true) && fixture.high_collision(12, false),
                "Jolt drops connected B-C as one compound while anchored A stays suspended"))
    {
        return false;
    }
    ByteBuffer fallen;
    ByteBuffer after;
    if (!expect(component->snapshot(fallen) && component->reset() &&
                    component->group_count() == 1 && component->dynamic_groups() == 0 &&
                    fixture.physics.body_count() == 2 && fixture.high_collision(12, true) &&
                    component->restore(fallen) && component->snapshot(after) && after == fallen &&
                    fixture.physics.body_count() == 3 && fixture.high_collision(12, false),
                "restore recovers split topology, poses, velocities and immediate collision",
                component->last_error()))
    {
        return false;
    }
    owner->get_component<Transform>()->set_enabled(false);
    if (!expect(!component->damage_sphere({10.5, 5, 0}, 1, 1) &&
                    fixture.destruction->synchronize() && fixture.physics.body_count() == 1 &&
                    generated_enabled(*scene, false) && fixture.high_collision(10, false) &&
                    component->snapshot(after) && after == fallen && component->restore(fallen) &&
                    fixture.physics.body_count() == 1 && generated_enabled(*scene, false) &&
                    fixture.advance(2) && component->snapshot(after) && after == fallen,
                "disabled source removes all bodies and state restore cannot reactivate them",
                component->last_error()))
    {
        return false;
    }
    component->set_enabled(false);
    owner->get_component<Transform>()->set_enabled(true);
    if (!expect(fixture.destruction->synchronize() && fixture.physics.body_count() == 1 &&
                    generated_enabled(*scene, false),
                "source transform alone cannot enable a disabled destruction component"))
    {
        return false;
    }
    component->set_enabled(true);
    if (!expect(fixture.destruction->synchronize() && fixture.physics.body_count() == 3 &&
                    generated_enabled(*scene, true) && fixture.high_collision(10, true) &&
                    component->snapshot(after) && after == fallen,
                "reenabling both source and component restores poses and velocities"))
    {
        return false;
    }
    const auto revision = component->revision();
    const auto previous_nodes = generated_ids(*scene);
    auto wrong = unpack(fallen);
    if (!expect(wrong != nullptr, "decode saved state"))
    {
        return false;
    }
    wrong->base_guid = "82000000-0000-4000-8000-000000000002";
    if (!expect(!component->restore(pack(*wrong)) && component->revision() == revision &&
                    generated_ids(*scene) == previous_nodes && component->snapshot(after) &&
                    after == fallen && fixture.physics.body_count() == 3,
                "foreign base GUID fails without replacing actors or health"))
    {
        return false;
    }
    wrong = unpack(fallen);
    wrong->base_hash = "different-content";
    if (!expect(!component->restore(pack(*wrong)) && component->revision() == revision &&
                    component->snapshot(after) && after == fallen,
                "foreign base content is rejected without mutation"))
    {
        return false;
    }
    wrong = unpack(fallen);
    wrong->groups.back()->transform->scale = {2, 1, 1};
    if (!expect(!component->restore(pack(*wrong)) && component->revision() == revision &&
                    generated_ids(*scene) == previous_nodes && component->snapshot(after) &&
                    after == fallen && fixture.physics.body_count() == 3,
                "failed later group preparation removes candidate nodes and preserves old bodies"))
    {
        return false;
    }
    auto corrupt = fallen;
    corrupt[4] = std::byte{0};
    if (!expect(!component->restore(corrupt) && component->revision() == revision &&
                    component->snapshot(after) && after == fallen,
                "invalid binary state retains the live result"))
    {
        return false;
    }
    moving = dynamic_group(*scene);
    float matrix[16]{};
    if (!expect(moving && moving->get_component<Transform>()->world_matrix(matrix),
                "resolve the surviving dynamic group's world transform"))
    {
        return false;
    }
    const double old_vertical_speed = moving->get_component<RigidBody>()->linear_velocity().y();
    // The intact B-C group's volume-weighted local center is x=1.5.
    const Vec3 impulse_center{matrix[12] + matrix[0] * 1.5, matrix[13] + matrix[1] * 1.5 - 0.2,
                              matrix[14] + matrix[2] * 1.5};
    ByteBuffer impulsed;
    if (!expect(component->damage_sphere(impulse_center, 1, 0, 5) &&
                    component->group_count() == 2 && component->snapshot(impulsed),
                "nonfracturing impulse is part of the committed state", component->last_error()))
    {
        return false;
    }
    auto impulse_state = unpack(impulsed);
    if (!expect(impulse_state != nullptr, "decode state immediately after impulse"))
    {
        return false;
    }
    const auto kicked = std::find_if(
        impulse_state->groups.begin(), impulse_state->groups.end(), [](const auto& group)
        { return group && group->chunks == std::vector<std::uint32_t>{1, 2}; });
    if (!expect(kicked != impulse_state->groups.end() &&
                    (*kicked)->linear_velocity.y() > old_vertical_speed + 1 &&
                    component->restore(impulsed) && component->snapshot(after) && after == impulsed,
                "snapshot taken before physics tick retains the explosion velocity"))
    {
        return false;
    }
    if (!expect(owner->remove_component(DestructionComponent::component_type) &&
                    fixture.destruction->synchronize() && generated_ids(*scene).empty() &&
                    scene->nodes().size() == 2 && fixture.physics.body_count() == 1 &&
                    fixture.high_collision(10, false),
                "removing the component immediately reclaims generated nodes and bodies"))
    {
        return false;
    }
    fixture.world.shutdown();
    return expect(!fixture.destruction->synchronize() && fixture.physics.tick(0) &&
                      fixture.physics.body_count() == 0,
                  "world shutdown ends the subsystem and physics removes the departed scene");
}

bool incremental_contract()
{
    Fixture fixture;
    if (!expect(fixture.initialize(), "initialize incremental destruction runtime"))
    {
        return false;
    }
    auto* scene = fixture.world.create_scene("Incremental destruction");
    auto* owner = scene ? scene->create_node("Source") : nullptr;
    auto* component = owner ? owner->add_component<DestructionComponent>() : nullptr;
    if (!expect(component != nullptr, "create incremental destruction source"))
    {
        return false;
    }
    auto pose = owner->get_component<Transform>()->settings();
    pose.position = {0, 10, 0};
    auto settings = component->settings();
    settings.destruction_guid = asset_guid;
    settings.density = 1;
    if (!expect(owner->get_component<Transform>()->set_settings(pose) &&
                    component->set_settings(settings) && fixture.destruction->synchronize(),
                "prepare incremental groups", component->last_error()))
    {
        return false;
    }
    const auto original_ids = generated_ids(*scene);
    if (!expect(original_ids.size() == 1, "publish exactly one initial physical group"))
    {
        return false;
    }
    const auto original_revision = component->revision();
    auto* original_node = scene->find_node(original_ids.front());
    auto* original_mesh = original_node->get_component<MeshRenderer>();
    const auto* original_data = original_mesh->mesh_data();
    const auto mesh_revision = original_mesh->mesh_revision();
    ByteBuffer before;
    ByteBuffer after;
    if (!expect(component->snapshot(before) &&
                    component->damage_sphere({1000, 1000, 0}, 1, 1, 10) &&
                    component->damage_sphere({0.5, 10, 0}, 1, 0, 0) && component->snapshot(after) &&
                    after == before && component->revision() == original_revision &&
                    generated_ids(*scene) == original_ids && fixture.physics.body_count() == 1,
                "missed and zero-damage explosions retain state, revision and physical groups"))
    {
        return false;
    }
    if (!expect(component->damage_sphere({0.5, 10, 0}, 0.1F, 0.25F) &&
                    component->revision() == original_revision + 1 &&
                    generated_ids(*scene) == original_ids &&
                    scene->find_node(original_ids.front())->get_component<MeshRenderer>() ==
                        original_mesh &&
                    original_mesh->mesh_data() == original_data &&
                    original_mesh->mesh_revision() == mesh_revision &&
                    fixture.physics.body_count() == 1 && component->snapshot(after),
                "partial health damage preserves generated mesh, collider and body identity"))
    {
        return false;
    }
    auto partial = unpack(after);
    if (!expect(partial && std::abs(partial->bond_health[0] - 0.75F) < 0.0001F &&
                    component->damage_sphere({0.5, 10, 0}, 0.1F, 1) &&
                    component->group_count() == 2,
                "partial health is published before the later bond fracture"))
    {
        return false;
    }
    auto* moving = dynamic_group(*scene);
    if (!expect(moving != nullptr, "find the separated B-C group"))
    {
        return false;
    }
    const auto moving_id = moving->id();
    const auto split_ids = generated_ids(*scene);
    const auto anchored_id = split_ids.front() == moving_id ? split_ids.back() : split_ids.front();
    auto* anchored_mesh = scene->find_node(anchored_id)->get_component<MeshRenderer>();
    const auto* anchored_data = anchored_mesh->mesh_data();
    auto* body = moving->get_component<RigidBody>();
    auto body_settings = body->settings();
    body_settings.gravity_factor = 0;
    body_settings.linear_damping = 0.1F;
    body_settings.angular_damping = 0.2F;
    body_settings.linear_velocity = {1, 0, 0};
    body_settings.angular_velocity = {0, 0.25, 0};
    if (!expect(body->set_settings(body_settings) && fixture.physics.tick(0) &&
                    component->damage_sphere({1.5, 10, 0}, 0.1F, 0.25F) &&
                    generated_ids(*scene) == split_ids && body->settings() == body_settings &&
                    component->snapshot(before),
                "zero-impulse partial damage retains motion and nondefault body properties"))
    {
        return false;
    }
    const auto impulse_revision = component->revision();
    if (!expect(!component->damage_sphere({1.5, 9.8, 0}, 1, 0, 1000000) &&
                    component->snapshot(after) && after == before &&
                    generated_ids(*scene) == split_ids &&
                    component->revision() == impulse_revision && fixture.physics.body_count() == 2,
                "invalid candidate velocity leaves existing graph, nodes and physics unchanged"))
    {
        return false;
    }
    if (!expect(
            body->add_impulse({0, 2, 0}) && component->damage_sphere({1.5, 9.8, 0}, 1, 0, 5) &&
                component->revision() == impulse_revision + 1 &&
                generated_ids(*scene) == split_ids && moving->get_component<RigidBody>() == body &&
                std::abs(body->linear_velocity().y() - 2.0) < 0.0001 &&
                body->settings().gravity_factor == 0 && body->settings().linear_damping == 0.1F &&
                body->settings().angular_damping == 0.2F &&
                body->settings().angular_velocity == body_settings.angular_velocity &&
                fixture.physics.tick(0) && std::abs(body->linear_velocity().y() - 3.0) < 0.0001 &&
                fixture.advance(1) &&
                moving->get_component<Transform>()->settings().position.y() > 10.02 &&
                body->linear_velocity().y() > 2.9,
            "reused body consumes explosion velocity and pending impulse in actual Jolt motion",
            component->last_error()))
    {
        return false;
    }
    float matrix[16]{};
    if (!expect(moving->get_component<Transform>()->world_matrix(matrix) &&
                    component->snapshot(before),
                "capture moving source before incremental split"))
    {
        return false;
    }
    const Vec3 second_bond{matrix[12] + matrix[0] * 1.5, matrix[13] + matrix[1] * 1.5,
                           matrix[14] + matrix[2] * 1.5};
    if (!expect(component->damage_sphere(second_bond, 0.1F, 1) && component->group_count() == 3 &&
                    fixture.physics.body_count() == 3 && scene->find_node(moving_id) == nullptr &&
                    component->owns_fragment(anchored_id) &&
                    scene->find_node(anchored_id)->get_component<MeshRenderer>() == anchored_mesh &&
                    anchored_mesh->mesh_data() == anchored_data && component->snapshot(after),
                "splitting B-C replaces only its group and retains unrelated anchored A"))
    {
        return false;
    }
    const auto source = unpack(before);
    const auto fractured = unpack(after);
    const auto parent =
        std::find_if(source->groups.begin(), source->groups.end(), [](const auto& group)
                     { return group->chunks == std::vector<std::uint32_t>{1, 2}; });
    if (!expect(parent != source->groups.end(), "find source rigid group velocities"))
    {
        return false;
    }
    for (const auto& group : fractured->groups)
    {
        if (group->chunks == std::vector<std::uint32_t>{0})
        {
            continue;
        }
        const double offset = static_cast<double>(group->chunks.front()) - 1.5;
        const Vec3 delta{matrix[0] * offset, matrix[1] * offset, matrix[2] * offset};
        const auto& velocity = (*parent)->linear_velocity;
        const auto& omega = (*parent)->angular_velocity;
        if (!expect(group->angular_velocity == omega &&
                        std::abs(group->linear_velocity.x() - velocity.x() - omega.y() * delta.z() +
                                 omega.z() * delta.y()) < 0.000001 &&
                        std::abs(group->linear_velocity.y() - velocity.y() - omega.z() * delta.x() +
                                 omega.x() * delta.z()) < 0.000001 &&
                        std::abs(group->linear_velocity.z() - velocity.z() - omega.x() * delta.y() +
                                 omega.y() * delta.x()) < 0.000001,
                    "split fragments inherit center-of-mass and angular velocity"))
        {
            return false;
        }
    }
    const auto before_settings = generated_ids(*scene);
    settings.friction = 0.35F;
    if (!expect(component->set_settings(settings) && generated_ids(*scene) != before_settings &&
                    component->group_count() == 3,
                "explicit settings change still rebuilds every group"))
    {
        return false;
    }
    for (const auto id : before_settings)
    {
        if (!expect(!component->owns_fragment(id), "settings rebuild retires every old group"))
        {
            return false;
        }
    }
    for (const auto id : generated_ids(*scene))
    {
        if (!expect(scene->find_node(id)->get_component<Collider>()->settings().friction == 0.35F,
                    "settings rebuild applies the new collision configuration"))
        {
            return false;
        }
    }
    return true;
}

bool support_contract()
{
    Fixture fixture;
    WorldSystem foreign_world;
    if (!expect(fixture.initialize() && foreign_world.initialize({}),
                "initialize explicit support query worlds"))
    {
        return false;
    }
    auto* scene = fixture.world.create_scene("Foundation support");
    auto* foreign_scene = foreign_world.create_scene("Foreign support");
    auto* foreign_node = foreign_scene ? foreign_scene->create_node("Foreign floor") : nullptr;
    auto* foreign_support = foreign_node ? foreign_node->add_component<Collider>() : nullptr;
    auto* floor = scene ? scene->create_node("Foundation") : nullptr;
    auto* owner = scene ? scene->create_node("Building") : nullptr;
    auto* support = floor ? floor->add_component<Collider>() : nullptr;
    auto* component = owner ? owner->add_component<DestructionComponent>() : nullptr;
    if (!expect(foreign_support && support && component, "create support query components"))
    {
        return false;
    }
    auto floor_pose = floor->get_component<Transform>()->settings();
    floor_pose.position = {10, -0.5, 2};
    auto floor_shape = support->settings();
    floor_shape.half_extents = {4, 0.5, 4};
    auto source_pose = owner->get_component<Transform>()->settings();
    source_pose.position = {10, 1, 2};
    source_pose.rotation = {0, 90, 0};
    source_pose.scale = {2, 2, 2};
    auto settings = component->settings();
    settings.destruction_guid = asset_guid;
    settings.density = 1;
    if (!expect(floor->get_component<Transform>()->set_settings(floor_pose) &&
                    fixture.physics.set_collider_data(*support, floor_shape) &&
                    owner->get_component<Transform>()->set_settings(source_pose) &&
                    component->set_settings(settings) && fixture.destruction->synchronize(),
                "publish rotated scaled building and its explicit static foundation"))
    {
        return false;
    }
    const auto initial_nodes = generated_ids(*scene);
    const auto initial_revision = component->revision();
    auto* own_node = initial_nodes.empty() ? nullptr : scene->find_node(initial_nodes.front());
    auto* own_collider = own_node ? own_node->get_component<Collider>() : nullptr;
    ByteBuffer intact;
    ByteBuffer after;
    if (!expect(own_collider && component->owns_fragment(own_node->id()) &&
                    !component->owns_fragment(owner->id()) &&
                    !component->owns_fragment(floor->id()) && component->snapshot(intact) &&
                    component->refresh_support(*support, 0.2F, 1) &&
                    component->revision() == initial_revision &&
                    generated_ids(*scene) == initial_nodes && component->snapshot(after) &&
                    after == intact,
                "full world-space coverage keeps live nodes, state and revision unchanged",
                component->last_error()))
    {
        return false;
    }
    if (!expect(!component->refresh_support(*own_collider, 0.2F, 0.5F) &&
                    !component->refresh_support(*foreign_support, 0.2F, 0.5F) &&
                    !component->refresh_support(*support, 0, 0.5F) &&
                    !component->refresh_support(*support, 1001, 0.5F) &&
                    !component->refresh_support(*support, 0.2F, 0) &&
                    !component->refresh_support(*support, 0.2F, 1.01F) &&
                    !component->refresh_support(*support, std::numeric_limits<float>::quiet_NaN(),
                                                0.5F) &&
                    component->snapshot(after) && after == intact &&
                    component->revision() == initial_revision,
                "self fragments, foreign scene and invalid parameters cannot release anchors"))
    {
        return false;
    }
    component->set_enabled(false);
    if (!expect(!component->refresh_support(*support, 0.2F, 0.5F),
                "disabled destruction component rejects foundation refresh"))
    {
        return false;
    }
    component->set_enabled(true);
    owner->get_component<Transform>()->set_enabled(false);
    if (!expect(!component->refresh_support(*support, 0.2F, 0.5F),
                "disabled source transform rejects foundation refresh"))
    {
        return false;
    }
    owner->get_component<Transform>()->set_enabled(true);
    auto invalid_support = floor_shape;
    invalid_support.is_trigger = true;
    if (!expect(fixture.physics.set_collider_data(*support, invalid_support) &&
                    !component->refresh_support(*support, 0.2F, 0.5F) &&
                    fixture.physics.set_collider_data(*support, floor_shape),
                "trigger support is rejected without graph mutation"))
    {
        return false;
    }
    auto* floor_body = floor->add_component<RigidBody>();
    if (!expect(floor_body && !component->refresh_support(*support, 0.2F, 0.5F) &&
                    floor->remove_component(RigidBody::component_type) &&
                    component->snapshot(after) && after == intact,
                "dynamic support is rejected without graph mutation"))
    {
        return false;
    }

    float matrix[16];
    if (!expect(owner->get_component<Transform>()->world_matrix(matrix),
                "resolve transformed bond positions"))
    {
        return false;
    }
    const Vec3 first_bond{matrix[12] + matrix[0] * 0.5, matrix[13] + matrix[1] * 0.5,
                          matrix[14] + matrix[2] * 0.5};
    const Vec3 second_bond{matrix[12] + matrix[0] * 1.5, matrix[13] + matrix[1] * 1.5,
                           matrix[14] + matrix[2] * 1.5};
    ByteBuffer moving;
    if (!expect(component->damage_sphere(second_bond, 0.1F, 0.25F) &&
                    component->damage_sphere(first_bond, 0.1F, 1) && component->snapshot(moving),
                "prepare partial damage and a separately moving group"))
    {
        return false;
    }
    auto state = unpack(moving);
    if (!expect(state && state->groups.size() == 2, "read two source groups"))
    {
        return false;
    }
    auto& dynamic = *state->groups[1];
    dynamic.transform->position = {11, 6, 2};
    dynamic.transform->rotation = {15, 30, 5};
    dynamic.linear_velocity = {1, 2, 3};
    dynamic.angular_velocity = {0.1, 0.2, 0.3};
    if (!expect(component->restore(pack(*state)) && component->snapshot(moving),
                "publish nonzero current pose and velocities before support loss"))
    {
        return false;
    }
    // Half of the real bottom is covered, including the center on its boundary.
    // A single center ray would miss the loss of area; the weighted samples must detect it.
    floor_pose.position = {9.5, -0.5, 2};
    floor_shape.half_extents = {0.5, 0.5, 4};
    const auto supported_nodes = generated_ids(*scene);
    const auto* unaffected = dynamic_group(*scene);
    const auto anchored =
        std::find_if(supported_nodes.begin(), supported_nodes.end(),
                     [&](std::uint64_t id)
                     {
                         const auto* body = scene->find_node(id)->get_component<RigidBody>();
                         return body && body->settings().motion == schema::BodyMotion::static_body;
                     });
    if (!expect(unaffected && anchored != supported_nodes.end(),
                "identify anchored and moving groups independently of entity generations"))
    {
        return false;
    }
    const auto unaffected_id = unaffected->id();
    const auto anchored_id = *anchored;
    const auto supported_revision = component->revision();
    if (!expect(floor->get_component<Transform>()->set_settings(floor_pose) &&
                    fixture.physics.set_collider_data(*support, floor_shape) &&
                    component->refresh_support(*support, 0.2F, 0.4F) &&
                    component->revision() == supported_revision &&
                    generated_ids(*scene) == supported_nodes &&
                    component->refresh_support(*support, 0.2F, 0.6F) &&
                    component->revision() == supported_revision + 1 &&
                    component->group_count() == 2 && component->dynamic_groups() == 2 &&
                    component->snapshot(after),
                "area coverage threshold releases only the unsupported anchor",
                component->last_error()))
    {
        return false;
    }
    auto expected = unpack(moving);
    expected->bond_health.back() = 0;
    if (!expect(after == pack(*expected) && !component->owns_fragment(anchored_id) &&
                    component->owns_fragment(unaffected_id),
                "release preserves unrelated bond health and exact group poses and velocities"))
    {
        return false;
    }
    const auto released_nodes = generated_ids(*scene);
    const auto released_revision = component->revision();
    floor_pose.position = {10, -0.5, 2};
    floor_shape.half_extents = {4, 0.5, 4};
    if (!expect(floor->get_component<Transform>()->set_settings(floor_pose) &&
                    fixture.physics.set_collider_data(*support, floor_shape) &&
                    component->refresh_support(*support, 0.2F, 1) &&
                    component->revision() == released_revision &&
                    generated_ids(*scene) == released_nodes && component->snapshot(after) &&
                    after == pack(*expected),
                "refilling ground never resurrects broken anchors or rebuilds unchanged groups"))
    {
        return false;
    }
    if (!expect(component->restore(intact), "restore anchored building for missing support"))
    {
        return false;
    }
    support->set_enabled(false);
    if (!expect(fixture.physics.set_collider_data(*support, floor_shape) &&
                    component->refresh_support(*support, 0.2F, 0.5F) &&
                    component->dynamic_groups() == 1,
                "a disabled committed support collider has zero coverage"))
    {
        return false;
    }
    support->set_enabled(true);
    schema::MeshDataT empty;
    if (!expect(component->restore(intact) && fixture.physics.set_mesh_data(*support, empty) &&
                    component->refresh_support(*support, 0.2F, 0.5F) &&
                    component->dynamic_groups() == 1,
                "a valid empty support mesh releases anchors without a query error"))
    {
        return false;
    }
    return true;
}

bool stress_contract()
{
    Fixture fixture;
    if (!expect(fixture.initialize(), "initialize stress runtime"))
    {
        return false;
    }
    auto* scene = fixture.world.create_scene("Stress contracts");
    auto* owner = scene ? scene->create_node("Cantilever") : nullptr;
    auto* component = owner ? owner->add_component<DestructionComponent>() : nullptr;
    auto* transform = owner ? owner->get_component<Transform>() : nullptr;
    if (!expect(component && transform, "create stress source"))
    {
        return false;
    }
    auto pose = transform->settings();
    pose.position = {12, 8, -4};
    pose.rotation = {0, 37, 25};
    pose.scale = {1.7, 1.7, 1.7};
    auto settings = component->settings();
    settings.destruction_guid = asset_guid;
    settings.density = 100;
    if (!expect(transform->set_settings(pose) && component->set_settings(settings) &&
                    fixture.destruction->synchronize() && component->ready(),
                "publish rotated, scaled cantilever", component->last_error()))
    {
        return false;
    }
    ByteBuffer initial;
    if (!expect(component->snapshot(initial), "capture source stress state"))
    {
        return false;
    }
    const auto ids = generated_ids(*scene);
    const auto revision = component->revision();
    std::uint32_t broken = 99;
    ByteBuffer after;
    if (!expect(component->apply_stress({0, -9.81, 0}, 1e8F, 2e8F, 500, broken) && broken == 0 &&
                    component->revision() == revision && generated_ids(*scene) == ids &&
                    component->snapshot(after) && after == initial,
                "safe stress preserves snapshots, revision and physical node identities",
                component->last_error()))
    {
        return false;
    }
    component->set_enabled(false);
    if (!expect(!component->apply_stress({0, -9.81, 0}, 10, 20, 500, broken) && broken == 0 &&
                    component->revision() == revision,
                "disabled component rejects stress without publication"))
    {
        return false;
    }
    component->set_enabled(true);
    if (!expect(!component->apply_stress({0, -9.81, 0}, 20, 10, 500, broken) && broken == 0 &&
                    component->snapshot(after) && after == initial &&
                    !component->apply_stress({0, -9.81, 0}, 10, 20, 0, broken) && broken == 0 &&
                    component->snapshot(after) && after == initial,
                "invalid stress inputs preserve live state"))
    {
        return false;
    }
    if (!expect(component->apply_stress({0, -9.81, 0}, 10, 20, 500, broken) && broken > 0 &&
                    component->group_count() > 1 && component->dynamic_groups() > 0 &&
                    component->snapshot(after),
                "official stress splits weak cantilever into dynamic Jolt groups",
                component->last_error()))
    {
        return false;
    }
    const auto original = unpack(initial);
    const auto fractured = unpack(after);
    for (const auto& group : fractured->groups)
    {
        const auto& before_pose = *original->groups.front()->transform;
        const auto& after_pose = *group->transform;
        if (!expect(after_pose.position.x() == before_pose.position.x() &&
                        after_pose.position.y() == before_pose.position.y() &&
                        after_pose.position.z() == before_pose.position.z() &&
                        after_pose.rotation.y() == before_pose.rotation.y() &&
                        after_pose.rotation.z() == before_pose.rotation.z() &&
                        after_pose.scale.x() == before_pose.scale.x(),
                    "stress fragments inherit their existing world pose and scale"))
        {
            return false;
        }
    }
    const auto broken_revision = component->revision();
    if (!expect(component->restore(initial) && component->group_count() == 1 &&
                    component->dynamic_groups() == 0 && component->revision() > broken_revision &&
                    component->snapshot(after) && after == initial &&
                    component->apply_stress({0, -9.81, 0}, 10, 20, 500, broken) && broken > 0,
                "restoring a snapshot clears stale solver state and permits repeat stress",
                component->last_error()))
    {
        return false;
    }
    return true;
}

bool asynchronous_damage_contract()
{
    ThreadSystem threads({.worker_limit = 1});
    TaskSystem tasks(threads, {.name = "Destruction asynchronous tests", .max_concurrency = 1});
    Fixture fixture;
    if (!expect(threads.initialize({}) && tasks.initialize({}) && fixture.initialize(&tasks, 2),
                "initialize asynchronous destruction task budget"))
    {
        return false;
    }
    auto* scene = fixture.world.create_scene("Async destruction");
    auto* owner = scene ? scene->create_node("Source") : nullptr;
    auto* component = owner ? owner->add_component<DestructionComponent>() : nullptr;
    if (!component)
    {
        return false;
    }
    auto pose = owner->get_component<Transform>()->settings();
    pose.position = {0, 10, 0};
    auto settings = component->settings();
    settings.destruction_guid = asset_guid;
    settings.density = 1;
    if (!owner->get_component<Transform>()->set_settings(pose) ||
        !component->set_settings(settings) || !fixture.destruction->synchronize())
    {
        return false;
    }
    const auto ids = generated_ids(*scene);
    const auto revision = component->revision();
    std::vector<bool> results;
    const auto completed = [&](bool success) { results.push_back(success); };
    std::binary_semaphore gate(0);
    auto occupied = tasks.submit("Hold destruction worker",
                                 [&]
                                 {
                                     gate.acquire();
                                     return true;
                                 });
    if (!occupied.valid())
    {
        return false;
    }
    const bool queued =
        fixture.destruction->request_damage(*component, {0.5, 10, 0}, 0.1F, 0.25F, 0, completed) &&
        fixture.destruction->request_damage(*component, {0.5, 10, 0}, 0.1F, 0.25F, 0, completed) &&
        !fixture.destruction->request_damage(*component, {}, 1, 1, 0, completed) &&
        fixture.destruction->synchronize() && results.empty() && component->revision() == revision;
    gate.release();
    ByteBuffer bytes;
    if (!expect(occupied.get() && queued && tasks.wait() && fixture.destruction->synchronize() &&
                    results == std::vector<bool>{true} && component->revision() == revision + 1 &&
                    tasks.wait() && fixture.destruction->synchronize() &&
                    results == std::vector<bool>({true, true}) && generated_ids(*scene) == ids &&
                    component->snapshot(bytes),
                "nonblocking ordered damage keeps unchanged geometry and obeys commit budget",
                component->last_error()))
    {
        return false;
    }
    auto saved = unpack(bytes);
    if (!expect(saved && std::abs(saved->bond_health[0] - 0.5F) < 0.0001F,
                "queued Blast candidates accumulate both damages"))
    {
        return false;
    }

    results.clear();
    if (!expect(
            fixture.destruction->request_damage(*component, {0.5, 10, 0}, 0.1F, 1, 0, completed) &&
                fixture.destruction->synchronize() && tasks.wait() && component->reset() &&
                fixture.destruction->synchronize() && results == std::vector<bool>{false} &&
                component->group_count() == 1,
            "reset invalidates a fully prepared Blast candidate"))
    {
        return false;
    }

    if (!component->damage_sphere({0.5, 10, 0}, 0.1F, 1))
    {
        return false;
    }
    auto* moving = dynamic_group(*scene);
    if (!moving)
    {
        return false;
    }
    auto body = moving->get_component<RigidBody>()->settings();
    body.gravity_factor = 0;
    body.linear_damping = 0;
    body.linear_velocity = {3, 0, 0};
    if (!moving->get_component<RigidBody>()->set_settings(body) || !fixture.physics.tick(0))
    {
        return false;
    }
    results.clear();
    if (!fixture.destruction->request_damage(*component, {1.5, 10, 0}, 0.1F, 1, 0, completed) ||
        !fixture.destruction->synchronize() || !tasks.wait() || !fixture.physics.tick(1.0 / 60.0))
    {
        return false;
    }
    const auto moved_pose = moving->get_component<Transform>()->settings();
    if (!expect(moved_pose.position.x() > 0 && fixture.destruction->synchronize() &&
                    results == std::vector<bool>{true} && component->group_count() == 3,
                "split commits after physics has advanced the original dynamic group",
                component->last_error()))
    {
        return false;
    }
    for (const auto& node : scene->nodes())
    {
        const auto* rigid = node->get_component<RigidBody>();
        if (node->transient() && rigid && rigid->settings().motion == schema::BodyMotion::dynamic &&
            !expect(node->get_component<Transform>()->settings() == moved_pose &&
                        std::abs(rigid->settings().linear_velocity.x() - 3) < 0.0001,
                    "new fragments retain current pose and velocity instead of request-time state"))
        {
            return false;
        }
    }

    results.clear();
    moving = dynamic_group(*scene);
    if (!fixture.destruction->request_damage(*component, {}, 100, 0, 1, completed) ||
        !fixture.destruction->synchronize() || !tasks.wait())
    {
        return false;
    }
    auto scaled = moving->get_component<Transform>()->settings();
    scaled.scale = {2, 2, 2};
    if (!expect(moving->get_component<Transform>()->set_settings(scaled) &&
                    fixture.destruction->synchronize() && results == std::vector<bool>{false},
                "source scale conflicts cancel stale impulse and geometry"))
    {
        return false;
    }
    if (!component->reset())
    {
        return false;
    }

    auto* identity_node = scene->create_node("Identity");
    auto* original = identity_node->add_component<DestructionComponent>();
    if (!original || !original->set_settings(settings) || !fixture.destruction->synchronize())
    {
        return false;
    }
    const auto original_revision = original->revision();
    results.clear();
    if (!fixture.destruction->request_damage(*original, {0.5, 0, 0}, 0.1F, 1, 0, completed) ||
        !fixture.destruction->synchronize() || !tasks.wait() ||
        !identity_node->remove_component(DestructionComponent::component_type))
    {
        return false;
    }
    auto* replacement = identity_node->add_component<DestructionComponent>();
    if (!expect(replacement && replacement->set_settings(settings) &&
                    fixture.destruction->synchronize() && results == std::vector<bool>{false} &&
                    replacement->revision() == original_revision && replacement->group_count() == 1,
                "old request cannot attach to a new component with the same revision"))
    {
        return false;
    }

    results.clear();
    if (!fixture.destruction->request_damage(*replacement, {}, 10, 1, 0, completed) ||
        !fixture.destruction->synchronize())
    {
        return false;
    }
    fixture.world.destroy_scene();
    if (!expect(fixture.world.create_scene("Other") && tasks.wait() &&
                    fixture.destruction->synchronize() && results == std::vector<bool>{false},
                "scene replacement cancels old damage without accessing its objects"))
    {
        return false;
    }
    owner = fixture.world.scene()->create_node("Shutdown");
    component = owner->add_component<DestructionComponent>();
    results.clear();
    if (!component || !component->set_settings(settings) || !fixture.destruction->synchronize() ||
        !fixture.destruction->request_damage(*component, {}, 10, 1, 0, completed) ||
        !fixture.destruction->synchronize())
    {
        return false;
    }
    fixture.world.shutdown();
    return expect(tasks.wait() && results.empty(),
                  "closing the world drops pending damage delegates");
}

bool asynchronous_damage_fairness(std::size_t concurrency)
{
    ThreadSystem threads({.worker_limit = concurrency});
    TaskSystem tasks(threads, {.name = "Destruction fairness", .max_concurrency = concurrency});
    Fixture fixture;
    if (!threads.initialize({}) || !tasks.initialize({}) || !fixture.initialize(&tasks, 4))
    {
        return false;
    }
    auto* scene = fixture.world.create_scene("Fair damage requests");
    auto* low_node = scene ? scene->create_node("Frequent damage") : nullptr;
    auto* high_node = scene ? scene->create_node("Occasional damage") : nullptr;
    auto* low = low_node ? low_node->add_component<DestructionComponent>() : nullptr;
    auto* high = high_node ? high_node->add_component<DestructionComponent>() : nullptr;
    schema::DestructionDataT settings;
    settings.destruction_guid = asset_guid;
    if (!low || !high || !low->set_settings(settings) || !high->set_settings(settings) ||
        !fixture.destruction->synchronize())
    {
        return false;
    }
    bool high_finished = false;
    bool succeeded = true;
    unsigned low_completions = 0;
    std::function<void(bool)> refill;
    refill = [&](bool success)
    {
        succeeded = success && succeeded;
        ++low_completions;
        if (!high_finished)
        {
            succeeded = fixture.destruction->request_damage(*low, {}, 1, 0, 0, refill) && succeeded;
        }
    };
    if (!fixture.destruction->request_damage(*low, {}, 1, 0, 0, refill) ||
        !fixture.destruction->request_damage(*low, {}, 1, 0, 0, refill) ||
        !fixture.destruction->request_damage(*high, {}, 1, 0, 0,
                                             [&](bool success)
                                             {
                                                 succeeded = success && succeeded;
                                                 high_finished = true;
                                             }) ||
        !fixture.destruction->synchronize())
    {
        return false;
    }
    for (unsigned frame = 0; frame < 4 && !high_finished; ++frame)
    {
        if (!tasks.wait() || !fixture.destruction->synchronize())
        {
            return false;
        }
    }
    const bool passed = expect(high_finished && succeeded && low_completions > 0,
                               "higher node ID finishes despite continuous lower-ID damage");
    fixture.world.shutdown();
    return tasks.wait() && passed;
}

bool persistent_destruction_contract()
{
    ThreadSystem threads({.worker_limit = 1});
    TaskSystem tasks(threads, {.name = "Persistent destruction", .max_concurrency = 1});
    Fixture fixture;
    if (!threads.initialize({}) || !tasks.initialize({}) || !fixture.initialize(&tasks))
    {
        return false;
    }
    auto* scene = fixture.world.create_scene("Retained destruction");
    auto* owner = scene ? scene->create_node("Source") : nullptr;
    auto* component = owner ? owner->add_component<DestructionComponent>() : nullptr;
    schema::DestructionDataT settings;
    settings.destruction_guid = asset_guid;
    if (!component || !owner->set_persistent(true) || !component->set_settings(settings) ||
        !fixture.destruction->synchronize())
    {
        return false;
    }
    // Generated roots have their own lifetime. Explicitly retain them for this pending request.
    auto fragments = generated_ids(*scene);
    for (const auto id : fragments)
    {
        if (!scene->find_node(id)->set_persistent(true))
        {
            return false;
        }
    }
    bool completed = false;
    bool success = false;
    const auto revision = component->revision();
    if (!fixture.destruction->request_damage(*component, {0.5, 0, 0}, 0.1F, 0.25F, 0,
                                             [&](bool result)
                                             {
                                                 completed = true;
                                                 success = result;
                                             }) ||
        !fixture.destruction->synchronize())
    {
        return false;
    }
    fixture.world.unload_scene();
    if (!fixture.world.create_scene("Next destruction level") || !tasks.wait() ||
        !expect(
            fixture.destruction->synchronize() && completed && success &&
                component->revision() > revision && generated_ids(*scene) == fragments,
            "pending damage publishes into its retained scene and preserves fragment identities"))
    {
        return false;
    }
    for (const auto id : fragments)
    {
        if (!scene->find_node(id)->set_persistent(false))
        {
            return false;
        }
    }
    const auto damaged_revision = component->revision();
    fixture.world.unload_scene();
    if (!expect(fixture.destruction->synchronize() && fixture.destruction->synchronize() &&
                    !component->ready() && component->revision() == damaged_revision &&
                    !component->last_error().empty() && generated_ids(*scene).empty() &&
                    fixture.physics.tick(0) && fixture.physics.body_count() == 0,
                "unloaded ordinary generated roots invalidate the assembly once without failing "
                "the host"))
    {
        return false;
    }
    return expect(component->reset() && component->ready() && !generated_ids(*scene).empty() &&
                      component->last_error().empty(),
                  "explicit reset recreates the retained source assembly after its outputs unload");
}

} // namespace

int main()
{
    return persistent_destruction_contract() && runtime_contract() && incremental_contract() &&
                   support_contract() && stress_contract() && asynchronous_damage_contract() &&
                   asynchronous_damage_fairness(1) && asynchronous_damage_fairness(2)
               ? 0
               : 1;
}
