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
                    close(animator.time(), 0.25) && close(renderer.deformed_vertices()[0].x(), 1),
                "source indices map to target hierarchy and ozz DFS order") ||
        !expect(*renderer.mesh_data() == bind && animator.data() == saved,
                "sampling does not mutate persisted component or bind mesh") ||
        !expect(animator.pause() && fixture.animation.tick(0.25) && close(animator.time(), 0.25) &&
                    close(renderer.deformed_vertices()[0].x(), 1),
                "pause retains last pose and time") ||
        !expect(animator.seek(0.75) && fixture.animation.tick(0) &&
                    close(renderer.deformed_vertices()[0].x(), 3),
                "seek samples requested seconds without time advance") ||
        !expect(animator.play() && fixture.animation.tick(0.5) && close(animator.time(), 0.25),
                "resume and loop wrap preserve the remainder") ||
        !expect(animator.stop() && renderer.deformed_vertices().empty() && animator.time() == 0,
                "stop restores bind mesh immediately") ||
        !expect(fixture.animation.tick(0.2) && renderer.deformed_vertices().empty(),
                "explicit stop prevents auto-play from starting again"))
    {
        return false;
    }
    auto settings = animator.settings();
    settings.loop = false;
    settings.speed = 2;
    if (!expect(animator.set_settings(settings) && animator.play() &&
                    fixture.animation.tick(0.25) && close(animator.time(), 0.5) &&
                    close(renderer.deformed_vertices()[0].x(), 2),
                "speed multiplies elapsed time") ||
        !expect(fixture.animation.tick(1) && animator.time() == 1 &&
                    animator.playback() == engine::AnimationPlayback::paused &&
                    close(renderer.deformed_vertices()[0].x(), 4),
                "nonloop playback holds its final frame") ||
        !expect(animator.stop() && animator.seek(0.5) && fixture.animation.tick(0) &&
                    close(renderer.deformed_vertices()[0].x(), 2),
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
    if (!expect(fixture.animation.tick(0) && renderer->deformed_vertices().empty(),
                "auto-play disabled leaves original vertices") ||
        !expect(animator->set_animation(scale_path) && animator->seek(0.5) &&
                    fixture.animation.tick(0),
                "prepare independent scale action"))
    {
        return false;
    }
    const auto& normal = renderer->deformed_vertices()[0];
    const auto& tangent = renderer->deformed_tangents()[0];
    if (!expect(close(normal.nx(), 1.0 / std::sqrt(5.0)) &&
                    close(normal.ny(), 2.0 / std::sqrt(5.0)) &&
                    close(tangent.x(), 2.0 / std::sqrt(5.0)) &&
                    close(tangent.y(), -1.0 / std::sqrt(5.0)) && close(normal.x(), -1),
                "mesh bind transform, normal inverse transpose and tangent linear transform") ||
        !expect(animator->set_animation(reflected_path) && animator->seek(0.5) &&
                    fixture.animation.tick(0) && renderer->deformed_tangents()[0].w() == -1,
                "mirrored skin preserves the correct tangent handedness") ||
        !expect(animator->set_animation(clip_path) && renderer->set_mesh_data(mesh(3)) &&
                    animator->seek(0.5) && fixture.animation.tick(0) &&
                    close(renderer->deformed_vertices()[0].x(), 0) &&
                    close(renderer->deformed_vertices()[0].y(), 2),
                "child track and missing parent track use the target bind pose"))
    {
        return false;
    }
    animator->set_enabled(false);
    if (!expect(fixture.animation.tick(0) && renderer->deformed_vertices().empty(),
                "disabled animator clears CPU deformation"))
    {
        return false;
    }
    animator->set_enabled(true);
    if (!expect(fixture.animation.tick(0) && close(renderer->deformed_vertices()[0].y(), 2),
                "re-enabled paused animator rebuilds and restores its pose"))
    {
        return false;
    }
    auto* transform = node->get_component<engine::Transform>();
    transform->set_enabled(false);
    if (!expect(fixture.animation.tick(0) && renderer->deformed_vertices().empty(),
                "disabled transform suppresses skinning"))
    {
        return false;
    }
    transform->set_enabled(true);
    fixture.animation.clear();
    fixture.animation.clear();
    if (!expect(fixture.animation.tick(0) && !renderer->deformed_vertices().empty(),
                "idempotent clear releases cache and reconstructs active pose") ||
        !expect(node->remove_component(engine::MeshRenderer::component_type), "remove renderer"))
    {
        return false;
    }
    renderer = node->add_component<engine::MeshRenderer>();
    if (!expect(renderer && renderer->set_mesh_data(mesh()) && fixture.animation.tick(0) &&
                    close(renderer->deformed_vertices()[0].x(), 2) &&
                    close(renderer->deformed_vertices()[0].y(), 0),
                "replacement renderer never reuses old skin weights") ||
        !expect(node->remove_component(engine::Animator::component_type) &&
                    fixture.animation.tick(0) && renderer->deformed_vertices().empty(),
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
} // namespace

int main()
{
    return sampling_and_controls() && skinning_and_lifecycle() ? 0 : 1;
}
