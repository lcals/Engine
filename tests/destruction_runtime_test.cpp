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
#include "runtime/world/world_system.hpp"

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <memory>
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
    entry->path = "project/test.destruction";
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

    bool initialize()
    {
        auto owned = std::make_unique<DestructionSystem>(world, physics);
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
    if (!expect(after == pack(*expected) && !component->owns_fragment(supported_nodes.front()),
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

} // namespace

int main()
{
    return runtime_contract() && support_contract() ? 0 : 1;
}
