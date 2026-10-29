#include "animation_fixture.hpp"

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
        !fixture.files.publish(fixture.resources))
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
    { return editor.execute(name, arguments, result, error); };
    auto& scene = *fixture.world.scene();
    const Json identity = {{"sceneId", std::to_string(scene.id())}};
    auto request = identity;
    request.update({{"path", model_path}, {"name", "Character"}});
    if (!expect(run("instantiate_model", request) && scene.nodes().size() == 3,
                "model command builds the complete hierarchy") ||
        !expect(!run("instantiate_model", request) && scene.nodes().size() == 3 &&
                    editor.undo_count() == 1,
                "duplicate root names preserve scene and history"))
    {
        return false;
    }
    auto* body = scene.nodes()[2].get();
    auto set = identity;
    set["nodeId"] = std::to_string(body->id());
    set["animation"] = clip_path;
    set["playOnAwake"] = false;
    if (!expect(run("set_animator", set) && run("undo", identity) && run("undo", identity) &&
                    scene.nodes().empty() && run("redo", identity) && run("redo", identity) &&
                    scene.nodes().size() == 3 &&
                    scene.nodes()[2]->get_component<engine::Animator>(),
                "model redo updates child identities used by later component edits"))
    {
        return false;
    }
    if (!run("undo", identity))
    {
        return false;
    }
    auto* extra = scene.create_node("Unrelated child");
    if (!expect(extra && extra->set_parent(scene.nodes()[0].get()) && !run("undo", identity) &&
                    scene.nodes().size() == 4 && editor.undo_count() == 1,
                "undo never deletes children attached by another operation") ||
        !expect(scene.destroy_node(extra->id()) && run("undo", identity) && scene.nodes().empty(),
                "removing unrelated edits restores valid model undo"))
    {
        return false;
    }
    auto* entry = fixture.files.catalog.entries.back().get();
    entry->content_hash = "0000000000000000";
    if (!expect(fixture.files.publish(fixture.resources) && !run("redo", identity) &&
                    scene.nodes().empty() && editor.redo_count() >= 1,
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
