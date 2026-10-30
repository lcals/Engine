"""通过真实编辑器主线程验证 MCP 场景修改、事务与撤销历史。"""

import argparse
import json
import math
from pathlib import Path
import tempfile

from editor_lifecycle_test import (
    check_empty,
    launch,
    prepare_project,
    read_json,
    terminate,
    tool,
    wait_for,
)
from script_tooling_test import require


def publish(path, value):
    temporary = path.with_name(".test-" + path.name)
    temporary.write_text(json.dumps(value), encoding="utf-8")
    temporary.replace(path)


def node_named(scene, name):
    matches = [node for node in scene["nodes"] if node["name"] == name]
    require(len(matches) == 1, f"expected exactly one node named {name}: {scene}")
    return matches[0]


def component(node, kind):
    matches = [item for item in node["components"] if item["type"] == kind]
    require(len(matches) == 1, f"expected exactly one {kind} component: {node}")
    return matches[0]


def parameter(node, name):
    material = component(node, "mesh_renderer")["material"]
    matches = [item for item in material["parameters"] if item["name"] == name]
    require(len(matches) == 1, f"expected one material parameter {name}: {material}")
    return matches[0]


def near(actual, expected):
    if isinstance(expected, list):
        return (
            isinstance(actual, list)
            and len(actual) == len(expected)
            and all(near(a, b) for a, b in zip(actual, expected))
        )
    return isinstance(actual, (int, float)) and math.isclose(actual, expected, abs_tol=1e-6)


