#!/usr/bin/env python3
"""Validate actual MCP, Gameplay and RmlUi interactions in a running showcase editor.

The caller starts the editor; close_editor shuts it down normally after successful checks.
Write receipts only under the project cache, keep temporary TS out of assets and use UI input for Model updates.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path

UNICODE = dict(
    line.split("\t", 1) for line in
    (Path(__file__).resolve().parents[3] / "tests/support/fixtures/unicode.tsv")
    .read_text(encoding="utf-8").splitlines()
)

import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[3]
PROJECT = ROOT / "projects" / "showcase"
sys.path.insert(0, str(ROOT / "scripts"))
from editor_mcp_session import EditorMcpSession


IDENTITY_WRITE = """
import Game from "./game.js";
import { LauncherModel } from "./models/launcher_model.js";
import { showcase_cases } from "./models/showcase_cases.js";
type Probe = { model: LauncherModel; original: number };
type View = { model: LauncherModel; __mcpInteraction?: Probe };
export default function(args: { increment: number }, gameplay: unknown) {
    const view = gameplay as View;
    if (!(gameplay instanceof Game) || !(view.model instanceof LauncherModel) ||
        view.model.choices !== showcase_cases || view.__mcpInteraction)
        throw new Error("MCP did not receive the existing Gameplay and module instances");
    view.__mcpInteraction = { model: view.model, original: view.model.selection_count };
    view.model.selection_count += args.increment;
    return { original: view.__mcpInteraction.original, count: view.model.selection_count,
        sameModule: view.model.choices === showcase_cases };
}
"""

IDENTITY_READ = """
import Game from "./game.js";
import { LauncherModel } from "./models/launcher_model.js";
type View = { model: LauncherModel;
    __mcpInteraction?: { model: LauncherModel; original: number } };
export default function(args: { restore: boolean }, gameplay: unknown) {
    const view = gameplay as View;
    const probe = view.__mcpInteraction;
    if (!(gameplay instanceof Game) || !probe || probe.model !== view.model)
        throw new Error("Gameplay or Model instance changed between MCP calls");
    const result = { count: view.model.selection_count, original: probe.original,
        sameGameplay: true, sameModel: true };
    if (args.restore) {
        view.model.selection_count = probe.original;
        delete view.__mcpInteraction;
    }
    return result;
}
"""

STATE_READ = """
import type { Scene } from "engine";
import type { LauncherModel } from "./models/launcher_model.js";
import type { PersistentUiModel } from "./models/persistent_ui_model.js";
type View = { model: LauncherModel;
    active: { scene: Scene | null; model?: PersistentUiModel } | null };
