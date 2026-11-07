#include "scene_snapshot.hpp"

#include "animation_fixture.hpp"

#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"

namespace
{
using namespace test_support::animation;

bool sampling_and_controls()
{
    Fixture fixture;
    if (!expect(fixture.initialize(), "initialize real ozz sampling world"))
    {
        return false;
    }
    auto node = fixture.create();
    if (!expect(node.valid(), "attach renderer and animator to an ordinary node"))
    {
        return false;
    }
    auto animator = node.get_component<engine::Animator>();
    auto renderer = node.get_component<engine::MeshRenderer>();
    const auto bind = renderer.mesh_data();
    engine::schema::ComponentDataT saved;
    if (!expect(animator.build_data(saved) && fixture.animation.tick(0.25), "auto-play samples") ||
        !expect(animator.playback() == engine::AnimationPlayback::playing &&
                    close(animator.time(), 0.25) && close(sampled_vertex(renderer).x(), 1),
                "source indices map to target hierarchy and ozz DFS order") ||
        !expect(renderer.mesh_data() == bind && animator.data() == saved,
                "sampling does not mutate persisted component or bind mesh") ||
        !expect(animator.pause() && fixture.animation.tick(0.25) && close(animator.time(), 0.25) &&
                    close(sampled_vertex(renderer).x(), 1),
                "pause retains last pose and time") ||
        !expect(animator.seek(0.75) && fixture.animation.tick(0) &&
                    close(sampled_vertex(renderer).x(), 3),
                "seek samples requested seconds without time advance") ||
        !expect(animator.play() && fixture.animation.tick(0.5) && close(animator.time(), 0.25),
                "resume and loop wrap preserve the remainder") ||
        !expect(animator.stop() && renderer.skinning_palette().empty() && animator.time() == 0,
                "stop restores bind mesh immediately") ||
        !expect(fixture.animation.tick(0.2) && renderer.skinning_palette().empty(),
                "explicit stop prevents auto-play from starting again"))
    {
        return false;
    }
    auto settings = animator.settings();
    settings.loop = false;
    settings.speed = 2;
    if (!expect(animator.set_settings(settings) && animator.play() &&
                    fixture.animation.tick(0.25) && close(animator.time(), 0.5) &&
                    close(sampled_vertex(renderer).x(), 2),
                "speed multiplies elapsed time") ||
        !expect(fixture.animation.tick(1) && animator.time() == 1 &&
                    animator.playback() == engine::AnimationPlayback::paused &&
                    close(sampled_vertex(renderer).x(), 4),
                "nonloop playback holds its final frame") ||
        !expect(animator.stop() && animator.seek(0.5) && fixture.animation.tick(0) &&
                    close(sampled_vertex(renderer).x(), 2),
                "seek from stopped enters a retained paused pose"))
    {
        return false;
    }
    settings.speed = 0;
    if (!expect(animator.set_settings(settings) && animator.play() && fixture.animation.tick(100) &&
                    close(animator.time(), 0.5),
                "zero speed freezes animation time"))
    {
        return false;
    }
    const auto valid = animator.settings();
    settings.speed = std::numeric_limits<float>::quiet_NaN();
    return expect(!animator.set_settings(settings) && animator.settings() == valid &&
                      !animator.set_animation("missing.animation") &&
                      !animator.set_animation("builtin://meshes/cube") &&
                      animator.animation_path() == clip_path && !animator.seek(-1) &&
                      !animator.seek(2) && !animator.seek(std::numeric_limits<double>::infinity()),
                  "invalid settings, types and seek values preserve live state");
}

bool persistent_animation()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto node = fixture.create();
    auto ordinary = fixture.create();
    if (!expect(node && ordinary && node.set_persistent(true) && fixture.animation.tick(0.25),
                "prepare retained and ordinary animated nodes"))
    {
        return false;
    }
    const auto ordinary_id = ordinary.id();
    auto animator = node.get_component<engine::Animator>();
    auto renderer = node.get_component<engine::MeshRenderer>();
    const auto revision = renderer.deformation_revision();
    if (!expect(animator.pause(), "pause retained pose before unloading"))
    {
        return false;
    }
    fixture.world.unload_scene();
    if (!expect(fixture.world.scene() == nullptr && fixture.world.find_node(node.id()) == node &&
                    fixture.world.find_node(ordinary_id).valid() == false &&
                    fixture.animation.tick(0.25) && close(animator.time(), 0.25) &&
                    renderer.deformation_revision() == revision,
                "unload preserves the paused palette without rebuilding it"))
    {
        return false;
    }
    if (!expect(fixture.world.create_scene("Next animation scene") && fixture.create() &&
                    animator.play() && fixture.animation.tick(0.25) &&
                    close(animator.time(), 0.5) && close(sampled_vertex(renderer).x(), 2),
                "retained animator and new scene both advance") ||
        !expect(node.scene()->destroy_node(node.id()) && fixture.animation.tick(0),
                "explicit retained node deletion retires its animation state"))
    {
        return false;
    }
    fixture.world.destroy_scene();
    return expect(fixture.animation.tick(0), "full stop retires every animation container");
}

