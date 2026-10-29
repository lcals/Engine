#include "animation_fixture.hpp"

#include "editor/commands/editor_commands.hpp"
#include "editor/commands/scene_commands.hpp"

#include <nlohmann/json.hpp>

namespace
{
using Json = nlohmann::json;
using namespace test_support::animation;

bool commands()
{
    Fixture fixture;
    if (!fixture.initialize())
    {
        return false;
    }
    engine::editor::SceneCommands module(fixture.world);
    engine::editor::EditorCommands editor([&fixture]() { return fixture.world.scene()->id(); });
    auto* node = fixture.world.scene()->create_node("Command animation");
    auto* renderer = node ? node->add_component<engine::MeshRenderer>() : nullptr;
    if (!expect(renderer && renderer->set_mesh_data(mesh()) && module.register_commands(editor),
                "register animation typed commands"))
    {
        return false;
    }
    Json result;
    std::string error;
    const auto run = [&editor, &result, &error](const char* name, const Json& arguments)
    { return editor.execute(name, arguments, result, error); };
    const Json target = {{"sceneId", std::to_string(node->scene().id())},
                         {"nodeId", std::to_string(node->id())}};
    const Json scene = {{"sceneId", std::to_string(node->scene().id())}};
    auto settings = target;
    settings.update(
        {{"animation", clip_path}, {"loop", false}, {"speed", 2}, {"playOnAwake", false}});
    if (!expect(run("set_animator", settings) && run("get_animator", target) &&
                    result["animation"] == clip_path && result["speed"] == 2 &&
                    result["playOnAwake"] == false,
                "set/get Animator uses public resource path and generated settings") ||
        !expect(run("undo", scene) && !node->get_component<engine::Animator>() &&
                    run("redo", scene),
                "component addition is undoable and redo restores persisted data"))
    {
        return false;
    }
    auto* animator = node->get_component<engine::Animator>();
    const auto before = animator->data();
    const auto undo_count = editor.undo_count();
    for (const Json& invalid :
         {Json{{"speed", -1}}, Json{{"speed", 17}}, Json{{"speed", "fast"}}, Json{{"loop", 1}},
          Json{{"playOnAwake", "true"}}, Json{{"animation", "builtin/meshes/cube"}},
          Json{{"animation", "project/missing.animation"}}, Json{{"unknown", 1}}})
    {
        auto request = target;
        request.update(invalid);
        if (!expect(!run("set_animator", request) && result.is_null() && !error.empty() &&
                        animator->data() == before && editor.undo_count() == undo_count,
                    "invalid edits preserve settings, identity and history"))
        {
            return false;
        }
    }
    if (!expect(run("play_animation", target) && fixture.animation.tick(0.25) &&
                    run("get_animation_state", target) && result["playback"] == "playing" &&
                    close(result["time"].get<double>(), 0.5),
                "play and query report sampled action time") ||
        !expect(run("pause_animation", target) && fixture.animation.tick(1) &&
                    close(animator->time(), 0.5),
                "pause command preserves sampled pose"))
    {
        return false;
    }
    auto seek = target;
    seek["seconds"] = 0.75;
    if (!expect(run("seek_animation", seek) && fixture.animation.tick(0) &&
                    close(renderer->deformed_vertices()[0].x(), 3) &&
                    editor.undo_count() == undo_count,
                "seek affects transient playback without polluting undo history"))
    {
        return false;
    }
    seek["seconds"] = 2;
    if (!expect(!run("seek_animation", seek) && close(animator->time(), 0.75) &&
                    run("stop_animation", target) && renderer->deformed_vertices().empty(),
                "invalid seek preserves pose and stop immediately restores bind mesh"))
    {
        return false;
    }
    auto stale = target;
    stale["sceneId"] = "0";
    return expect(!run("play_animation", stale) && !run("get_animator", stale) &&
                      fixture.world.scene()->destroy_node(node->id()) &&
                      !run("get_animation_state", target),
                  "stale scene and destroyed node identities cannot control animation");
}
} // namespace

int main()
{
    return commands() ? 0 : 1;
}
