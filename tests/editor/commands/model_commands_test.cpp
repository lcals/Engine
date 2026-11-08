#include "animation_fixture.hpp"
#include "scene_snapshot.hpp"

#include "editor/commands/editor_commands.hpp"
#include "editor/commands/scene_commands.hpp"

#include <nlohmann/json.hpp>

namespace
{
using Json = nlohmann::json;
using namespace test_support::animation;

bool model_commands()
{
    Fixture fixture;
    if (!fixture.initialize() || !add_model(fixture.files) ||
        !(fixture.files.publish(fixture.resources) && fixture.services.prepare_builtins()))
    {
        return false;
    }
    engine::editor::SceneCommands module(fixture.world);
    engine::editor::EditorCommands editor([&fixture]() { return fixture.world.scene()->id(); });
    if (!module.register_commands(editor))
    {
        return false;
    }
    Json result;
    std::string error;
    const auto run = [&editor, &result, &error](const char* name, const Json& arguments)
    {
        const bool success = editor.execute(name, arguments, result, error);
        if (!success)
        {
            std::fprintf(stderr, "%s: %s\n", name, error.c_str());
        }
        return success;
    };
    auto& scene = *fixture.world.scene();
    const Json identity = {{"sceneId", std::to_string(scene.id())}};
    auto request = identity;
    request.update({{"path", model_path}, {"name", "Character"}});
    if (!expect(run("instantiate_model", request) && test_support::scene_nodes(scene).size() == 3,
                "model command builds the complete hierarchy") ||
        !expect(!run("instantiate_model", request) &&
                    test_support::scene_nodes(scene).size() == 3 && editor.undo_count() == 1,
                "duplicate root names preserve scene and history"))
    {
        return false;
    }
    auto body = test_support::scene_nodes(scene)[2];
    auto set = identity;
    set["nodeId"] = std::to_string(body.id());
    set["animation"] = clip_path;
    set["playOnAwake"] = false;
    if (!expect(run("set_animator", set) && run("undo", identity) && run("undo", identity) &&
                    test_support::scene_nodes(scene).empty() && run("redo", identity) &&
                    run("redo", identity) && test_support::scene_nodes(scene).size() == 3 &&
                    test_support::scene_nodes(scene)[2].get_component<engine::Animator>(),
                "model redo updates child identities used by later component edits"))
    {
        return false;
    }
    if (!run("undo", identity))
    {
        return false;
    }
    auto extra = scene.create_node("Unrelated child");
    if (!expect(extra && extra.set_parent(test_support::scene_nodes(scene)[0]) &&
                    !run("undo", identity) && test_support::scene_nodes(scene).size() == 4 &&
                    editor.undo_count() == 1,
                "undo never deletes children attached by another operation") ||
        !expect(scene.destroy_node(extra.id()) && run("undo", identity) &&
                    test_support::scene_nodes(scene).empty(),
                "removing unrelated edits restores valid model undo"))
    {
        return false;
    }
    auto* entry = fixture.files.catalog.entries.back().get();
    entry->content_hash = "0000000000000000";
    if (!expect(fixture.files.publish(fixture.resources) && !run("redo", identity) &&
                    test_support::scene_nodes(scene).empty() && editor.redo_count() >= 1,
                "redo rejects changed model resource versions without creating nodes"))
    {
        return false;
    }
    request["sceneId"] = "0";
    return expect(!run("instantiate_model", request), "stale scene identity is rejected");
}
} // namespace

int main()
{
    return model_commands() ? 0 : 1;
}