bool skinning_and_lifecycle()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto node = fixture.create(false);
    if (!node)
    {
        return false;
    }
    auto animator = node.get_component<engine::Animator>();
    auto renderer = node.get_component<engine::MeshRenderer>();
    if (!expect(fixture.animation.tick(0) && renderer.skinning_palette().empty(),
                "auto-play disabled leaves original vertices") ||
        !expect(animator.set_animation(scale_path) && animator.seek(0.5) &&
                    fixture.animation.tick(0),
                "prepare independent scale action"))
    {
        return false;
    }
    const auto& normal = sampled_vertex(renderer);
    const auto& tangent = sampled_tangent(renderer);
    if (!expect(close(normal.nx(), 1.0 / std::sqrt(5.0)) &&
                    close(normal.ny(), 2.0 / std::sqrt(5.0)) &&
                    close(tangent.x(), 2.0 / std::sqrt(5.0)) &&
                    close(tangent.y(), -1.0 / std::sqrt(5.0)) && close(normal.x(), -1),
                "mesh bind transform, normal inverse transpose and tangent linear transform") ||
        !expect(animator.set_animation(reflected_path) && animator.seek(0.5) &&
                    fixture.animation.tick(0) && sampled_tangent(renderer).w() == -1,
                "mirrored skin preserves the correct tangent handedness") ||
        !expect(animator.set_animation(clip_path) && renderer.set_mesh_data(mesh(3)) &&
                    animator.seek(0.5) && fixture.animation.tick(0) &&
                    close(sampled_vertex(renderer).x(), 0) &&
                    close(sampled_vertex(renderer).y(), 2),
                "child track and missing parent track use the target bind pose"))
    {
        return false;
    }
    if (!animator.set_enabled(false))
    {
        std::abort();
    }
    if (!expect(fixture.animation.tick(0) && renderer.skinning_palette().empty(),
                "disabled animator clears GPU skinning palette"))
    {
        return false;
    }
    if (!animator.set_enabled(true))
    {
        std::abort();
    }
    if (!expect(fixture.animation.tick(0) && close(sampled_vertex(renderer).y(), 2),
                "re-enabled paused animator rebuilds and restores its pose"))
    {
        return false;
    }
    auto transform = node.get_component<engine::Transform>();
    if (!transform.set_enabled(false))
    {
        std::abort();
    }
    if (!expect(fixture.animation.tick(0) && renderer.skinning_palette().empty(),
                "disabled transform suppresses skinning"))
    {
        return false;
    }
    if (!transform.set_enabled(true))
    {
        std::abort();
    }
    fixture.animation.clear();
    fixture.animation.clear();
    if (!expect(fixture.animation.tick(0) && !renderer.skinning_palette().empty(),
                "idempotent clear releases cache and reconstructs active pose") ||
        !expect(node.remove_component(engine::MeshRenderer::component_type), "remove renderer"))
    {
        return false;
    }
    renderer = node.add_component<engine::MeshRenderer>();
    if (!expect(renderer && renderer.set_mesh_data(mesh()) && fixture.animation.tick(0) &&
                    close(sampled_vertex(renderer).x(), 2) &&
                    close(sampled_vertex(renderer).y(), 0),
                "replacement renderer never reuses old skin weights") ||
        !expect(node.remove_component(engine::Animator::component_type) &&
                    fixture.animation.tick(0) && renderer.skinning_palette().empty(),
                "removed animator restores unmodified source mesh") ||
        !expect(fixture.world.scene()->destroy_node(node.id()) && fixture.animation.tick(0),
                "deleted node has no retained native pointer"))
    {
        return false;
    }
    fixture.world.destroy_scene();
    if (!expect(fixture.animation.tick(0) && fixture.world.create_scene("Replacement") &&
                    fixture.create() && fixture.animation.tick(0.25),
                "destroyed and replacement scenes synchronize independently"))
    {
        return false;
    }
    fixture.animation.shutdown();
    fixture.animation.shutdown();
    return expect(test_support::scene_nodes(*fixture.world.scene())[0]
                      .get_component<engine::MeshRenderer>()
                      .deformed_vertices()
                      .empty(),
                  "shutdown clears skinning streams");
}

