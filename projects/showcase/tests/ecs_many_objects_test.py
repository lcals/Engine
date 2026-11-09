#!/usr/bin/env python3
"""Validate ECS identities, hierarchy and cleanup through the actual editor MCP.

Connect to an imported showcase project, navigate through normal UI, rebuild and return.
Keep the editor open and write receipts only to the local cache; do not collect performance scores.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import sys
import uuid

from mcp_interaction_test import (PROJECT, EditorMcpSession, click, execute, get_status,
                                  lifecycle, require, wait_for)

READ = """
import EcsManyObjects from "./modules/ecs-many-objects.js";
export default function(_args: unknown, gameplay: unknown) {
    const menu = gameplay as { active: EcsManyObjects | null; model: { selected: {id: string} | null } };
    return { selected: menu.model.selected?.id ?? null,
        ecs: menu.active instanceof EcsManyObjects ? menu.active.Inspect() : null };
}
"""


def ready(session, run, count, previous_checks=-1):
    def read():
        value = execute(session, run, READ, None, "mcp-ecs-many-objects.ts")
        require(value["selected"] == "ecs-many-objects" and value["ecs"], "ECS example is not open")
        state = value["ecs"]
        require(state["phase"] != "failed", f"ECS entity validation failed: {state}")
        return state
    state = wait_for("ECS entity creation and validation", read,
        lambda s: s["phase"] == "ready" and s["pending"] is None and
                  s["requested"] == count and s["checks"] > previous_checks, session.timeout)
    require(state["created"] == count and state["checked"] == count and state["sceneValid"],
            f"Incorrect entity count or scene identity: {state}")
    scene = session.call("get_scene", {"offset": 0, "limit": 0})
    require(scene["totalNodes"] == count + 5, f"Scene has missing or extra entities: {scene}")
    return state


def check(session, results, count, output):
    status = get_status(session)
    require(status.get("editorAlive") is True, "Start the showcase editor and finish asset import first")
    if not status.get("running"):
        status = lifecycle(session, "start_project", "running")
    run = status["runId"]
    current = execute(session, run, READ, None, "mcp-ecs-many-objects.ts")
    require(current["selected"] is None, "Return to the showcase menu before running ECS checks")
    click(session, "scene-ecs")
    state = ready(session, run, 10000)
    if count != 10000:
        click(session, f"ecs-{count}")
        state = ready(session, run, count, state["checks"])
    results["initial"] = state
    results["capture"] = session.capture(str((output / "many-objects.png").relative_to(PROJECT / "cache")))
    require(state["visible"] == count and state["active"] == count, "Incorrect initial entity state")
    for action, expected_visible, expected_active in [
        ("move", count, count),
        ("visibility", count // 2, count),
        ("activation", count // 2, count // 2),
        ("reparent", count // 2 + 1, count // 2 + 1),
        ("recreate", count // 2, count // 2),
        ("visibility", count, count // 2),
        ("activation", count, count),
    ]:
        click(session, f"ecs-{action}")
        state = ready(session, run, count, state["checks"])
        require(state["visible"] == expected_visible and state["active"] == expected_active,
                f"{action} has incorrect inherited state: {state}")
        results.setdefault("actions", []).append({"action": action, "state": state})
    require(state["stale_handles"] >= 3, "Old handle invalidation was not checked after recreation")
    click(session, "ecs-1000")
    state = ready(session, run, 1000, state["checks"])
    require(state["stale_handles"] >= 27, "Old entity and component handles were not checked after group cleanup")
    results["recreated"] = state
    click(session, "back-menu")
    menu = wait_for("return to menu", lambda: execute(session, run, READ, None, "mcp-ecs-many-objects.ts"),
                    lambda item: item["selected"] is None and item["ecs"] is None, session.timeout)
    results["menu"] = menu
    click(session, "scene-ecs")
    reopened = ready(session, run, 10000)
    require(reopened["generations"] == 1 and reopened["visible"] == 10000 and
            reopened["active"] == 10000, f"Reentry retained state from the previous run: {reopened}")
    results["reopened"] = reopened
    click(session, "back-menu")
    wait_for("return to menu again", lambda: execute(session, run, READ, None, "mcp-ecs-many-objects.ts"),
             lambda item: item["selected"] is None and item["ecs"] is None, session.timeout)
    print(f"PASS {count} actual ECS objects: components, movement, hierarchy visibility/activation, reparenting, recreation, group cleanup and return to menu", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True, type=Path)
    parser.add_argument("--runtime-dir", required=True, type=Path)
    parser.add_argument("--count", type=int, choices=(1000, 10000, 50000), default=10000)
    parser.add_argument("--timeout", type=float, default=600)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    label = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    output = PROJECT / "cache" / "ecs-many-objects" / label
    output.mkdir(parents=True, exist_ok=False)
    results = {"passed": False, "count": args.count}
    try:
        with EditorMcpSession(args.server, PROJECT, args.runtime_dir, output / "receipts.jsonl",
                              timeout=args.timeout) as session:
            check(session, results, args.count, output)
        results["passed"] = True
        print(f"Receipts and functional results: {output}")
        return 0
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        results["error"] = str(error)
        print(f"ECS functional checks failed: {error}\nPreserved receipts: {output}", file=sys.stderr)
        return 1
    finally:
        (output / "result.json").write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    raise SystemExit(main())
