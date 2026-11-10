#!/usr/bin/env python3
"""Capture each showcase menu case from a separate Release editor session.

The menu's registered cases are the source of truth. Screenshots and MCP receipts
stay in the project's ignored cache; this is a visual record, not a functional test.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[3]
PROJECT = ROOT / "projects" / "showcase"
sys.path.insert(0, str(PROJECT / "tests"))
from mcp_interaction_test import (EditorMcpSession, click, execute, get_status,
                                  require, wait_for)


READ = """
import { showcase_cases } from "./models/showcase_cases.js";
export default function(_args: unknown, gameplay: unknown) {
    const view = gameplay as any;
    const selected = view.model.selected?.id ?? null;
    return {
        cases: showcase_cases.map(item => ({id: item.id, button: item.button,
                                            title: item.title, scene: item.scene})),
        selected,
        pending: view.model.pending !== null,
        error: view.model.error,
        sceneValid: view.active?.scene?.IsValid() ?? false,
        ecs: selected === "ecs-many-objects" ? view.active?.Inspect?.() ?? null : null,
    };
}
"""


def git_revision():
    result = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True,
                            capture_output=True, check=False)
    return result.stdout.strip() if result.returncode == 0 else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", action="append", default=[], help="Capture only this registered case ID")
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args()
    run_name = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    output = PROJECT / "cache" / "scene-captures" / run_name
    output.mkdir(parents=True)
    runtime = output / "runtime"
    runtime.mkdir()
    report_path = output / "report.json"
    report = {"complete": False, "kind": "scene captures, not functional test results",
              "startedUtc": datetime.now(timezone.utc).isoformat(), "engineRevision": git_revision(),
              "platform": platform.platform(), "project": str(PROJECT), "runtime": str(runtime),
              "requestedCases": args.case, "cases": []}

    def save():
        report_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    release = ROOT / "build" / "local-ninja-release"
    log = (output / "editor.log").open("wb")
    editor = subprocess.Popen([str(release / "editor_app"), "--project", str(PROJECT),
                               "--runtime-dir", str(runtime), "--width", "960", "--height", "540"],
                              stdout=log, stderr=subprocess.STDOUT)
    try:
        with EditorMcpSession(release / "mcp_app", PROJECT, runtime, output / "receipts.jsonl",
                              timeout=args.timeout) as session:
            deadline = time.monotonic() + args.timeout
            while True:
                if editor.poll() is not None:
                    raise RuntimeError(f"Editor exited during startup: {editor.returncode}")
                try:
                    status = get_status(session)
                    if status.get("editorAlive"):
                        break
                except RuntimeError:
                    if time.monotonic() >= deadline:
                        raise
                if time.monotonic() >= deadline:
                    raise RuntimeError("Editor did not become available")
                time.sleep(0.25)
            session.call("start_project")
            status = wait_for("project startup", lambda: get_status(session),
                              lambda value: value.get("projectState") not in ("starting", "stopping"),
                              args.timeout)
            require(status.get("running"), f"Project did not start: {status}")
            run = status["runId"]
            report.update(session=session.session_id, runId=run,
                          activeRevision=status.get("activeRevision"))
            read = lambda: execute(session, run, READ, filename="mcp-scene-capture.ts")
            initial = wait_for("showcase menu", read,
                               lambda value: not value["pending"] and value["selected"] is None,
                               args.timeout)
            cases = initial["cases"]
            if args.case:
                missing = set(args.case) - {case["id"] for case in cases}
                require(not missing, f"Unknown case IDs: {sorted(missing)}")
                cases = [case for case in cases if case["id"] in args.case]
            save()
            for case in cases:
                entry = {**case, "status": "running"}
                report["cases"].append(entry)
                save()
                try:
                    click(session, case["button"])
                    state = wait_for(case["id"], read,
                                     lambda value: value["selected"] == case["id"] and
                                     value["sceneValid"] and not value["pending"] and
                                     (case["id"] != "ecs-many-objects" or
                                      (value["ecs"] is not None and value["ecs"].get("phase") == "ready")),
                                     args.timeout)
                    require(state["error"] is None, f"Scene reported an error: {state['error']}")
                    time.sleep(1)
                    entry["settings"] = session.call("get_render_settings")
                    scene = session.call("get_scene", {"limit": 0})
                    entry["totalNodes"] = scene.get("totalNodes")
                    entry["sceneId"] = (scene.get("scene") or {}).get("sceneId")
                    relative = (output / f"{case['id']}.png").relative_to(PROJECT / "cache")
                    attempts = session.capture_when_visible(str(relative))
                    entry["captureAttempts"] = attempts
                    entry["capture"] = attempts[-1]
                    entry["capture"]["sha256"] = hashlib.sha256(
                        Path(entry["capture"]["path"]).read_bytes()).hexdigest()
                    entry["capturedUtc"] = datetime.now(timezone.utc).isoformat()
                    require(entry["capture"]["opaqueBlack"] is False,
                            "Captured frame is fully black or its pixels could not be inspected")
                    entry["status"] = "captured"
                    print(f"CAPTURED {case['id']}: {entry['capture']['path']}", flush=True)
                    click(session, "back-menu")
                    wait_for("return to menu", read,
                             lambda value: value["selected"] is None and not value["pending"],
                             args.timeout)
                except Exception as error:
                    entry.update(status="failed", error=str(error))
                    print(f"FAILED {case['id']}: {error}", flush=True)
                    if "capture" not in entry:
                        try:
                            relative = (output / f"{case['id']}-failure.png").relative_to(
                                PROJECT / "cache")
                            entry["failureCapture"] = session.capture(str(relative), inspect_black=True)
                        except Exception as capture_error:
                            entry["failureCaptureError"] = str(capture_error)
                    try:
                        if read()["selected"] is not None:
                            click(session, "back-menu")
                            wait_for("return to menu", read,
                                     lambda value: value["selected"] is None and not value["pending"],
                                     args.timeout)
                    except Exception as recovery:
                        entry["recoveryError"] = str(recovery)
                        save()
                        break
                finally:
                    save()
            report["complete"] = len(report["cases"]) == len(cases) and all(
                entry["status"] == "captured" for entry in report["cases"])
            report["close"] = session.call("close_editor", {"delayMs": 500})
            report["exit"] = editor.wait(timeout=15)
    except Exception as error:
        report["error"] = str(error)
        print(f"FAILED capture run: {error}", flush=True)
    finally:
        if editor.poll() is None:
            editor.terminate()
            try:
                editor.wait(timeout=5)
            except subprocess.TimeoutExpired:
                editor.kill()
                editor.wait()
        log.close()
        report["finishedUtc"] = datetime.now(timezone.utc).isoformat()
        save()
        print(json.dumps({"report": str(report_path), "complete": report["complete"]}), flush=True)
    return 0 if report["complete"] else 1


if __name__ == "__main__":
    sys.exit(main())