bool blending_and_instances()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto first = fixture.create(false);
    auto second = fixture.create(false);
    if (!first.valid() || !second.valid())
    {
        return false;
    }
    auto animator = first.get_component<engine::Animator>();
    auto other = second.get_component<engine::Animator>();
    auto renderer = first.get_component<engine::MeshRenderer>();
    auto other_renderer = second.get_component<engine::MeshRenderer>();
    if (!expect(animator.seek(0.5) && other.seek(0.25) && fixture.animation.tick(0) &&
                    renderer.skinning_palette().size() == 4 &&
                    renderer.deformed_vertices().empty() &&
                    close(sampled_vertex(renderer).x(), 2) &&
                    close(sampled_vertex(other_renderer).x(), 1),
                "shared actions retain independent instance time and produce only bone palettes") ||
        !expect(animator.cross_fade(scale_path, 1) && fixture.animation.tick(0.25) &&
                    close(sampled_vertex(renderer).x(), 1.25) &&
                    close(sampled_vertex(other_renderer).x(), 1),
                "two sampled local poses blend without changing another instance") ||
        !expect(animator.pause() && fixture.animation.tick(1) && close(animator.time(), 0.25) &&
                    close(sampled_vertex(renderer).x(), 1.25),
                "pause freezes clip clocks and blend progress") ||
        !expect(animator.cross_fade(clip_path, 1) && fixture.animation.tick(0) &&
                    close(sampled_vertex(renderer).x(), 1.25) && fixture.animation.tick(0.5) &&
                    close(sampled_vertex(renderer).x(), 1.625),
                "interrupting a fade starts at the displayed blended pose without a snap"))
    {
        return false;
    }
    if (!animator.set_enabled(false))
    {
        std::abort();
    }
    if (!expect(fixture.animation.tick(0) && renderer.skinning_palette().empty(),
                "disabled transition releases its GPU output"))
    {
        return false;
    }
    if (!animator.set_enabled(true))
    {
        std::abort();
    }
    if (!expect(animator.pause() && fixture.animation.tick(0) &&
                    close(sampled_vertex(renderer).x(), 1.625),
                "reenabling an interrupted fade retains its frozen source pose") ||
        !expect(animator.set_animation(clip_path) && fixture.animation.tick(0) &&
                    close(animator.time(), 0.5) && close(sampled_vertex(renderer).x(), 2),
                "explicit same-path SetAnimation cancels fading without restarting the clock"))
    {
        return false;
    }
    const auto settings = animator.settings();
    const auto time = animator.time();
    const auto palette = renderer.skinning_palette()[2];
    if (!expect(!animator.cross_fade("", 1) && !animator.cross_fade(scale_path, -1) &&
                    !animator.cross_fade(scale_path, std::numeric_limits<double>::infinity()) &&
                    !animator.cross_fade("missing.animation", 1) &&
                    !animator.cross_fade("builtin://meshes/cube", 1) &&
                    animator.settings() == settings && animator.time() == time &&
                    renderer.skinning_palette()[2] == palette,
                "rejected transitions preserve settings, time and the displayed pose") ||
        !expect(animator.stop() && renderer.skinning_palette().empty() &&
                    animator.cross_fade(scale_path, 1) && fixture.animation.tick(0.5) &&
                    close(sampled_vertex(renderer).x(), -0.5),
                "a fade from stopped starts from the bind pose") ||
        !expect(animator.seek(0.5) && fixture.animation.tick(0) &&
                    close(sampled_vertex(renderer).x(), -1),
                "seek cancels blending and samples the destination action directly"))
    {
        return false;
    }
    // 同 GUID 新资源代际与旧场景引用共存，不能命中旧编译动作。
    auto replacement = clip(clip_path, false, false);
    replacement.tracks[0]->translations[1] = {1, {11, 0, 0}};
    flatbuffers::FlatBufferBuilder builder;
    engine::schema::FinishAnimationDataBuffer(
        builder, engine::schema::AnimationData::Pack(builder, &replacement));
    std::erase_if(fixture.files.catalog.entries,
                  [](const auto& entry) { return entry->guid == clip_guid; });
    if (!add(fixture.files, clip_guid, clip_path, engine::schema::AssetKind::animation, builder,
             {source_guid}) ||
        !fixture.services.publish_and_load(fixture.files.catalog,
                                           {{"content", fixture.files.root}}))
    {
        return false;
    }
    return expect(animator.cross_fade(clip_path, 0) && animator.seek(0.5) &&
                      fixture.animation.tick(0) && close(sampled_vertex(renderer).x(), 4) &&
                      close(sampled_vertex(other_renderer).x(), 1) && close(other.time(), 0.25),
                  "resource generations and per-instance sampling state never share mutable data");
}
bool world_pause_preserves_individual_playback()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto playing = fixture.create();
    auto paused = fixture.create();
    if (!playing || !paused || !fixture.animation.tick(0.25))
    {
        return false;
    }
    auto active = playing.get_component<engine::Animator>();
    auto individual = paused.get_component<engine::Animator>();
    auto renderer = playing.get_component<engine::MeshRenderer>();
    const auto pose = sampled_vertex(renderer);
    if (!expect(individual.pause() && fixture.world.set_paused(true) &&
                    fixture.animation.tick(120) && close(active.time(), 0.25) &&
                    close(individual.time(), 0.25) &&
                    close(sampled_vertex(renderer).x(), pose.x()) &&
                    active.playback() == engine::AnimationPlayback::playing,
                "world pause retains sampled pose without rewriting individual playback"))
    {
        return false;
    }
    return expect(
        fixture.world.set_paused(false) && fixture.animation.tick(0.25) &&
            close(active.time(), 0.5) && close(individual.time(), 0.25) &&
            individual.playback() == engine::AnimationPlayback::paused,
        "world resume advances only fresh time and preserves an individually paused action");
}