def test(editor, mcp, root, temporary):
    project = prepare_project(root, temporary)
    # 场景由真实脚本加载；去掉示例逐帧旋转，使查询值只受编辑命令改变。
    (project / "assets/scripts/game.ts").write_text(
        "import { Gameplay, Scene } from 'engine';\n"
        "export default class Game extends Gameplay {\n"
        "    override OnInitialize(): boolean {\n"
        "        return Scene.Load('project/scenes/main.scene') !== null;\n"
        "    }\n"
        "}\n",
        encoding="utf-8",
    )
    runtime = temporary / "runtime"
    log_path = temporary / "editor-mcp.log"
    process = launch(editor, project, runtime, log_path)
    status_path = runtime / "project-status.json"
    try:
        status = wait_for(process, lambda: read_json(status_path), "editor idle session")
        check_empty(status)
        session = status["session"]

        def command(name, arguments=None, expect_error=False):
            receipt = tool(mcp, project, runtime, name, arguments, expect_error)
            require(
                receipt.get("activation") == "completed"
                and receipt.get("session") == session
                and receipt.get("projectRoot") == project.resolve().as_posix()
                and receipt.get("command") == ("batch" if name == "execute_batch" else name)
                and receipt.get("success") is not expect_error
                and isinstance(receipt.get("requestId"), str)
                and receipt["requestId"]
                and isinstance(receipt.get("completedAtMs"), int),
                f"command did not return its actual editor completion: {receipt}",
            )
            require(bool(receipt.get("error")) is expect_error, f"invalid error state: {receipt}")
            return receipt

        def snapshot():
            return command("get_scene")["result"]

        idle = snapshot()
        require(
            idle == {"scene": None, "undoCount": 0, "redoCount": 0},
            f"idle editor unexpectedly owns a scene or history: {idle}",
        )
        tool(mcp, project, runtime, "start_project")
        initial = snapshot()
        scene = initial["scene"]
        require(scene is not None and scene["name"] == "PBR cube", "scene was not loaded")
        require(len(scene["nodes"]) == 3, "MCP scene snapshot did not enumerate all nodes")
        cube = node_named(scene, "Cube")
        sun = node_named(scene, "Sun")
        camera = node_named(scene, "Camera")
        identifiers = [scene["sceneId"], scene["activeCameraNodeId"]] + [
            node["nodeId"] for node in scene["nodes"]
        ]
        require(
            all(
                isinstance(value, str)
                and value.isascii()
                and value.isdecimal()
                and str(int(value)) == value
                and 0 < int(value) <= (1 << 64) - 1
                for value in identifiers
            ),
            f"scene/node identities must preserve uint64 through decimal strings: {identifiers}",
        )
        require(scene["activeCameraNodeId"] == camera["nodeId"], "active camera identity is wrong")
        require(all(node["parentId"] is None for node in scene["nodes"]), "unexpected parents")
        require(component(camera, "camera")["nearClip"] > 0, "camera component is incomplete")
        cube_target = {"sceneId": scene["sceneId"], "nodeId": cube["nodeId"]}
        sun_target = {"sceneId": scene["sceneId"], "nodeId": sun["nodeId"]}

        transform = {"position": [1.0, 2.0, -1.0], "rotation": [0.0, 45.0, 0.0], "scale": [2, 1, 1]}
        edited = command("set_transform", cube_target | transform)
        current = snapshot()
        native_transform = component(node_named(current["scene"], "Cube"), "transform")
        require(
            all(near(native_transform[key], value) for key, value in transform.items()),
            f"transform command did not mutate native component: {native_transform}",
        )
        stored = tool(
            mcp,
            project,
            runtime,
            "get_editor_receipt",
            {"session": session, "requestId": edited["requestId"]},
        )
        require(stored == edited, "receipt lookup returned a newer command instead of requested id")

        command("set_light", sun_target | {"color": [0.4, 0.5, 0.6, 1.0], "intensity": 2.5})
        command(
            "set_material_parameter",
            cube_target | {"name": "metallic", "type": "float", "value": 0.7},
        )
        command(
            "set_material_parameter",
            cube_target | {"name": "baseColor", "type": "v4", "value": [0.2, 0.3, 0.4, 1.0]},
        )
        before_batch = snapshot()
        light = component(node_named(before_batch["scene"], "Sun"), "light")
        require(
            near(light["color"], [0.4, 0.5, 0.6, 1.0]) and near(light["intensity"], 2.5),
            "light command did not mutate native component",
        )
        require(
            near(parameter(node_named(before_batch["scene"], "Cube"), "metallic")["value"], 0.7)
            and near(
                parameter(node_named(before_batch["scene"], "Cube"), "baseColor")["value"],
                [0.2, 0.3, 0.4, 1.0],
            ),
            "material named parameter mutation is missing from native snapshot",
        )
        operations = [
            {"command": "set_transform", "arguments": cube_target | {"position": [-1, -2, 0]}},
            {"command": "set_light", "arguments": sun_target | {"intensity": 6.0}},
            {
                "command": "set_material_parameter",
                "arguments": cube_target | {"name": "roughness", "type": "float", "value": 0.8},
            },
        ]
        batch = command("execute_batch", {"commands": operations})
        after_batch = snapshot()
        require(
            batch["result"]["count"] == len(operations)
            and after_batch["undoCount"] == before_batch["undoCount"] + 1
            and after_batch["redoCount"] == 0,
            "batch must create exactly one undo entry",
        )
        require(
            near(
                component(node_named(after_batch["scene"], "Cube"), "transform")["position"],
                [-1, -2, 0],
            )
            and near(component(node_named(after_batch["scene"], "Sun"), "light")["intensity"], 6.0)
            and near(
                parameter(node_named(after_batch["scene"], "Cube"), "roughness")["value"], 0.8
            ),
            "batch did not apply every native change",
        )
        command("undo", {"sceneId": scene["sceneId"]})
        undone = snapshot()
        require(
            undone["scene"] == before_batch["scene"]
            and undone["undoCount"] == before_batch["undoCount"]
            and undone["redoCount"] == 1,
            "one undo did not restore the complete batch",
        )
        command("redo", {"sceneId": scene["sceneId"]})
        require(snapshot() == after_batch, "one redo did not restore the complete batch")

        failed_batch = command(
            "execute_batch",
            {
                "commands": [
                    {
                        "command": "set_transform",
                        "arguments": cube_target | {"position": [9, 8, 7]},
                    },
                    {"command": "set_light", "arguments": cube_target | {"intensity": 10}},
                ]
            },
            expect_error=True,
        )
        require(snapshot() == after_batch, "failed batch left a partial change or history entry")
        failed_receipt = tool(
            mcp,
            project,
            runtime,
            "get_editor_receipt",
            {"session": session, "requestId": failed_batch["requestId"]},
        )
        require(
            failed_receipt == failed_batch and failed_receipt["success"] is False,
            "receipt lookup must succeed while preserving the original operation failure",
        )
        # 批次项由编辑器注册表判断能力，MCP 传输层不能固化模块命令枚举。
        for invalid_command in ("get_scene", "future.edit"):
            rejected = command(
                "execute_batch",
                {
                    "commands": [
                        {
                            "command": "set_transform",
                            "arguments": cube_target | {"position": [9, 8, 7]},
                        },
                        {"command": invalid_command, "arguments": {}},
                    ]
                },
                expect_error=True,
            )
            require(
                rejected["result"] is None and snapshot() == after_batch,
                f"batch with {invalid_command} did not roll back its earlier edit and history",
            )
        command(
            "set_material_parameter",
            cube_target | {"name": "unknownParameter", "type": "float", "value": 1},
            expect_error=True,
        )
        require(snapshot() == after_batch, "unknown material parameter changed scene/history")

        # 重放撤销比重放绝对属性赋值更能检测重复执行：再次撤销会消耗另一条历史。
        undo_receipt = command("undo", {"sceneId": scene["sceneId"]})
        after_undo = snapshot()
        require(after_undo == undone, "failed commands changed the undo history")

        def replay(envelope):
            before = read_json(status_path)["updatedAtMs"]
            publish(runtime / "editor-command.json", envelope)
            wait_for(
                process,
                lambda: read_json(status_path).get("updatedAtMs", 0) > before + 300,
                "editor main thread polling replayed command",
            )

        envelope = {
            "session": session,
            "projectRoot": project.resolve().as_posix(),
            "requestId": undo_receipt["requestId"],
            "command": "undo",
            "arguments": {"sceneId": scene["sceneId"]},
        }
        replay(envelope)
        require(snapshot() == after_undo, "duplicate request consumed another undo operation")
        replay(
            envelope
            | {
                "session": session + "-expired",
                "requestId": "expired-session-request",
                "command": "set_transform",
                "arguments": cube_target | {"position": [99, 99, 99]},
            }
        )
        require(snapshot() == after_undo, "expired editor session mutated the current scene")

        check_empty(tool(mcp, project, runtime, "stop_project"))
        require(snapshot() == idle, "stopped project retained scene or undo references")
        tool(mcp, project, runtime, "start_project")
        restarted = snapshot()
        require(restarted["scene"]["sceneId"] != scene["sceneId"], "scene identity was reused")
        command("set_transform", cube_target | {"position": [3, 2, 1]}, expect_error=True)
        command("undo", {"sceneId": scene["sceneId"]}, expect_error=True)
        require(snapshot() == restarted, "expired scene identity changed the replacement scene")
        fresh_scene = snapshot()["scene"]
        fresh_camera = node_named(fresh_scene, "Camera")
        fresh_cube = node_named(fresh_scene, "Cube")
        fresh_target = {"sceneId": fresh_scene["sceneId"], "nodeId": fresh_camera["nodeId"]}
        command("set_camera", fresh_target | {"projection": "orthographic", "orthographicSize": 4})
        require(
            component(node_named(snapshot()["scene"], "Camera"), "camera")["projection"]
            == "orthographic",
            "camera command did not reach renderer scene",
        )
        command(
            "set_material_texture",
            {
                "sceneId": fresh_scene["sceneId"],
                "nodeId": fresh_cube["nodeId"],
                "name": "baseColorTexture",
                "path": "builtin/textures/white-linear",
            },
        )
        textures = component(node_named(snapshot()["scene"], "Cube"), "mesh_renderer")["material"][
            "textures"
        ]
        require(
            any(
                item == {"name": "baseColorTexture", "path": "builtin/textures/white-linear"}
                for item in textures
            ),
            "texture assignment missing from snapshot",
        )
        command("undo", {"sceneId": fresh_scene["sceneId"]})
        check_empty(tool(mcp, project, runtime, "stop_project"))
        require(snapshot() == idle, "final stop retained editor command resources")
    finally:
        terminate(process)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--editor", required=True, type=Path)
    parser.add_argument("--mcp", required=True, type=Path)
    parser.add_argument("--root", required=True, type=Path)
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="engine-mcp-editor-") as directory:
        temporary = Path(directory).resolve()
        try:
            test(
                arguments.editor.resolve(),
                arguments.mcp.resolve(),
                arguments.root.resolve(),
                temporary,
            )
        except BaseException:
            log = temporary / "editor-mcp.log"
            if log.exists():
                print(log.read_text(encoding="utf-8")[-16000:], flush=True)
            raise
    print(
        "Editor MCP PASS: native mutations, batch rollback, undo/redo, receipts and stale identities"
    )


if __name__ == "__main__":
    main()
