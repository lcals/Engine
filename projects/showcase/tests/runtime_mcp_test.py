#!/usr/bin/env python3
"""Exercise temporary scripts and screenshot capture in a standalone test runtime."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import sys
import time
import uuid

from mcp_interaction_test import (PROJECT, EditorMcpSession, capture_for_review, execute,
                                  expect_script_failure, get_status, require, script_arguments,
                                  wait_for)

WRITE = """
import Game from "./game.js";
type View = { model: { selection_count: number }; __runtimeProbe?: number };
export default function(_args: unknown, gameplay: unknown) {
    const view = gameplay as View;
    if (!(gameplay instanceof Game) || view.__runtimeProbe !== undefined)
        throw new Error("Runtime did not supply the active Gameplay instance");
    view.__runtimeProbe = view.model.selection_count;
    view.model.selection_count += 17;
    return { previous: view.__runtimeProbe, current: view.model.selection_count };
}
"""

READ = """
import Game from "./game.js";
type View = { model: { selection_count: number }; __runtimeProbe?: number };
export default function(_args: unknown, gameplay: unknown) {
    const view = gameplay as View;
    if (!(gameplay instanceof Game) || view.__runtimeProbe === undefined)
        throw new Error("Runtime Gameplay identity changed between calls");
    const result = { previous: view.__runtimeProbe, current: view.model.selection_count };
    view.model.selection_count = view.__runtimeProbe;
    delete view.__runtimeProbe;
    return result;
}
"""

QUIT = """
import { App } from "engine";
export default function() { return { requested: App.RequestQuit() }; }
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path,
                        help="Exported MCP test executable with its assets beside it")
    parser.add_argument("--server", required=True, type=Path, help="Built mcp_app executable")
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    executable = args.executable.resolve()
    label = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    output = PROJECT / "cache" / "runtime-mcp" / label
    output.mkdir(parents=True, exist_ok=False)
    channel = output / "channel"
    channel.mkdir()
    report = {"passed": False, "project": str(PROJECT), "channel": str(channel),
              "executable": str(executable)}
    log = (output / "runtime.log").open("wb")
    process = subprocess.Popen([str(executable), "--mcp-project", str(PROJECT),
                                "--mcp-runtime-dir", str(channel)], stdout=log,
                               stderr=subprocess.STDOUT)
    try:
        with EditorMcpSession(args.server, PROJECT, channel, output / "receipts.jsonl",
                              timeout=args.timeout) as session:
            def runtime_status():
                if process.poll() is not None:
                    raise RuntimeError(f"Runtime exited during startup: {process.returncode}")
                if not (channel / "project-status.json").exists():
                    return {}
                return get_status(session)

            status = wait_for("standalone runtime", runtime_status,
                              lambda item: item.get("runtimeAlive") and item.get("running"),
                              args.timeout)
            require(status.get("hostKind") == "runtime-test" and not status.get("editorAlive"),
                    f"MCP connected to the wrong host: {status}")
            run = status["runId"]
            report.update(session=session.session_id, runId=run, activeRevision=status.get("activeRevision"))
            written = execute(session, run, WRITE, filename="mcp-runtime-write.ts")
            read = execute(session, run, READ, filename="mcp-runtime-read.ts")
            require(read == written and read["current"] == read["previous"] + 17,
                    f"Temporary scripts did not share and restore the active Gameplay: {written}, {read}")
            report["sharedGameplay"] = read

            expect_script_failure(session, str(int(run) + 1), READ, "active Run does not match")
            wrong_session = script_arguments(session, run, READ, filename="mcp-runtime-old.ts")
            wrong_session["session"] = "stale-runtime-session"
            result, _ = session.request_tool("execute_project_script", wrong_session)
            require(result.get("isError"), "A stale runtime session was accepted")
            try:
                session.call("start_project")
            except RuntimeError as error:
                require("unavailable in the runtime test target" in str(error),
                        f"Editor command failed for an unexpected reason: {error}")
            else:
                raise RuntimeError("Editor-only command was accepted by the runtime target")
            report["invalidIdentitiesRejected"] = True
            prepared = wait_for("runtime rendering preparation", lambda: get_status(session),
                                lambda value: value.get("renderPreparation", {}).get("state") == "ready",
                                args.timeout)
            report["renderPreparation"] = prepared["renderPreparation"]
            capture_for_review(session, report, output, "runtime-frame")
            require(not report.get("invalidCaptures"), f"Invalid runtime capture: {report}")
            stopped = execute(session, run, QUIT, filename="mcp-runtime-quit.ts")
            require(stopped.get("requested") is True, f"Runtime did not accept quit: {stopped}")
            require(process.wait(timeout=20) == 0, "Runtime did not exit normally")
            closed = get_status(session)
            require(not closed.get("runtimeAlive", True) and closed.get("runId") == "0",
                    f"Runtime status remained active after exit: {closed}")
            report["closedStatus"] = closed
        report["passed"] = True
        print(f"PASS standalone runtime MCP injection and screenshot: {output}", flush=True)
        return 0
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired) as error:
        report["error"] = str(error)
        print(f"Standalone runtime MCP check failed: {error}\nRecords: {output}", file=sys.stderr)
        return 1
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        log.close()
        report["exit"] = process.returncode
        (output / "result.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                                            encoding="utf-8")


if __name__ == "__main__":
    raise SystemExit(main())