using Palettes = std::vector<std::vector<std::array<float, 16>>>;

bool collect_sampling_results(engine::TaskSystem* tasks, Palettes& palettes,
                              engine::ThreadSystem* threads = nullptr)
{
    // 非整批数量覆盖尾批；每实例使用不同时间与状态，暴露错误共享采样上下文。
    Fixture fixture(tasks, {.min_parallel_count = 2, .batch_size = 4}, threads);
    if (!expect(fixture.initialize(), "initialize sampling comparison fixture"))
    {
        return false;
    }
    std::vector<engine::Node> nodes;
    for (std::size_t index = 0; index < 19; ++index)
    {
        auto node = fixture.create(false);
        auto animator = node.valid() ? node.get_component<engine::Animator>() : engine::Animator{};
        if (!expect(animator.valid() && animator.seek(static_cast<double>(index) / 20.0) &&
                        (index % 3 == 0 || animator.play()),
                    "configure independent comparison animation"))
        {
            return false;
        }
        nodes.push_back(node);
    }
    for (std::size_t frame = 0; frame < 3; ++frame)
    {
        if (frame == 1)
        {
            for (std::size_t index = 0; index < nodes.size(); ++index)
            {
                auto animator = nodes[index].get_component<engine::Animator>();
                if (index % 2 == 0 &&
                    !expect(animator.cross_fade(scale_path, 0.5), "start comparison blend"))
                {
                    return false;
                }
            }
        }
        if (!expect(fixture.animation.tick(0.125), "sample comparison frame"))
        {
            return false;
        }
        for (auto node : nodes)
        {
            auto renderer = node.get_component<engine::MeshRenderer>();
            palettes.emplace_back(renderer.skinning_palette().begin(),
                                  renderer.skinning_palette().end());
            palettes.emplace_back(renderer.skinning_normal_palette().begin(),
                                  renderer.skinning_normal_palette().end());
        }
    }
    // 已经 join 的批次不能保留旧场景节点或条目。
    fixture.world.destroy_scene();
    return expect(fixture.world.create_scene("Replacement") && fixture.create().valid() &&
                      fixture.animation.tick(0.25),
                  "replace comparison scene after joined animation tasks");
}

