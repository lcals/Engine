#include "resource_fixture.hpp"

#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "editor/commands/editor_commands.hpp"
#include "plugins/editor/animation_tools/animation_authoring_commands.hpp"
#include "plugins/editor/asset_import/fbx_importer.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/schema/animation_generated.h"
#include "runtime/schema/skeleton_generated.h"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
namespace fs = std::filesystem;
namespace io = engine::editor::asset_pipeline;
namespace schema = engine::schema;
using Json = nlohmann::json;

bool expect(bool success, const char* message, const std::string& error = {})
{
    if (!success)
    {
        std::fprintf(stderr, "animation authoring: %s: %s\n", message, error.c_str());
    }
    return success;
}

std::string read(const fs::path& path)
{
    std::string output;
    return engine::ResourceSystem::read_text(path, output) ? output : std::string{};
}

bool write(const fs::path& path, const std::string& contents)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    stream.close();
    return static_cast<bool>(stream);
}

bool payload(const fs::path& project, std::string_view path, engine::ByteBuffer& output)
{
    engine::ResourceCatalog catalog;
    engine::Guid guid;
    return engine::ResourceSystem::read_catalog(project / "cache", catalog) &&
           catalog.identities().path_to_guid(path, guid) &&
           engine::ResourceSystem::read_resource(catalog, {{"content", project / "cache"}}, guid,
                                                 output);
}

Json request(const Json& receipt)
{
    return {{"path", receipt["path"]},
            {"sourceHash", receipt["sourceHash"]},
            {"metaHash", receipt["metaHash"]}};
}

