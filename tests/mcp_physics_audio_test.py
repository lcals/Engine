"""在真实编辑器中经 MCP 添加物理/音频，验证运行与保存重载；音频使用 dummy 设备。"""

import argparse
import math
from pathlib import Path
import struct
import tempfile
import wave

from editor_lifecycle_test import (
    check_empty,
    launch,
    prepare_project,
    read_json,
    terminate,
    tool,
    wait_for,
)
from mcp_editor_test import component, node_named
from script_tooling_test import require


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
    audio = project / "assets/audio/test.wav"
    audio.parent.mkdir()
    with wave.open(str(audio), "wb") as stream:
        stream.setparams((1, 2, 24000, 0, "NONE", "not compressed"))
        stream.writeframes(
            b"".join(
                struct.pack("<h", round(300 * math.sin(2 * math.pi * 220 * n / 24000)))
                for n in range(24000)
            )
        )
    runtime = temporary / "runtime"
    process = launch(editor, project, runtime, temporary / "editor.log")
    try:
        check_empty(wait_for(process, lambda: read_json(runtime / "project-status.json"), "idle"))
        tool(mcp, project, runtime, "start_project")

        def command(name, arguments=None):
            reply = tool(mcp, project, runtime, name, arguments)
            require(reply.get("success") is True, f"{name}: {reply}")
            return reply["result"]

        scene = command("get_scene")["scene"]
        identity = {"sceneId": scene["sceneId"]}
        cube = identity | {"nodeId": node_named(scene, "Cube")["nodeId"]}
        camera = identity | {"nodeId": node_named(scene, "Camera")["nodeId"]}
        command("create_node", identity | {"name": "Ground", "component": "empty"})
        scene = command("get_scene")["scene"]
        ground = identity | {"nodeId": node_named(scene, "Ground")["nodeId"]}
        command("set_transform", ground | {"position": [0, -0.5, 0]})
        command("set_collider", ground | {"halfExtents": [10, 0.5, 10]})
        command("set_transform", cube | {"position": [0, 3, 0], "rotation": [0, 0, 0]})
        command("set_collider", cube | {"halfExtents": [0.5, 0.5, 0.5]})
        command("set_rigid_body", cube | {"motion": "dynamic", "mass": 2})

        def landed():
            node = node_named(command("get_scene")["scene"], "Cube")
            height = component(node, "transform")["position"][1]
            return 0.45 < height < 0.6

        wait_for(process, landed, "Jolt cube landing on ground")
        command("set_audio_listener", camera | {"volume": 0.5})
        command(
            "set_audio_source",
            cube
            | {
                "clip": "project/audio/test.wav",
                "loop": True,
                "playOnAwake": False,
                "spatial": False,
                "volume": 0.2,
            },
        )
        command("play_audio", cube)
        wait_for(process, lambda: command("get_audio_state", cube)["playing"], "audio playing")
        command("set_audio_source", cube | {"volume": 0.15, "pitch": 0.9})
        require(command("get_audio_state", cube)["playing"], "editing volume stopped playback")
        command("pause_audio", cube)
        require(not command("get_audio_state", cube)["playing"], "pause did not take effect")
        command("play_audio", cube)
        command("set_rigid_body", cube | {"motion": "static_body"})
        command("save_scene", identity | {"path": "scenes/main.scene"})
        check_empty(tool(mcp, project, runtime, "stop_project"))
        tool(mcp, project, runtime, "sync_assets")
        tool(mcp, project, runtime, "start_project")
        reloaded = command("get_scene")["scene"]
        cube = {"sceneId": reloaded["sceneId"], "nodeId": node_named(reloaded, "Cube")["nodeId"]}
        require(command("get_collider", cube)["halfExtents"] == [0.5, 0.5, 0.5], "collider lost")
        require(command("get_rigid_body", cube)["motion"] == "static_body", "body lost")
        source = command("get_audio_source", cube)
        require(source["clip"] == "project/audio/test.wav" and source["loop"], "audio GUID lost")
        command("play_audio", cube)
        wait_for(process, lambda: command("get_audio_state", cube)["playing"], "reloaded audio")
        command("stop_audio", cube)
        require(not command("get_audio_state", cube)["playing"], "stop did not take effect")
        check_empty(tool(mcp, project, runtime, "stop_project"))
        require(command("get_scene")["scene"] is None, "stop left a scene")
    finally:
        terminate(process)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--editor", required=True, type=Path)
    parser.add_argument("--mcp", required=True, type=Path)
    parser.add_argument("--root", required=True, type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="engine-physics-audio-") as directory:
        temporary = Path(directory).resolve()
        try:
            test(args.editor.resolve(), args.mcp.resolve(), args.root.resolve(), temporary)
        except BaseException:
            log = temporary / "editor.log"
            if log.exists():
                print(log.read_text(encoding="utf-8")[-16000:], flush=True)
            raise
    print("MCP physics, audio, scene persistence and restart passed")


if __name__ == "__main__":
    main()