bool parallel_sampling_matches_serial()
{
    Palettes serial;
    if (!collect_sampling_results(nullptr, serial))
    {
        return false;
    }
    const engine::EngineConfig config;
    engine::ThreadSystem threads({.worker_limit = 2});
    engine::TaskSystem tasks(threads, {.name = "Animation test", .max_concurrency = 2});
    if (!expect(threads.initialize(config) && tasks.initialize(config),
                "initialize animation comparison task domain"))
    {
        return false;
    }
    Palettes parallel;
    if (!collect_sampling_results(&tasks, parallel, &threads) ||
        !expect(parallel.size() == serial.size(), "parallel animation publishes every palette"))
    {
        return false;
    }
    for (std::size_t pose = 0; pose < serial.size(); ++pose)
    {
        if (!expect(serial[pose].size() == parallel[pose].size(), "palette sizes are identical"))
        {
            return false;
        }
        for (std::size_t joint = 0; joint < serial[pose].size(); ++joint)
        {
            for (std::size_t element = 0; element < 16; ++element)
            {
                if (!expect(close(serial[pose][joint][element], parallel[pose][joint][element]),
                            "parallel sampling, blending and normal matrices match serial"))
                {
                    return false;
                }
            }
        }
    }
    tasks.shutdown();
    engine::TaskSystem limited(
        threads, {.name = "Limited animation test", .max_concurrency = 2, .max_pending = 1});
    if (!limited.initialize(config))
    {
        return false;
    }
    std::promise<void> release;
    const auto gate = release.get_future().share();
    auto busy = limited.submit("Occupy admission",
                               [gate]
                               {
                                   gate.wait();
                                   return true;
                               });
    if (!busy.valid())
    {
        return false;
    }
    Palettes fallback;
    const bool sampled = collect_sampling_results(&limited, fallback, &threads);
    release.set_value();
    return expect(busy.get() && sampled && fallback == serial,
                  "saturated task domain falls back without dropping or duplicating poses");
}

} // namespace

int main()
{
    return persistent_animation() && parallel_sampling_matches_serial() &&
                   world_pause_preserves_individual_playback() && sampling_and_controls() &&
                   skinning_and_lifecycle() && blending_and_instances()
               ? 0
               : 1;
}
