#include "animation_fixture.hpp"

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
    auto* node = fixture.create();
    if (!expect(node != nullptr, "attach renderer and animator to an ordinary node"))
    {
        return false;
    }
    auto& animator = *node->get_component<engine::Animator>();
    auto& renderer = *node->get_component<engine::MeshRenderer>();
    const auto bind = *renderer.mesh_data();
    engine::schema::ComponentDataT saved;
    if (!expect(animator.build_data(saved) && fixture.animation.tick(0.25), "auto-play samples") ||
        !expect(animator.playback() == engine::AnimationPlayback::playing &&
                    close(animator.time(), 0.25) && close(sampled_vertex(renderer).x(), 1),
                "source indices map to target hierarchy and ozz DFS order") ||
        !expect(*renderer.mesh_data() == bind && animator.data() == saved,
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
                      !animator.set_animation("project/missing.animation") &&
                      !animator.set_animation("builtin/meshes/cube") &&
                      animator.animation_path() == clip_path && !animator.seek(-1) &&
                      !animator.seek(2) && !animator.seek(std::numeric_limits<double>::infinity()),
                  "invalid settings, types and seek values preserve live state");
}

bool skinning_and_lifecycle()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    auto* node = fixture.create(false);
    if (!node)
    {
        return false;
    }
    auto* animator = node->get_component<engine::Animator>();
    auto* renderer = node->get_component<engine::MeshRenderer>();
    if (!expect(fixture.animation.tick(0) && renderer->skinning_palette().empty(),
                "auto-play disabled leaves original vertices") ||
        !expect(animator->set_animation(scale_path) && animator->seek(0.5) &&
                    fixture.animation.tick(0),
                "prepare independent scale action"))
    {
        return false;
    }
    const auto& normal = sampled_vertex(*renderer);
    const auto& tangent = sampled_tangent(*renderer);
    if (!expect(close(normal.nx(), 1.0 / std::sqrt(5.0)) &&
                    close(normal.ny(), 2.0 / std::sqrt(5.0)) &&
                    close(tangent.x(), 2.0 / std::sqrt(5.0)) &&
                    close(tangent.y(), -1.0 / std::sqrt(5.0)) && close(normal.x(), -1),
                "mesh bind transform, normal inverse transpose and tangent linear transform") ||
        !expect(animator->set_animation(reflected_path) && animator->seek(0.5) &&
                    fixture.animation.tick(0) && sampled_tangent(*renderer).w() == -1,
                "mirrored skin preserves the correct tangent handedness") ||
        !expect(animator->set_animation(clip_path) && renderer->set_mesh_data(mesh(3)) &&
                    animator->seek(0.5) && fixture.animation.tick(0) &&
                    close(sampled_vertex(*renderer).x(), 0) &&
                    close(sampled_vertex(*renderer).y(), 2),
                "child track and missing parent track use the target bind pose"))
    {
        return false;
    }
    animator->set_enabled(false);
    if (!expect(fixture.animation.tick(0) && renderer->skinning_palette().empty(),
                "disabled animator clears GPU skinning palette"))
    {
        return false;
    }
    animator->set_enabled(true);
    if (!expect(fixture.animation.tick(0) && close(sampled_vertex(*renderer).y(), 2),
                "re-enabled paused animator rebuilds and restores its pose"))
    {
        return false;
    }
    auto* transform = node->get_component<engine::Transform>();
    transform->set_enabled(false);
    if (!expect(fixture.animation.tick(0) && renderer->skinning_palette().empty(),
                "disabled transform suppresses skinning"))
    {
        return false;
    }
    transform->set_enabled(true);
    fixture.animation.clear();
    fixture.animation.clear();
    if (!expect(fixture.animation.tick(0) && !renderer->skinning_palette().empty(),
                "idempotent clear releases cache and reconstructs active pose") ||
        !expect(node->remove_component(engine::MeshRenderer::component_type), "remove renderer"))
    {
        return false;
    }
    renderer = node->add_component<engine::MeshRenderer>();
    if (!expect(renderer && renderer->set_mesh_data(mesh()) && fixture.animation.tick(0) &&
                    close(sampled_vertex(*renderer).x(), 2) &&
                    close(sampled_vertex(*renderer).y(), 0),
                "replacement renderer never reuses old skin weights") ||
        !expect(node->remove_component(engine::Animator::component_type) &&
                    fixture.animation.tick(0) && renderer->skinning_palette().empty(),
                "removed animator restores unmodified source mesh") ||
        !expect(fixture.world.scene()->destroy_node(node->id()) && fixture.animation.tick(0),
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
    return expect(fixture.world.scene()
                      ->nodes()[0]
                      ->get_component<engine::MeshRenderer>()
                      ->deformed_vertices()
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
    auto* first = fixture.create(false);
    auto* second = fixture.create(false);
    if (first == nullptr || second == nullptr)
    {
        return false;
    }
    auto& animator = *first->get_component<engine::Animator>();
    auto& other = *second->get_component<engine::Animator>();
    auto& renderer = *first->get_component<engine::MeshRenderer>();
    auto& other_renderer = *second->get_component<engine::MeshRenderer>();
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
    animator.set_enabled(false);
    if (!expect(fixture.animation.tick(0) && renderer.skinning_palette().empty(),
                "disabled transition releases its GPU output"))
    {
        return false;
    }
    animator.set_enabled(true);
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
                    !animator.cross_fade("project/missing.animation", 1) &&
                    !animator.cross_fade("builtin/meshes/cube", 1) &&
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
        !fixture.files.publish(fixture.resources))
    {
        return false;
    }
    return expect(animator.cross_fade(clip_path, 0) && animator.seek(0.5) &&
                      fixture.animation.tick(0) && close(sampled_vertex(renderer).x(), 4) &&
                      close(sampled_vertex(other_renderer).x(), 1) && close(other.time(), 0.25),
                  "resource generations and per-instance sampling state never share mutable data");
}
} // namespace

int main()
{
    return sampling_and_controls() && skinning_and_lifecycle() && blending_and_instances() ? 0 : 1;
}