bool exercise()
{
    test_support::ResourceFixture fixture;
    std::error_code code;
    for (const auto* directory : {"assets", "settings", "cache"})
    {
        fs::create_directories(fixture.root / directory, code);
        if (code)
        {
            return false;
        }
    }
    if (!write(fixture.root / "settings/editor-import.toml", "[common]\nimport_threads = 8\n"))
    {
        return false;
    }
    for (const auto* name : {"skeleton.fbx", "locomotions.fbx"})
    {
        fs::copy_file(fs::path(ENGINE_FBX_TEST_DATA_DIR) / name, fixture.root / "assets" / name,
                      code);
        if (code)
        {
            return false;
        }
    }
    const auto source = fixture.root / "assets/skeleton.fbx";
    const auto meta = fixture.root / "assets/skeleton.fbx.meta";
    const auto original_source = read(source);
    engine::ThreadSystem threads;
    engine::TaskSystem tasks(threads);
    engine::editor::AssetPipelineSystem pipeline(tasks);
    std::unique_ptr<io::AssetImporter> fbx =
        std::make_unique<engine::plugins::asset_import::FbxImporter>();
    if (!expect(threads.initialize({}) && tasks.initialize({}) && pipeline.initialize({}) &&
                    pipeline.add_importer(fbx) && pipeline.build_assets(fixture.root),
                "import public FBX fixtures through the production pipeline",
                pipeline.last_error()))
    {
        return false;
    }
    std::uint64_t scene_id = 42;
    engine::editor::EditorCommands commands([&scene_id] { return scene_id; });
    if (!expect(engine::plugins::animation_tools::register_animation_authoring_commands(
                    commands, fixture.root),
                "register authoring commands"))
    {
        return false;
    }
    Json receipt, output;
    std::string error;
    if (!expect(
            commands.execute("get_animation_authoring", {{"path", "skeleton.fbx"}}, receipt, error),
            "read exact source identity, bone paths and clips", error) ||
        !expect(!receipt["joints"].empty() && !receipt["animations"].empty(),
                "authoring exposes actual FBX joints and animation keys"))
    {
        return false;
    }
    const auto initial_meta = read(meta);
    const auto original_guid = receipt["guid"];
    std::string start, middle, end;
    for (const auto& first : receipt["joints"])
    {
        for (const auto& second : receipt["joints"])
        {
            for (const auto& third : receipt["joints"])
            {
                const auto a = first.get<std::string>(), b = second.get<std::string>(),
                           c = third.get<std::string>();
                if (b.starts_with(a + "/") && c.starts_with(b + "/"))
                {
                    start = a;
                    middle = b;
                    end = c;
                    break;
                }
            }
            if (!end.empty())
            {
                break;
            }
        }
        if (!end.empty())
        {
            break;
        }
    }
    if (!expect(!end.empty(), "fixture contains a valid two-bone ancestor chain"))
    {
        return false;
    }
    const auto clip = receipt["animations"][0]["key"].get<std::string>();
    const auto duration = receipt["animations"][0]["duration"].get<double>();
    Json changed = request(receipt);
    changed["skeleton_sockets"] =
        Json::array({{{"name", "weapon"}, {"joint_path", end}, {"translation", {0.1, 0.2, 0.3}}}});
    changed["skeleton_aim_ik"] =
        Json::array({{{"name", "look"}, {"joint_path", middle}, {"order", 2}}});
    changed["skeleton_two_bone_ik"] = Json::array({{{"name", "arm"},
                                                    {"start_path", start},
                                                    {"middle_path", middle},
                                                    {"end_path", end},
                                                    {"order", 2}}});
    changed["animation_events"] =
        Json::array({{{"animation", clip}, {"marker_id", 1}, {"time", 0}, {"name", "start"}},
                     {{"animation", clip},
                      {"marker_id", 2},
                      {"time", duration * .25},
                      {"name", "flag"},
                      {"payload_bool", false}},
                     {{"animation", clip},
                      {"marker_id", 3},
                      {"time", duration * .5},
                      {"name", "number"},
                      {"payload_number", 3.25}},
                     {{"animation", clip},
                      {"marker_id", 4},
                      {"time", duration},
                      {"name", "end"},
                      {"payload_string", ""}}});
    if (!expect(commands.execute("set_animation_authoring", changed, output, error),
                "author socket, two IK definitions and typed timeline markers", error) ||
        !expect(output["guid"] == original_guid && output["reimportPending"] == true &&
                    commands.undo_count() == 1 && commands.mutation_revision() == 0 &&
                    read(source) == original_source,
                "edit preserves source bytes/GUID, uses shared history and does not dirty scene"))
    {
        return false;
    }
    const auto authored_meta = read(meta);
    if (!expect(pipeline.build_assets(fixture.root),
                "authoring reimports through the existing pipeline", pipeline.last_error()))
    {
        return false;
    }
    const auto published_revision = pipeline.last_build().revision;
    if (!expect(pipeline.build_assets(fixture.root) && pipeline.last_build().imported == 0 &&
                    pipeline.last_build().revision == published_revision,
                "unchanged rig and event authoring reuse the published GUID resources",
                pipeline.last_error()))
    {
        return false;
    }
    engine::ByteBuffer bytes;
    if (!expect(payload(fixture.root, "skeleton.fbx/skeleton", bytes), "read generated skeleton"))
    {
        return false;
    }
    const auto* skeleton = schema::GetSkeletonData(bytes.data());
    if (!expect(skeleton->sockets()->size() == 1 && skeleton->aim_ik()->size() == 1 &&
                    skeleton->two_bone_ik()->size() == 1 &&
                    skeleton->sockets()->Get(0)->joint_path()->str() == end &&
                    skeleton->sockets()->Get(0)->offset()->translation().x() == .1F,
                "official GUID skeleton contains authored socket and IK definitions"))
    {
        return false;
    }
    if (!expect(payload(fixture.root, "skeleton.fbx/" + clip, bytes), "read generated action"))
    {
        return false;
    }
    const auto* events = schema::GetAnimationData(bytes.data())->events();
    if (!expect(events && events->size() == 4 &&
                    events->Get(0)->payload_type() == schema::AnimationEventPayload::NONE &&
                    events->Get(1)->payload_as_AnimationEventBoolean() &&
                    !events->Get(1)->payload_as_AnimationEventBoolean()->value() &&
                    events->Get(2)->payload_as_AnimationEventNumber()->value() == 3.25 &&
                    events->Get(3)->payload_as_AnimationEventString()->value()->str().empty(),
                "no/bool/number/string payload types survive actual FlatBuffers import"))
    {
        return false;
    }
    if (!expect(commands.execute("undo", {{"sceneId", "42"}}, output, error) &&
                    read(meta) == initial_meta &&
                    commands.execute("redo", {{"sceneId", "42"}}, output, error) &&
                    read(meta) == authored_meta,
                "undo and redo restore exact source metadata", error))
    {
        return false;
    }
    if (!expect(!commands.execute("set_animation_authoring", changed, output, error) &&
                    read(meta) == authored_meta,
                "stale hashes reject lost updates"))
    {
        return false;
    }
    if (!expect(
            commands.execute("get_animation_authoring", {{"path", "skeleton.fbx"}}, receipt, error),
            "read current authoring", error))
    {
        return false;
    }
    const auto base = request(receipt);
    std::vector<Json> invalid;
    auto bad = base;
    bad["animation_events"] = changed["animation_events"];
    bad["animation_events"][1]["marker_id"] = 1;
    invalid.push_back(bad);
    bad["animation_events"][1]["marker_id"] = 0;
    invalid.push_back(bad);
    bad = base;
    bad["animation_events"] = Json::array(
        {{{"animation", "animations/missing"}, {"marker_id", 1}, {"time", 0}, {"name", "bad"}}});
    invalid.push_back(bad);
    bad["animation_events"][0]["animation"] = clip;
    bad["animation_events"][0]["time"] = duration + 1;
    invalid.push_back(bad);
    bad["animation_events"][0]["time"] = true;
    invalid.push_back(bad);
    bad["animation_events"][0]["time"] = 0;
    bad["animation_events"][0]["payload_bool"] = true;
    bad["animation_events"][0]["payload_number"] = 1;
    invalid.push_back(bad);
    bad = base;
    bad["skeleton_sockets"] = Json::array({{{"name", "bad"}, {"joint_path", "missing"}}});
    invalid.push_back(bad);
    bad["skeleton_sockets"][0]["joint_path"] = end;
    bad["skeleton_sockets"][0]["rotation"] = {0, 0, 0, 0};
    invalid.push_back(bad);
    bad = base;
    bad["skeleton_aim_ik"] =
        Json::array({{{"name", "look"}, {"joint_path", middle}, {"forward_joint", {0, 1, 0}}}});
    invalid.push_back(bad);
    bad = base;
    bad["skeleton_two_bone_ik"] = changed["skeleton_two_bone_ik"];
    bad["skeleton_two_bone_ik"][0]["start_path"] = end;
    invalid.push_back(bad);
    bad = base;
    bad["skeleton_two_bone_ik"] = changed["skeleton_two_bone_ik"];
    bad["skeleton_two_bone_ik"][0]["name"] = "look";
    invalid.push_back(bad);
    bad = base;
    bad["unexpected"] = true;
    invalid.push_back(bad);
    bad = base;
    bad["path"] = "../skeleton.fbx";
    invalid.push_back(bad);
    for (const auto& input : invalid)
    {
        if (!expect(!commands.execute("set_animation_authoring", input, output, error) &&
                        read(meta) == authored_meta && commands.undo_count() == 1,
                    "invalid authoring leaves source and history unchanged", error))
        {
            return false;
        }
    }
    auto clearing = base;
    clearing["animation_events"] = Json::array();
    if (!expect(commands.execute("batch",
                                 {{"commands", Json::array({{{"command", "set_animation_authoring"},
                                                             {"arguments", clearing}},
                                                            {{"command", "unknown_animation_edit"},
                                                             {"arguments", Json::object()}}})}},
                                 output, error) == false &&
                    read(meta) == authored_meta && commands.undo_count() == 1,
                "failed shared batch rolls back already-applied metadata", error))
    {
        return false;
    }
    if (!expect(commands.execute("set_animation_authoring", clearing, output, error) &&
                    output["animation_events"].empty() && output["skeleton_sockets"].size() == 1 &&
                    commands.execute("undo", {{"sceneId", "42"}}, output, error) &&
                    read(meta) == authored_meta,
                "empty array clears only its section and remains undoable", error))
    {
        return false;
    }
    if (!expect(write(meta, authored_meta + "\n# external edit\n") &&
                    !commands.execute("undo", {{"sceneId", "42"}}, output, error) &&
                    read(meta) == authored_meta + "\n# external edit\n" &&
                    write(meta, authored_meta),
                "undo does not overwrite external changes", error))
    {
        return false;
    }
    // A model that references an external skeleton cannot silently author a second unused rig.
    const auto motion_meta = fixture.root / "assets/locomotions.fbx.meta";
    auto motion_settings = read(motion_meta);
    const auto skeleton_setting = motion_settings.find("skeleton =");
    if (!expect(skeleton_setting != std::string::npos, "importer preserves the skeleton setting"))
    {
        return false;
    }
    motion_settings.replace(skeleton_setting,
                            motion_settings.find('\n', skeleton_setting) - skeleton_setting,
                            "skeleton = \"skeleton.fbx/skeleton\"");
    if (!expect(write(motion_meta, motion_settings),
                "prepare animation-only external skeleton mapping"))
    {
        return false;
    }
    if (!expect(commands.execute("get_animation_authoring", {{"path", "locomotions.fbx"}}, receipt,
                                 error),
                "external-skeleton authoring query", error))
    {
        return false;
    }
    bad = request(receipt);
    bad["skeleton_sockets"] = changed["skeleton_sockets"];
    const auto unchanged_motion = read(motion_meta);
    if (!expect(!commands.execute("set_animation_authoring", bad, output, error) &&
                    read(motion_meta) == unchanged_motion,
                "reject orphan rig definitions on external-skeleton animation sources", error))
    {
        return false;
    }
    const auto stable_revision = pipeline.last_build().revision;
    if (!expect(
            write(meta, authored_meta +
                            "\n[[animation_events]]\nanimation = \"animations/missing\"\nmarker_id "
                            "= 99\ntime = 0\nname = \"bad\"\n") &&
                !pipeline.build_assets(fixture.root) &&
                pipeline.last_build().revision == stable_revision &&
                payload(fixture.root, "skeleton.fbx/" + clip, bytes) &&
                schema::GetAnimationData(bytes.data())->events()->size() == 4,
            "invalid manually authored meta cannot publish over the previous runtime resources",
            pipeline.last_error()))
    {
        return false;
    }
    return true;
}
} // namespace

int main()
{
    return exercise() ? 0 : 1;
}
