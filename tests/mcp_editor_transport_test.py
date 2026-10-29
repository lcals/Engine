"""真实 MCP 进程配合文件传输替身，验证等待、回执身份和未完成请求保护。"""

import argparse
import json
from pathlib import Path
import tempfile
import threading
import time

from script_tooling_test import call, contents, invoke, require


def publish(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(".test-" + path.name)
    temporary.write_text(json.dumps(value), encoding="utf-8")
    temporary.replace(path)


def response(mcp, project, runtime, name, arguments=None):
    reply = invoke(mcp, project, runtime, [call(name, arguments)])[0]
    require("result" in reply, f"unexpected MCP protocol failure: {reply}")
    return reply["result"].get("isError", False), contents(reply)


class EditorFixture:
    """只模拟传输心跳，不模拟场景执行；原生修改另由 GPU 集成测试验证。"""

    def __init__(self, project, runtime):
        self.project = project
        self.runtime = runtime
        self.session = "transport-session-001"
        self.project_root = str(project.resolve())
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.failure = None
        self.thread = threading.Thread(target=self.run, daemon=True)

    def status(self, state="running"):
        with self.lock:
            return {
                "session": self.session,
                "projectRoot": self.project_root,
                "projectState": state,
                "running": state == "running",
                "lastRequestId": "",
                "lastRequestSucceeded": True,
                "lastCommand": "",
                "lastError": "",
                "updatedAtMs": time.time_ns() // 1_000_000,
            }

    def run(self):
        try:
            while not self.stop.wait(0.05):
                publish(self.runtime / "project-status.json", self.status())
        except BaseException as error:
            self.failure = error

    def __enter__(self):
        publish(self.runtime / "project-status.json", self.status())
        self.thread.start()
        return self

    def __exit__(self, kind, value, traceback):
        self.stop.set()
        self.thread.join(timeout=3)
        require(not self.thread.is_alive(), "fixture heartbeat did not stop")
        if kind is None and self.failure is not None:
            raise self.failure

    def completed(self, envelope):
        receipt = {
            "session": envelope["session"],
            "projectRoot": envelope["projectRoot"],
            "requestId": envelope["requestId"],
            "command": envelope["command"],
            "success": True,
            "result": {"scene": None, "undoCount": 0, "redoCount": 0},
            "error": "",
            "completedAtMs": time.time_ns() // 1_000_000,
        }
        path = (
            self.runtime / "editor-receipts" / receipt["session"] / (receipt["requestId"] + ".json")
        )
        publish(path, receipt)
        publish(self.runtime / "editor-receipt.json", receipt)
        return receipt


def test(mcp, temporary):
    project = temporary / "project 中文"
    for name in ("settings", "cache", "assets"):
        (project / name).mkdir(parents=True)
    runtime = temporary / "runtime"
    runtime.mkdir()
    request_path = runtime / "editor-command.json"
    with EditorFixture(project, runtime) as editor:
        failed, pending = response(mcp, project, runtime, "get_scene")
        require(
            failed
            and pending.get("activation") == "pending"
            and pending.get("session") == editor.session
            and pending.get("projectRoot") == str(project.resolve())
            and pending.get("requestId"),
            f"unacknowledged command must return a queryable pending identity: {pending}",
        )
        original = request_path.read_bytes()
        envelope = json.loads(original)
        require(
            envelope["requestId"] == pending["requestId"]
            and envelope["session"] == editor.session
            and envelope["projectRoot"] == str(project.resolve())
            and envelope["command"] == "get_scene"
            and envelope["arguments"] == {},
            "MCP response does not identify the submitted command envelope",
        )
        identity = {"session": editor.session, "requestId": envelope["requestId"]}
        _, queried = response(mcp, project, runtime, "get_editor_receipt", identity)
        require(
            queried.get("activation") == "pending"
            and queried.get("requestId") == envelope["requestId"],
            f"receipt lookup claimed completion before execution: {queried}",
        )
        failed, blocked = response(
            mcp,
            project,
            runtime,
            "set_transform",
            {"sceneId": "1", "nodeId": "2", "position": [1, 2, 3]},
        )
        require(
            failed and request_path.read_bytes() == original,
            f"new command overwrote an unacknowledged request: {blocked}",
        )

        completed = editor.completed(envelope)
        failed, queried = response(mcp, project, runtime, "get_editor_receipt", identity)
        require(
            not failed
            and queried.get("activation") == "completed"
            and all(queried.get(key) == value for key, value in completed.items()),
            f"timed-out command completion cannot be queried by its original identity: {queried}",
        )

        # 最新回执属于另一请求；按 ID 查询仍必须返回原回执。
        other = dict(envelope, requestId="later-query")
        editor.completed(other)
        failed, historic = response(mcp, project, runtime, "get_editor_receipt", identity)
        require(
            not failed and historic == queried, "receipt lookup confused newest and requested ids"
        )

        unsuccessful = dict(
            completed,
            requestId="failed-operation",
            command="set_light",
            success=False,
            result=None,
            error="Node has no Light",
        )
        publish(
            runtime / "editor-receipts" / editor.session / "failed-operation.json", unsuccessful
        )
        failed, original_failure = response(
            mcp,
            project,
            runtime,
            "get_editor_receipt",
            {"session": editor.session, "requestId": "failed-operation"},
        )
        require(
            not failed
            and original_failure.get("activation") == "completed"
            and all(original_failure.get(key) == value for key, value in unsuccessful.items()),
            f"querying a completed failure must preserve its result and succeed: {original_failure}",
        )

        failed, unknown = response(
            mcp,
            project,
            runtime,
            "get_editor_receipt",
            {"session": editor.session, "requestId": "never-submitted"},
        )
        require(
            failed and unknown.get("activation") != "completed",
            f"unknown request was confused with the latest receipt: {unknown}",
        )
        for field in ("session", "requestId"):
            for value in (
                "",
                "..",
                "../outside",
                "folder/file",
                "folder\\file",
                "/absolute",
                "bad\x00id",
                "错",
                "a" * 129,
            ):
                invalid_identity = dict(identity, **{field: value})
                rejected = invoke(
                    mcp, project, runtime, [call("get_editor_receipt", invalid_identity)]
                )[0]
                require(
                    rejected.get("error", {}).get("code") == -32602
                    or rejected.get("result", {}).get("isError") is True,
                    f"invalid receipt path identity was accepted: {invalid_identity}: {rejected}",
                )
        require(
            request_path.read_bytes() == original,
            "invalid or unknown receipt lookup changed the outstanding request",
        )

        # 即使链接内容身份完全匹配，也不能沿回执文件或上级目录链接读取。
        linked = dict(completed, requestId="linked-receipt")
        external_file = temporary / "external-receipt.json"
        publish(external_file, linked)
        link_path = runtime / "editor-receipts" / editor.session / "linked-receipt.json"
        try:
            link_path.symlink_to(external_file)
        except (NotImplementedError, OSError) as error:
            print(f"Symlink receipt checks skipped: {error}", flush=True)
        else:
            failed, symlink = response(
                mcp,
                project,
                runtime,
                "get_editor_receipt",
                {"session": editor.session, "requestId": "linked-receipt"},
            )
            require(failed, f"receipt file symlink was followed: {symlink}")
            link_path.unlink()
            external_session = temporary / "external-session"
            linked_session = "linked-session"
            publish(
                external_session / "linked-receipt.json",
                dict(linked, session=linked_session),
            )
            session_link = runtime / "editor-receipts" / linked_session
            session_link.symlink_to(external_session, target_is_directory=True)
            try:
                failed, symlink = response(
                    mcp,
                    project,
                    runtime,
                    "get_editor_receipt",
                    {"session": linked_session, "requestId": "linked-receipt"},
                )
                require(failed, f"receipt session directory symlink was followed: {symlink}")
            finally:
                session_link.unlink()

        forged = dict(completed, requestId="wrong-project", projectRoot=str(temporary / "other"))
        publish(runtime / "editor-receipts" / editor.session / "wrong-project.json", forged)
        failed, invalid = response(
            mcp,
            project,
            runtime,
            "get_editor_receipt",
            {"session": editor.session, "requestId": "wrong-project"},
        )
        require(failed, f"receipt from a different project was accepted: {invalid}")

        forged = dict(completed, requestId="wrong-session", session="another-session")
        publish(runtime / "editor-receipts" / editor.session / "wrong-session.json", forged)
        failed, invalid = response(
            mcp,
            project,
            runtime,
            "get_editor_receipt",
            {"session": editor.session, "requestId": "wrong-session"},
        )
        require(failed, f"receipt from a different session was accepted: {invalid}")

        # 目标项目身份必须在写入控制文件之前确认。
        with editor.lock:
            editor.project_root = str(temporary / "another-project")
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            status = json.loads((runtime / "project-status.json").read_text(encoding="utf-8"))
            if status["projectRoot"] == editor.project_root:
                break
            time.sleep(0.02)
        require(
            status["projectRoot"] == editor.project_root, "fixture target identity did not update"
        )
        failed, mismatch = response(mcp, project, runtime, "get_scene")
        require(
            failed and request_path.read_bytes() == original,
            f"MCP submitted a command to another project: {mismatch}",
        )

    # 历史回执不依赖活跃心跳；用户可以在编辑器退出后确认原请求结果。
    publish(runtime / "project-status.json", editor.status("closed"))
    failed, offline = response(mcp, project, runtime, "get_editor_receipt", identity)
    require(not failed and offline == queried, "completed receipt disappeared after editor exit")
    failed, closed = response(mcp, project, runtime, "get_scene")
    require(
        failed and request_path.read_bytes() == original,
        f"closed editor accepted a new command: {closed}",
    )

    # 会话在等待期间改变，旧请求不能被当成新会话的已完成结果。
    changed_runtime = temporary / "changed-runtime"
    changed_runtime.mkdir()
    with EditorFixture(project, changed_runtime) as changed:
        changed_request = changed_runtime / "editor-command.json"
        switched = threading.Event()

        def replace_session():
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if changed_request.exists():
                    with changed.lock:
                        changed.session = "transport-replacement-002"
                    switched.set()
                    return
                time.sleep(0.02)

        switcher = threading.Thread(target=replace_session, daemon=True)
        switcher.start()
        try:
            failed, changed_result = response(mcp, project, changed_runtime, "get_scene")
        finally:
            switcher.join(timeout=6)
        require(
            switched.is_set(), "fixture did not observe the command before replacing its session"
        )
        require(
            failed and changed_result.get("activation") != "completed",
            f"session replacement fabricated command completion: {changed_result}",
        )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mcp", required=True, type=Path)
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="engine-mcp-editor-transport-") as directory:
        test(arguments.mcp.resolve(), Path(directory).resolve())
    print("Editor MCP transport PASS: pending protection, late receipts, project/session isolation")


if __name__ == "__main__":
    main()
