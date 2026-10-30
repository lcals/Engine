"""真实桌面窗口、V8 场景和 MCP 启停集成测试；只在 GPU 测试开启时运行。"""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import time

from sample_project import prepare_minimal_project
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


def tool(mcp, project, runtime, name, arguments=None, expect_error=False, timeout=60):
    def request(tool_name, tool_arguments):
        reply = invoke(mcp, project, runtime, [call(tool_name, tool_arguments)])[0]
        require("result" in reply, f"{tool_name} protocol failure: {reply}")
        return reply["result"].get("isError", False), contents(reply)

    failed, body = request(name, arguments)
    if body.get("activation") == "pending":
        deadline = time.monotonic() + timeout
        project_root = project.resolve().as_posix()
        runtime_directory = runtime.resolve().as_posix()
        request_id = body.get("requestId")
        require(isinstance(request_id, str) and request_id, f"pending identity missing: {body}")
        if name in ("start_project", "stop_project"):
            original = read_json(runtime / "project-control.json")
            require(
                original.get("requestId") == request_id
                and original.get("command") == name
                and original.get("session"),
                f"{name} did not submit this project request: {body}: {original}",
            )
            session = original["session"]
            while time.monotonic() < deadline:
                status = read_json(runtime / "project-status.json")
                require(
                    status.get("session") == session and status.get("projectRoot") == project_root,
                    f"{name} project/session changed before completion: {status}",
                )
                if status.get("lastRequestId") == request_id and status.get("projectState") not in (
                    "starting",
                    "stopping",
                ):
                    require(
                        isinstance(status.get("lastRequestSucceeded"), bool),
                        f"invalid project completion: {status}",
                    )
                    body = dict(
                        status,
                        activation="completed",
                        requestId=request_id,
                        runtimeDir=runtime_directory,
                    )
                    failed = not status["lastRequestSucceeded"]
                    break
                require(status.get("projectState") != "closed", f"{name} editor closed: {status}")
                time.sleep(0.04)
            else:
                raise AssertionError(f"{name} original project request timed out: {original}")
        else:
            identity = {"session": body.get("session"), "requestId": request_id}
            require(
                isinstance(identity["session"], str)
                and identity["session"]
                and body.get("projectRoot") == project_root
                and body.get("runtimeDir") == runtime_directory,
                f"{name} pending project/runtime identity mismatch: {body}",
            )
            command = "batch" if name == "execute_batch" else name
            if name != "get_editor_receipt":
                original = read_json(runtime / "editor-command.json")
                require(
                    all(original.get(key) == value for key, value in identity.items())
                    and original.get("projectRoot") == project_root
                    and original.get("command") == command
                    and original.get("arguments") == (arguments or {})
                    and "Previous editor command" not in body.get("error", ""),
                    f"{name} did not submit this editor request: {body}: {original}",
                )
            while time.monotonic() < deadline:
                query_failed, receipt = request("get_editor_receipt", identity)
                require(not query_failed, f"{name} receipt lookup failed: {receipt}")
                require(
                    all(receipt.get(key) == value for key, value in identity.items())
                    and receipt.get("projectRoot") == project_root
                    and receipt.get("runtimeDir") == runtime_directory,
                    f"{name} polled a different editor request: {receipt}",
                )
                if receipt.get("activation") == "completed":
                    require(
                        isinstance(receipt.get("success"), bool)
                        and (name == "get_editor_receipt" or receipt.get("command") == command),
                        f"{name} invalid original completion: {receipt}",
                    )
                    body = receipt
                    failed = False if name == "get_editor_receipt" else not receipt["success"]
                    break
                require(receipt.get("activation") == "pending", f"invalid receipt state: {receipt}")
                time.sleep(0.04)
            else:
                raise AssertionError(f"{name} original editor request timed out: {identity}")
    require(failed is expect_error, f"{name} unexpected result: {body}")
    return body


def prepare_project(root, temporary):
    project = temporary / "project 中文"
    prepare_minimal_project(root, project)
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

        # Live cache cannot be overwritten by import/build. Prebuilt immutable outputs remain deployable.
        tool(mcp, project, runtime, "start_project")
        source.write_text(
            original_source + "\n// lifecycle reload revision one\n", encoding="utf-8"
        )
        rejected = tool(mcp, project, runtime, "sync_assets", expect_error=True)
        require(
            "stop" in rejected.get("error", "").lower(),
            "live cache import must explain stop_project",
        )
        check_empty(tool(mcp, project, runtime, "stop_project"))
        # Two individually bounded resources form a carrier larger than the per-entity limit.
        large_assets = project / "assets/large"
        large_assets.mkdir()
        for name in ("first.bin", "second.bin"):
            with (large_assets / name).open("wb") as resource:
                resource.truncate(33 * 1024 * 1024)
        (project / "settings/lifecycle-package.toml").write_text(
            'target_platform=""\noutput="cache/build/lifecycle-package"\n'
            '[[items]]\nname="game"\nstorage="package"\n'
            'roots=["project/scripts/game.ts","project/scenes/main.scene"]\n'
            'keep=["project/large/first.bin","project/large/second.bin"]\n',
            encoding="utf-8",
        )
        tool(
            mcp,
            project,
            runtime,
            "build_resources",
            {"manifest": "settings/lifecycle-package.toml"},
        )
        packages = list((project / "cache/build/lifecycle-package/packs").iterdir())
        require(
            len(packages) == 1 and packages[0].stat().st_size > 64 * 1024 * 1024,
            "expected one valid built carrier larger than the per-resource limit",
        )
        tool(mcp, project, runtime, "start_project")
        tool(mcp, project, runtime, "set_hot_reload", {"enabled": False})
        before_revision = read_json(script_status).get("activeRevision")
        revision = tool(
            mcp, project, runtime, "sync_assets", {"source": "cache/build/lifecycle-package"}
        )["availableRevision"]
        wait_for(
            process,
            lambda: read_json(script_status).get("availableRevision") == revision,
            "prebuilt deployment availability",
        )
        require(
            read_json(script_status).get("activeRevision") == before_revision,
            "disabled hot reload applied a synchronized package",
        )
        tool(mcp, project, runtime, "set_hot_reload", {"enabled": True})
        wait_for(
            process,
            lambda: read_json(script_status).get("activeRevision") == revision,
            "explicit package hot reload",
        )
        running = wait_for(
            process,
            lambda: (s if (s := read_json(project_status)).get("running") else None),
            "running after package hot reload",
        )
        require(
            running["session"] == session and process.pid == pid,
            "hot reload replaced the editor session",
        )
        check_empty(tool(mcp, project, runtime, "stop_project"))
        published = (runtime / "assets/available.json").read_bytes()
        source.write_text(
            original_source + "\nconst invalid: number = 'type-error';\n", encoding="utf-8"
        )
        tool(mcp, project, runtime, "sync_assets", expect_error=True)
        require(
            (runtime / "assets/available.json").read_bytes() == published,
            "compile failure changed the published resource set",
        )
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
    # Process exit releases the OS lease; stale markers never require heartbeat expiry.
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