export default function(_args: unknown, gameplay: unknown) {
    const view = gameplay as View;
    const persistent = view.model.selected?.id === "persistent-ui" ? view.active?.model : null;
    return { selected: view.model.selected?.id ?? null,
        count: view.model.selection_count, error: view.model.error,
        sceneValid: view.active?.scene?.IsValid() ?? false,
        persistent: persistent ? { events: persistent.events, completed: persistent.completed,
            loading: persistent.loading, identity: persistent.identity } : null };
}
"""

ERROR_SCRIPT = """
export default function(_args: unknown, _gameplay: unknown) {
    throw new Error("intentional-mcp-interaction-error");
}
"""


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def tool_payload(result):
    values = []
    for block in result.get("content", []):
        if block.get("type") == "text":
            try:
                value = json.loads(block.get("text", ""))
            except json.JSONDecodeError:
                continue
            if isinstance(value, dict):
                values.append(value)
    require(not result.get("isError") and len(values) == 1,
            f"MCP did not return one successful JSON object: {result}")
    return values[0]


def get_status(session):
    result, _ = session.request_tool("get_status", {})
    status = tool_payload(result)
    session.validate_identity("get_status", status)
    return status


def wait_for(label, operation, predicate, timeout):
    deadline = time.monotonic() + timeout
    while True:
        value = operation()
        if predicate(value):
            return value
        if time.monotonic() >= deadline:
            raise RuntimeError(f"Waiting for {label} timed out; last state: {value}")
        time.sleep(0.1)


def lifecycle(session, command, expected):
    # 生命周期工具可能在 10 秒后返回 pending；只查询原 requestId，禁止重发。
    result, receipt = session.request_tool(command, {})
    require(receipt is not None, f"{command} has no lifecycle receipt: {result}")
    if receipt.get("activation") == "pending":
        request_id = receipt.get("requestId")
        require(isinstance(request_id, str) and request_id, "Lifecycle receipt is missing requestId")
        status = wait_for(command, lambda: get_status(session),
                          lambda item: item.get("lastRequestId") == request_id and
                          item.get("projectState") not in ("starting", "stopping"),
                          session.timeout)
    else:
        session.validate_identity(command, receipt)
        require(not result.get("isError") and receipt.get("activation") == "completed",
                f"{command} did not complete: {receipt}")
        # Completed editor receipts carry operation state under result; status owns the
        # current run/session fields. Read it instead of treating the receipt as status.
        require(receipt.get("success", receipt.get("lastRequestSucceeded")) is True,
                f"{command} returned a failed receipt: {receipt}")
        status = get_status(session)
    require(status.get("lastRequestSucceeded") is True and
            status.get("projectState") == expected,
            f"{command} did not enter {expected}: {status}")
    return get_status(session)


def script_arguments(session, run_id, source, arguments=None, filename="mcp-interaction.ts"):
    return {"session": session.session_id, "runId": run_id,
            "sourcePath": f"scripts/{filename}", "source": source, "arguments": arguments}


def execute(session, run_id, source, arguments=None, filename="mcp-interaction.ts"):
    result = session.call("execute_project_script",
                          script_arguments(session, run_id, source, arguments, filename))
    require(result.get("runId") == run_id and "value" in result,
            f"Script receipt does not belong to the current Run: {result}")
    return result["value"]


def expect_script_failure(session, run_id, source, expected):
    try:
        execute(session, run_id, source)
    except RuntimeError as error:
        require(expected in str(error), f"Script failed for an unexpected reason: {error}")
        return
    raise RuntimeError("Project script expected to fail was accepted")


def ui_element(session, element_id):
    documents = session.call("get_ui_documents")
    matches = []
    for document in documents["documents"]:
        if not document.get("documentId"):
            continue
        offset = 0
        while True:
            tree = session.call("get_ui_tree", {
                "nodeId": document["nodeId"], "documentId": document["documentId"],
                "limit": 512, "depth": 16, "offset": offset,
            })
            matches.extend(item for item in tree["elements"] if item.get("id") == element_id)
            if tree.get("nextOffset") is None:
                break
            offset = tree["nextOffset"]
    require(len(matches) == 1, f"Control {element_id} must be unique; found {len(matches)} matches")
    return matches[0], documents["window"]


def finish_input(session, result):
    job = result.get("job")
    require(isinstance(job, str) and job, f"Input job has no identity: {result}")
    complete = wait_for("actual UI input", lambda: session.call("get_ui_input", {"job": job}),
                        lambda item: item.get("status") != "running", session.timeout)
    require(complete.get("status") == "complete" and
            complete.get("completedEvents") == complete.get("eventCount"),
            f"Actual UI input did not complete: {complete}")
    return complete


def click(session, element_id):
    element, window = ui_element(session, element_id)
    require(window.get("available"), "Editor logical window dimensions are unavailable")
    # 真正滚动菜单，把目标中心移入窗口；不通过修改样式伪造可点击状态。
    for _ in range(40):
        x, y, width, height = element["bounds"]
        center_x, center_y = x + width / 2, y + height / 2
        if 0 <= center_x < window["logicalWidth"] and 0 <= center_y < window["logicalHeight"]:
            require(element.get("visible") and element.get("enabled"),
                    f"Control {element_id} is hidden or disabled: {element}")
            return finish_input(session, session.call("click_ui", {"target": element["handle"]}))
        require(0 <= center_x < window["logicalWidth"], f"Control {element_id} extends beyond the window horizontally")
        direction = -5 if center_y >= window["logicalHeight"] else 5
        finish_input(session, session.call("ui_input", {
            "documentId": element["handle"]["documentId"],
            "events": [{"type": "wheel", "x": window["logicalWidth"] / 2,
                        "y": window["logicalHeight"] / 2, "deltaX": 0, "deltaY": direction}],
        }))
        element = session.call("get_ui_element", {"target": element["handle"]})
    raise RuntimeError(f"Scrolled control {element_id} is still outside the window")


def check(session, results):
    required = {"get_status", "start_project", "stop_project", "execute_project_script",
                "get_ui_documents", "get_ui_tree", "get_ui_element", "click_ui", "ui_input",
                "get_ui_input", "get_editor_receipt", "close_editor"}
    require(required <= session.tools, f"Missing MCP tools: {sorted(required - session.tools)}")
    status = get_status(session)
    require(status.get("editorAlive") is True, "The caller must start the showcase editor first")
    if not status.get("running"):
        status = lifecycle(session, "start_project", "running")
    run_id = status.get("runId")
    require(isinstance(run_id, str) and run_id.isdecimal() and int(run_id) > 0,
            f"Invalid active Run identity: {status}")
    results.update(session=session.session_id, firstRun=run_id)

    write = execute(session, run_id, IDENTITY_WRITE, {"increment": 7})
    read = execute(session, run_id, IDENTITY_READ, {"restore": True}, "mcp-interaction-read.ts")
    require(write["sameModule"] and read["sameGameplay"] and read["sameModel"] and
            read["count"] == write["original"] + 7 and read["original"] == write["original"],
            f"Gameplay/Model identity or values differ across requests: {write}, {read}")
    restored = execute(session, run_id, STATE_READ)
    require(restored["count"] == write["original"], "Temporary Model change was not restored")
    results["sharedInstances"] = read
    print("PASS active Gameplay, Model and loaded modules retain identity across write/read requests; data restored", flush=True)

    expect_script_failure(session, run_id, ERROR_SCRIPT, "intentional-mcp-interaction-error")
    status = get_status(session)
    require(status.get("running") and status.get("runId") == run_id,
            f"One script failure stopped or replaced the Run: {status}")
    require(execute(session, run_id, STATE_READ)["count"] == write["original"],
            "Existing Model cannot be read after a script error")
    print("PASS script error fails only its request; the active Run continues", flush=True)

    stopped = lifecycle(session, "stop_project", "idle")
    require(stopped.get("running") is False and stopped.get("runId") == "0",
            f"Run identity was not cleared after stopping: {stopped}")
    expect_script_failure(session, run_id, STATE_READ, "active Run does not match")
    restarted = lifecycle(session, "start_project", "running")
    new_run = restarted.get("runId")
    require(isinstance(new_run, str) and new_run.isdecimal() and
            int(new_run) > 0 and new_run != run_id, f"Restart did not create a new Run: {restarted}")
    expect_script_failure(session, run_id, STATE_READ, "active Run does not match")
    run_id = new_run
    results["secondRun"] = run_id
    print("PASS stop and restart reject the old runId; the new Run works", flush=True)

    read_state = lambda: execute(session, run_id, STATE_READ)
    initial = read_state()
    require(initial["selected"] is None and initial["error"] is None,
            f"New Run did not enter the menu: {initial}")
    click(session, "scene-cube")
    pbr = wait_for("PBR scene opening", read_state,
                   lambda item: item["selected"] == "pbr-material" and item["sceneValid"],
                   session.timeout)
    require(pbr["error"] is None and pbr["count"] == initial["count"] + 1,
            f"UI click did not update LauncherModel correctly: {pbr}")
    click(session, "back-menu")
    wait_for("return to menu", read_state, lambda item: item["selected"] is None, session.timeout)
    results["pbrClick"] = pbr
    print("PASS actual RmlUi click opens the PBR example and returns to the menu", flush=True)

    click(session, "scene-persistent-ui")
    before = wait_for("persistent UI example opening", read_state,
                      lambda item: item["selected"] == "persistent-ui" and
                      item["persistent"] is not None, session.timeout)
    note, _ = ui_element(session, "lifetime-note")
    original_value = note["value"]
    typed = UNICODE["mcp_input_suffix"]
    handle = note["handle"]
    finish_input(session, session.call("ui_input", {
        "documentId": handle["documentId"],
        "events": [{"type": "focus", "target": handle},
                   {"type": "key_down", "key": "End", "target": handle},
                   {"type": "key_up", "key": "End", "target": handle},
                   {"type": "text", "text": typed, "target": handle}],
    }))
    after = wait_for("actual input callback updates the Model", read_state,
                     lambda item: item["persistent"] is not None and
                     item["persistent"]["events"] > before["persistent"]["events"],
                     session.timeout)
    entered = session.call("get_ui_element", {"target": handle})
    require(entered["value"] == original_value + typed,
            f"Unicode input value does not match: {entered['value']!r}")
    click(session, "load-a")
    loaded = wait_for("persistent UI across scene commit", read_state,
                      lambda item: item["persistent"] is not None and
                      item["persistent"]["completed"] == after["persistent"]["completed"] + 1,
                      session.timeout)
    preserved = session.call("get_ui_element", {"target": handle})
    require(preserved["value"] == entered["value"] and
            loaded["persistent"]["identity"] == before["persistent"]["identity"],
            "Scene commit did not preserve persistent UI text and identity")
    results["persistentInput"] = {"before": before["persistent"],
                                  "after": loaded["persistent"], "value": preserved["value"]}
    click(session, "back-menu")
    wait_for("persistent UI return to menu", read_state,
             lambda item: item["selected"] is None, session.timeout)
    print("PASS actual Unicode input updates Model.events and preserves text and control identity across scenes", flush=True)

    results["closeReceipt"] = session.call("close_editor", {"delayMs": 500})
    closed = wait_for("normal editor shutdown", lambda: get_status(session),
                      lambda item: not item.get("editorAlive", True) and
                      item.get("lastReportedProjectState", item.get("projectState")) == "closed",
                      session.timeout)
    require(closed.get("running") is False and closed.get("runId") == "0",
            f"Active Run remains after shutdown: {closed}")
    results["closedStatus"] = closed
    print("PASS close_editor returns its completion receipt before normal editor shutdown", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True, type=Path, help="Built mcp_app executable")
    parser.add_argument("--runtime-dir", required=True, type=Path,
                        help="Runtime directory of the showcase editor started by the caller")
    parser.add_argument("--timeout", type=float, default=120)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    label = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    output = PROJECT / "cache" / "mcp-interaction" / label
    output.mkdir(parents=True, exist_ok=False)
    results = {"project": str(PROJECT), "runtimeDir": str(args.runtime_dir.resolve()),
               "passed": False}
    virtual_paths = [PROJECT / "assets/scripts" / name for name in
                     ("mcp-interaction.ts", "mcp-interaction-read.ts")]
    try:
        require(not any(path.exists() for path in virtual_paths), "Temporary TS path conflicts with a project source file")
        with EditorMcpSession(args.server, PROJECT, args.runtime_dir, output / "receipts.jsonl",
                              timeout=args.timeout) as session:
            check(session, results)
        require(not any(path.exists() for path in virtual_paths), "Temporary script was unexpectedly written to assets")
        results["passed"] = True
        print(f"PASS actual showcase MCP interactions; receipts and results: {output}")
        return 0
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        results["error"] = str(error)
        print(f"Showcase MCP interaction checks failed: {error}\nPreserved receipts: {output}", file=sys.stderr)
        return 1
    finally:
        (output / "result.json").write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n",
                                           encoding="utf-8")


if __name__ == "__main__":
    raise SystemExit(main())
