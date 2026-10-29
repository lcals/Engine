"""真实桌面窗口、V8 场景和 MCP 启停集成测试；只在 GPU 测试开启时运行。"""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

from script_tooling_test import call, contents, invoke, require


def read_json(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def wait_for(process, predicate, description, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        require(process.poll() is None, f"editor exited while waiting for {description}")
        time.sleep(0.04)
    raise AssertionError(f"timeout waiting for {description}")


def terminate(process):
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=8)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def tool(mcp, project, runtime, name, arguments=None, expect_error=False):
    reply = invoke(mcp, project, runtime, [call(name, arguments)])[0]
    require("result" in reply, f"{name} protocol failure: {reply}")
    body = contents(reply)
    require(
        reply["result"].get("isError", False) is expect_error, f"{name} unexpected result: {body}"
    )
    return body


def prepare_project(root, temporary):
    project = temporary / "project 中文"
    shutil.copytree(
        root / "projects/minimal", project, ignore=shutil.ignore_patterns("node_modules", "cache")
    )
    (project / "cache").mkdir()
    compiler = root / "projects/minimal/settings/node_modules/typescript"
    require(compiler.is_dir(), "Run npm ci --prefix projects/minimal/settings first")
    shutil.copytree(compiler, project / "settings/node_modules/typescript")
    config_path = project / "settings/tsconfig.json"
    config = read_json(config_path)
    config["files"] = [str(root / "include/engine.d.ts")]
    config_path.write_text(json.dumps(config), encoding="utf-8")
    return project


def launch(editor, project, runtime, log_path, frames=None):
    arguments = [str(editor), "--project", str(project), "--runtime-dir", str(runtime)]
    if frames is not None:
        arguments += ["--frames", str(frames)]
    stream = log_path.open("w", encoding="utf-8")
    try:
        process = subprocess.Popen(
            arguments, stdin=subprocess.DEVNULL, stdout=stream, stderr=subprocess.STDOUT
        )
    finally:
        stream.close()
    return process


def check_empty(status, state="idle"):
    require(
        status.get("projectState") == state and status.get("running") is False,
        f"expected stopped project in {state}: {status}",
    )


def test(editor, mcp, root, temporary):
    project = prepare_project(root, temporary)
    runtime = temporary / "runtime"
    source = project / "assets/scripts/game.ts"
    original_source = source.read_text(encoding="utf-8")
    log_path = temporary / "editor-live.log"
    process = launch(editor, project, runtime, log_path)
    project_status = runtime / "project-status.json"
    script_status = runtime / "script-status.json"
    marker = "Game.OnInitialize: PBR cube scene ready"
    try:
        status = wait_for(process, lambda: read_json(project_status), "initial idle status")
        check_empty(status)
        session = status["session"]
        pid = process.pid
        starts = 0
        for _ in range(3):
            started = tool(mcp, project, runtime, "start_project")
            starts += 1
            require(
                started.get("activation") == "completed"
                and started["running"]
                and started["projectState"] == "running"
                and started["session"] == session,
                f"start must acknowledge the running native scene: {started}",
            )
            before = log_path.read_text(encoding="utf-8").count(marker)
            duplicate = tool(mcp, project, runtime, "start_project")
            require(
                duplicate["running"]
                and log_path.read_text(encoding="utf-8").count(marker) == before == starts,
                "duplicate start rebuilt gameplay or duplicated native resources",
            )
            require(
                process.pid == pid and process.poll() is None and duplicate["session"] == session,
                "MCP replaced the editor process/session",
            )
            check_empty(tool(mcp, project, runtime, "stop_project"))
            check_empty(tool(mcp, project, runtime, "stop_project"))

        tool(mcp, project, runtime, "start_project")
        tool(mcp, project, runtime, "set_hot_reload", {"enabled": True})
        source.write_text(
            original_source + "\n// lifecycle reload revision one\n", encoding="utf-8"
        )
        revision = tool(mcp, project, runtime, "sync_assets")["availableRevision"]
        wait_for(
            process,
            lambda: read_json(script_status).get("activeRevision") == revision,
            "native hot reload",
        )
        running = wait_for(
            process,
            lambda: (s if (s := read_json(project_status)).get("running") else None),
            "running project after hot reload",
        )
        require(
            running["session"] == session and process.pid == pid,
            "hot reload replaced the editor session",
        )
        published = (runtime / "assets/available.json").read_bytes()
        source.write_text(
            original_source + "\nconst invalid: number = 'type-error';\n", encoding="utf-8"
        )
        tool(mcp, project, runtime, "sync_assets", expect_error=True)
        require(
            (runtime / "assets/available.json").read_bytes() == published
            and read_json(script_status).get("activeRevision") == revision
            and read_json(project_status).get("running") is True,
            "compile failure changed the active scene or published files",
        )
        check_empty(tool(mcp, project, runtime, "stop_project"))
        source.write_text(
            original_source + "\n// lifecycle stopped revision two\n", encoding="utf-8"
        )
        stopped_revision = tool(mcp, project, runtime, "sync_assets")["availableRevision"]
        wait_for(
            process,
            lambda: read_json(script_status).get("availableRevision") == stopped_revision,
            "stopped editor observes published files",
        )
        check_empty(read_json(project_status))
        require(not read_json(script_status).get("running"), "reload restarted a stopped project")

        source.write_text(
            "import {Gameplay,Scene} from 'engine';\n"
            "export default class extends Gameplay {\n"
            " override OnInitialize():boolean { const scene=Scene.Create('partial'); "
            "if(!scene) return false; scene.CreateNode('partial'); return false; }\n}\n",
            encoding="utf-8",
        )
        tool(mcp, project, runtime, "sync_assets")
        failed = tool(mcp, project, runtime, "start_project", expect_error=True)
        check_empty(failed)
        require(
            failed.get("lastError")
            and failed.get("activation") == "completed"
            and process.poll() is None,
            "failed initialization must leave a usable idle editor",
        )
        status = tool(mcp, project, runtime, "get_status")
        check_empty(status)
        require(
            status.get("editorAlive") is True and status["session"] == session,
            "get_status did not report the actual live editor",
        )
    finally:
        terminate(process)

    # 正常退出单独用帧数边界验证，不能把 terminate 当成析构/状态发布验证。
    source.write_text(original_source, encoding="utf-8")
    graceful_log = temporary / "editor-graceful.log"
    graceful = launch(editor, project, runtime, graceful_log, frames=300)
    try:
        current = wait_for(
            graceful,
            lambda: (
                s
                if (s := read_json(project_status)).get("session") != session
                and s.get("projectState") == "idle"
                else None
            ),
            "new editor session",
        )
        check_empty(current)
        started = tool(mcp, project, runtime, "start_project")
        require(
            started["projectState"] == "running" and started["running"],
            "fresh session cannot start native scene",
        )
        graceful.wait(timeout=30)
        require(graceful.returncode == 0, "normal frame-limited editor exit failed")
        closed = read_json(project_status)
        check_empty(closed, "closed")
        require(
            "Game.OnShutdown" in graceful_log.read_text(encoding="utf-8"),
            "normal exit did not execute gameplay cleanup",
        )
    finally:
        terminate(graceful)

    # 遗留 start 请求来自已关闭会话；下次打开同项目/目录必须仍为空白。
    (runtime / "project-control.json").write_text(
        json.dumps(
            {
                "session": closed["session"],
                "requestId": "old-session-replay",
                "command": "start_project",
            }
        ),
        encoding="utf-8",
    )
    replay_log = temporary / "editor-replay.log"
    replay = launch(editor, project, runtime, replay_log, frames=30)
    try:
        replay.wait(timeout=30)
        require(replay.returncode == 0, "editor replay-check process failed")
        final = read_json(project_status)
        check_empty(final, "closed")
        require(
            final["session"] != closed["session"]
            and not final["lastRequestId"]
            and marker not in replay_log.read_text(encoding="utf-8"),
            "old session request restarted gameplay in a new editor",
        )
        status = tool(mcp, project, runtime, "get_status")
        require(
            status.get("editorAlive") is False
            and status.get("projectState") == "offline"
            and status.get("lastReportedProjectState") == "closed"
            and not status["running"],
            "offline status incorrectly reports live gameplay",
        )
    finally:
        terminate(replay)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--editor", required=True, type=Path)
    parser.add_argument("--mcp", required=True, type=Path)
    parser.add_argument("--root", required=True, type=Path)
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="engine-editor-lifecycle-") as directory:
        temporary = Path(directory).resolve()
        try:
            test(
                arguments.editor.resolve(),
                arguments.mcp.resolve(),
                arguments.root.resolve(),
                temporary,
            )
        except BaseException:
            for log in temporary.glob("editor-*.log"):
                print(f"--- {log.name} ---\n{log.read_text(encoding='utf-8')[-16000:]}", flush=True)
            raise
    print(
        "Editor lifecycle PASS: idle/start/stop, native scene/GPU cleanup, reload, session isolation"
    )


if __name__ == "__main__":
    main()
