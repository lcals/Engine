"""真实 MCP → 编辑器主线程 → C++ 建模核心的操作、持久化与撤销测试。"""

import argparse
import math
from pathlib import Path
import tempfile
import tomllib

from editor_lifecycle_test import launch, prepare_project, read_json, terminate, tool, wait_for
from script_tooling_test import call, contents, invoke, require, resource_payload


def test(editor, mcp, root, temporary):
    project = prepare_project(root, temporary)
    (project / "assets/scripts/game.ts").write_text(
        "import { Gameplay, Scene } from 'engine';\n"
        "export default class Game extends Gameplay {\n"
        "  override OnInitialize(): boolean {\n"
        "    return Scene.Load('project/scenes/main.scene') !== null;\n"
        "  }\n}\n",
        encoding="utf-8",
    )
    runtime = temporary / "runtime"
    process = launch(editor, project, runtime, temporary / "modeling-editor.log")
    try:
        wait_for(process, lambda: read_json(runtime / "project-status.json"), "modeling editor")
        tool(mcp, project, runtime, "start_project")
        scene = tool(mcp, project, runtime, "get_scene")["result"]["scene"]
        identity = {"sceneId": scene["sceneId"]}

        def command(name, arguments, expect_error=False):
            # A long editor operation may return isError with a durable pending receipt.
            # Poll that identity; resubmitting the operation could save or edit twice.
            reply = invoke(mcp, project, runtime, [call(name, arguments)])[0]
            require("result" in reply, f"{name} protocol failure: {reply}")
            receipt = contents(reply)
            if receipt.get("activation") == "pending":
                request = {"session": receipt["session"], "requestId": receipt["requestId"]}

                def completed():
                    polled = tool(mcp, project, runtime, "get_editor_receipt", request)
                    require(
                        all(polled.get(key) == value for key, value in request.items()),
                        "polled a different editor request",
                    )
                    return polled if polled.get("activation") == "completed" else None

                receipt = wait_for(process, completed, f"{name} original receipt", timeout=60)
            else:
                require(
                    reply["result"].get("isError", False) is expect_error,
                    f"{name} unexpected result: {receipt}",
                )
            require(receipt.get("activation") == "completed", f"pending command: {receipt}")
            require(receipt.get("projectRoot") == str(project.resolve()), "wrong project")
            require(receipt.get("runtimeDir") == str(runtime.resolve()), "wrong editor runtime")
            require(receipt.get("success") is (not expect_error), f"failed command: {receipt}")
            return receipt.get("result")

        before_render = command("get_render_settings", {})
        render_settings = {
            "ambientColor": [0.5, 0.625, 0.75],
            "ambientIntensity": 0.25,
            "exposure": 1.25,
            "filmicTonemap": True,
            "directionalShadows": True,
            "shadowResolution": 512,
            "shadowBias": 0.001,
            "clearColor": [17 / 255, 34 / 255, 51 / 255, 1],
        }
        command("set_render_settings", render_settings)
        after_render = command("get_render_settings", {})
        for name, expected in render_settings.items():
            actual = after_render[name]
            if isinstance(expected, list):
                require(
                    len(actual) == len(expected)
                    and all(math.isclose(a, b, abs_tol=1e-6) for a, b in zip(actual, expected)),
                    f"render color did not round-trip: {name}={actual}",
                )
            elif isinstance(expected, float):
                require(math.isclose(actual, expected, abs_tol=1e-6), f"render value lost: {name}")
            else:
                require(actual == expected, f"render option lost: {name}")
        command("undo", identity)
        undone_render = command("get_render_settings", {})
        require(
            all(undone_render[name] == before_render[name] for name in render_settings),
            f"render settings undo failed: {undone_render}",
        )
        require(
            undone_render["undoCount"] == before_render["undoCount"]
            and undone_render["redoCount"] == after_render["redoCount"] + 1,
            "render settings undo did not update edit history",
        )
        command("redo", identity)
        redone_render = command("get_render_settings", {})
        require(
            all(redone_render[name] == after_render[name] for name in render_settings),
            f"render settings redo failed: {redone_render}",
        )
        require(
            redone_render["undoCount"] == after_render["undoCount"]
            and redone_render["redoCount"] == after_render["redoCount"],
            "render settings redo did not restore edit history",
        )

        created = command("modeling_create", identity | {"shape": "cube", "name": "MCP cube"})
        target = identity | {"modelId": created["modelId"]}
        initial = command("modeling_get", target)
        require(initial["model"]["faceCount"] == 6, "cube does not retain editable polygon faces")
        require(initial["model"]["vertexCount"] == 8, "cube does not share geometric vertices")
        face = initial["faces"][0]["id"]
        command("modeling_select", target | {"selection": {"faces": [face]}})
        command("modeling_edit", target | {"operation": "extrude", "parameters": {"distance": 0.2}})
        changed = command("modeling_get", target)
        require(changed["model"]["faceCount"] > 6, "extrude did not update native topology")
        command("undo", identity)
        restored = command("modeling_get", target)
        require(restored["faces"] == initial["faces"], "undo did not restore source topology")
        command("modeling_edit", target | {"operation": "extrude", "parameters": {"distance": "bad"}}, True)
        require(command("modeling_get", target)["faces"] == restored["faces"], "failed edit mutated model")
        command("modeling_save", target | {"path": "models/mcp-cube.mesh"})
        source = project / "assets/models/mcp-cube.mesh"
        require(source.is_file() and "flatbuffers-hex" in source.read_text(), "source data not saved")
        command("modeling_save", target | {"path": "../escape.mesh"}, True)
        command("modeling_load", identity | {"path": "models/mcp-cube.mesh", "name": "Reloaded cube"})
        models = command("modeling_get", identity)["models"]
        require(len(models) == 2, "model source did not load")
        command("undo", identity)
        command("modeling_delete", target)
        require(not command("modeling_get", identity)["models"], "delete left model alive")
        command("undo", identity)
        recreated = command("modeling_get", target)
        require(recreated["model"]["nodeId"] != initial["model"]["nodeId"], "node ID was reused")
        command("modeling_get", target | {"nodeId": initial["model"]["nodeId"]}, True)
        command("create_node", identity | {"name": "Saved lamp", "component": "light"})
        command("create_node", identity | {"name": "Saved camera", "component": "camera"})
        composed = command("get_scene", {})["scene"]
        nodes = {node["name"]: node for node in composed["nodes"]}
        camera_node = nodes["Saved camera"]["nodeId"]
        command("set_transform", identity | {"nodeId": camera_node, "position": [0, 1, 7]})
        command("set_active_camera", identity | {"nodeId": camera_node})
        lamp_node = nodes["Saved lamp"]["nodeId"]
        command("set_light", identity | {"nodeId": lamp_node, "kind": "point", "intensity": 4})
        cube_node = nodes["MCP cube"]["nodeId"]
        command("set_material_type", identity | {"nodeId": cube_node, "path": "builtin/materials/default-unlit"})
        command("set_material_parameter", identity | {"nodeId": cube_node, "name": "baseColor", "type": "v4", "value": [0.15, 0.45, 0.75, 1]})

        screenshot = project / "cache/screenshots/mcp-modeling.png"
        queued = command("capture_frame", {"path": "screenshots/mcp-modeling.png"})
        require(queued.get("state") == "pending", "screenshot request must report queued work")
        require(queued.get("path") == str(screenshot.resolve()), "wrong screenshot destination")

        def screenshot_complete():
            current = command("get_frame_capture", {})
            require(current.get("path") == queued["path"], "screenshot request was replaced")
            require(current.get("state") in ("pending", "complete"), f"capture failed: {current}")
            return current if current["state"] == "complete" else None

        captured = wait_for(process, screenshot_complete, "actual rendered PNG")
        require(screenshot.is_file(), "completed screenshot file is missing")
        with screenshot.open("rb") as image:
            header = image.read(24)
        require(header[:8] == b"\x89PNG\r\n\x1a\n", "capture did not produce a PNG")
        require(
            len(header) == 24 and header[12:16] == b"IHDR"
            and int.from_bytes(header[16:20], "big") > 0
            and int.from_bytes(header[20:24], "big") > 0,
            "captured PNG has no valid image dimensions",
        )
        absolute_escape = temporary / "absolute-capture.png"
        command("capture_frame", {"path": "../relative-capture.png"}, True)
        command("capture_frame", {"path": str(absolute_escape.resolve())}, True)
        require(
            not (project / "relative-capture.png").exists() and not absolute_escape.exists(),
            "invalid screenshot path wrote outside the project cache",
        )
        require(command("get_frame_capture", {}) == captured, "rejected capture changed state")

        saved = command("save_scene", identity | {"path": "scenes/main.scene"})
        require(saved["nodeCount"] == len(composed["nodes"]), "save omitted scene nodes")
        require(process.poll() is None, "real editor crashed during scene TOML formatting")
        scene_source = project / "assets/scenes/main.scene"
        scene_meta = scene_source.with_name(scene_source.name + ".meta").read_text(encoding="utf-8")
        require("main-resources" not in scene_source.read_text(encoding="utf-8"), "scene must reference GUIDs, not source paths")
        tool(mcp, project, runtime, "stop_project")
        wait_for(
            process,
            lambda: read_json(runtime / "project-status.json").get("projectState") == "idle",
            "project stopped before source import",
        )
        synced = tool(mcp, project, runtime, "sync_assets")
        require(synced.get("availableRevision"), "resource pipeline did not publish a revision")
        meta = source.with_name(source.name + ".meta")
        require(meta.is_file(), "resource pipeline did not create a model meta file")
        guid = tomllib.loads(meta.read_text(encoding="utf-8"))["guid"]
        cached = project / "cache/gen" / guid
        require(cached.is_file() and resource_payload(cached.read_bytes())[4:8] == b"EMSH", "GUID cache is not MeshData")
        status = read_json(runtime / "project-status.json")
        require(status.get("projectState") == "idle", f"project did not release modeling scene: {status}")
        require(scene_source.with_name(scene_source.name + ".meta").read_text(encoding="utf-8") == scene_meta, "scene GUID changed during import")
        tool(mcp, project, runtime, "start_project")
        loaded = command("get_scene", {})["scene"]
        require(len(loaded["nodes"]) == saved["nodeCount"], "reloaded scene lost nodes")
        by_name = {node["name"]: node for node in loaded["nodes"]}
        require(loaded["activeCameraNodeId"] == by_name["Saved camera"]["nodeId"], "active camera not restored")
        light = next(value for value in by_name["Saved lamp"]["components"] if value["type"] == "light")
        require(light["kind"] == "point" and light["intensity"] == 4, "light not restored")
        renderer = next(value for value in by_name["MCP cube"]["components"] if value["type"] == "mesh_renderer")
        color = next(value for value in renderer["material"]["parameters"] if value["name"] == "baseColor")
        require(
            len(color["value"]) == 4
            and all(math.isclose(a, b, abs_tol=1e-6)
                    for a, b in zip(color["value"], [0.15, 0.45, 0.75, 1])),
            "material override not restored",
        )
        loaded_models = command("modeling_get", {"sceneId": loaded["sceneId"]})["models"]
        restored_models = [model for model in loaded_models if model["name"] == "MCP cube"]
        require(
            len(restored_models) == 1 and restored_models[0]["faceCount"] == 6,
            "editable polygon source not restored",
        )
        loaded_target = {"sceneId": loaded["sceneId"], "modelId": restored_models[0]["modelId"]}
        loaded_mesh = command("modeling_get", loaded_target)
        require(loaded_mesh["faces"] == initial["faces"], "saved polygon topology changed")
        command("modeling_edit", loaded_target | {
            "operation": "extrude", "selection": {"faces": [loaded_mesh["faces"][0]["id"]]},
            "parameters": {"distance": 0.2},
        })
        require(
            command("modeling_get", loaded_target)["model"]["faceCount"] > 6,
            "reloaded mesh cannot be edited through MCP",
        )
        command("undo", {"sceneId": loaded["sceneId"]})
        require(
            command("modeling_get", loaded_target)["faces"] == loaded_mesh["faces"],
            "reloaded mesh edit cannot be undone",
        )
        tool(mcp, project, runtime, "stop_project")
        wait_for(process, lambda: read_json(runtime / "project-status.json").get("projectState") == "idle", "reloaded scene stopped")
    finally:
        terminate(process)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--editor", required=True, type=Path)
    parser.add_argument("--mcp", required=True, type=Path)
    parser.add_argument("--root", required=True, type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="engine-mcp-modeling-") as directory:
        test(args.editor.resolve(), args.mcp.resolve(), args.root.resolve(), Path(directory).resolve())


if __name__ == "__main__":
    main()
