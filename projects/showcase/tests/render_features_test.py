#!/usr/bin/env python3
"""Validate standalone render cases through actual MCP, RmlUi and TS without performance scores.

Connect to an imported showcase editor without starting or closing it.
Use 384 fixed objects and same-camera submission counts; keep captures and receipts in the project cache.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import sys
import uuid

from mcp_interaction_test import (PROJECT, EditorMcpSession, capture_for_review, click, execute,
                                  get_status, lifecycle, require, wait_for)


READ = """
import RenderFeatures from "./modules/render-features.js";
export default function(_args: unknown, gameplay: unknown) {
    const menu = gameplay as { active: RenderFeatures | null;
        model: { selected: {id: string} | null; error: string | null } };
    return { selected: menu.model.selected?.id ?? null, error: menu.model.error,
        rendering: menu.active instanceof RenderFeatures ? menu.active.Inspect() : null };
}
"""


def expect_failure(session, name, arguments):
    try:
        session.call(name, arguments)
    except RuntimeError:
        return
    raise RuntimeError(f"Invalid request {name} was accepted: {arguments}")


def read_lab(session, run):
    value = execute(session, run, READ, None, "mcp-render-features.ts")
    require(value["error"] is None, f"Render case startup failed: {value}")
    return value


def ready(session, run):
    value = wait_for("standalone render case", lambda: read_lab(session, run),
        lambda item: item["selected"] == "render-features" and item["rendering"] is not None and
                     item["rendering"]["objects"] == 384,
        session.timeout)
    return value["rendering"]


def states(session):
    result = session.call("get_render_features")["renderer"]
    require(result["scope"] == "world_summary", "Render summary must state the actual world submission scope")
    return result["features"]


def wait_feature(session, feature, enabled, after=0):
    def read():
        result = session.call("get_render_features")
        return {"frame": result["renderer"]["frameNumber"],
                "state": result["renderer"]["features"][feature]}
    return wait_for(f"{feature} actual rendering state", read,
        lambda item: item["frame"] > after and item["state"]["requested"] == enabled and
                     item["state"]["effective"] == enabled,
        session.timeout)["state"]


def main_stats(session, after=0):
    result = wait_for("new main-view submission", lambda: session.call("get_render_submission_stats"),
        lambda item: item["frameNumber"] > after and
                     any(row["viewId"] == 113 and row["candidateMeshes"] >= 384
                         for row in item["views"]), session.timeout)
    require(result["source"] == "scene_mesh_submission" and
            result["gpuCompletion"] == "not_measured", "Submission counts must not be presented as GPU timing or completion")
    return next(row for row in result["views"] if row["viewId"] == 113)


def selected_ground(session):
    summary = session.call("get_scene", {"limit": 0})
    count = summary["totalNodes"]
    # Ground is appended after the 384 spheres. Read a bounded page, not a >1 MiB scene receipt.
    result = session.call("get_scene", {"offset": max(0, count - 16), "limit": 16})
    nodes = [node for node in result["scene"]["nodes"] if node["name"] == "Hex surface"]
    require(len(nodes) == 1, "Expected one Hex material test floor")
    return {"sceneId": result["scene"]["sceneId"], "nodeId": nodes[0]["nodeId"], "slot": 0}


def wait_scoped(session, scope, feature, enabled, identity=None):
    return wait_for(f"{scope}/{feature} actual value", lambda: session.call("get_render_features", identity or {}),
        lambda item: item[scope]["features"][feature]["requested"] == enabled and
                     (scope == "material" or item[scope]["features"][feature]["effective"] == enabled),
        session.timeout)[scope]


def check(session, results, output):
    status = get_status(session)
    require(status.get("editorAlive") is True, "Start the showcase editor and finish asset import first")
    if not status.get("running"):
        status = lifecycle(session, "start_project", "running")
    run = status["runId"]
    require(read_lab(session, run)["selected"] is None, "Return to the showcase menu before render checks")
    previous = {name: state["requested"] for name, state in states(session).items()}
    click(session, "scene-render")
    results["initial"] = ready(session, run)
    results["baseline"] = main_stats(session)
    require(results["baseline"]["culledMeshes"] > 0 and
            results["baseline"]["instanceBatches"] > 0,
            "Fixed scene does not actually use frustum culling and static instancing")
    capture_for_review(session, results, output, "initial")
    lod = session.call("get_mesh_lod_import", {"path": "meshes/render-lod-sphere.mesh"})
    require(lod["settings"]["enabled"] and len(lod["meshes"]) == 1, "LOD does not use imported resources")
    levels = lod["meshes"][0]["levels"]
    require(len(levels) >= 2 and all(levels[index]["triangleCount"] > levels[index + 1]["triangleCount"]
                                     for index in range(len(levels) - 1)), f"Imported LOD did not reduce geometry: {lod}")
    results["importedLods"] = lod

    for feature in ("frustum_culling", "static_instancing", "static_lod"):
        before = main_stats(session)
        click(session, f"render-{feature}")
        wait_feature(session, feature, False, before["frameNumber"])
        disabled = main_stats(session, before["frameNumber"])
        require(ready(session, run)["objects"] == 384, f"{feature} changed the scene object count")
        if feature == "frustum_culling":
            require(disabled["culledMeshes"] == 0 and disabled["visibleMeshes"] >= before["visibleMeshes"],
                    "Objects were still culled after culling was disabled")
        elif feature == "static_instancing":
            require(disabled["instanceBatches"] == 0 and disabled["drawCalls"] > before["drawCalls"],
                    "Disabling instancing did not restore individual draw submissions")
        else:
            require(disabled["triangles"] > before["triangles"], "Disabling LOD did not restore LOD0 topology")
        click(session, f"render-{feature}")
        wait_feature(session, feature, True, disabled["frameNumber"])
        results.setdefault("pathComparisons", {})[feature] = {"enabled": before, "disabled": disabled}

    # Change camera through the normal View/Controller route, then compare LOD on/off at that same camera.
    click(session, "render-camera")
    wait_for("distant viewing camera", lambda: ready(session, run), lambda item: item["distant"], session.timeout)
    far_on = main_stats(session)
    click(session, "render-static_lod")
    wait_feature(session, "static_lod", False, far_on["frameNumber"])
    far_off = main_stats(session, far_on["frameNumber"])
    require(far_off["triangles"] > far_on["triangles"], "Toggling LOD from the distant view did not change the triangle count")
    click(session, "render-static_lod")
    wait_feature(session, "static_lod", True, far_off["frameNumber"])
    click(session, "render-camera")
    wait_for("restore original camera", lambda: ready(session, run), lambda item: not item["distant"], session.timeout)
    results["farCameraLod"] = {"enabled": far_on, "disabled": far_off}

    for feature in ("wireframe", "mesh_bounds", "camera_frusta", "stats"):
        original = states(session)[feature]
        require(original["supported"], f"Current device does not support {feature}; this check cannot be reported as complete")
        session.call("set_render_feature", {"feature": feature, "enabled": not original["requested"]})
        changed = wait_feature(session, feature, not original["requested"])
        if feature == "wireframe":
            capture_for_review(session, results, output, "wireframe")
        session.call("set_render_feature", {"feature": feature, "enabled": original["requested"]})
        wait_feature(session, feature, original["requested"])
        results.setdefault("mcpDebug", {})[feature] = changed

    session.call("set_render_feature", {"feature": "debug_draw", "enabled": False})
    wait_feature(session, "debug_draw", False)
    line = {"start": [0, 1, 0], "end": [0, 5, 0], "color": 4294902015, "depthTest": True}
    expect_failure(session, "draw_debug_lines", {"lines": [line]})
    session.call("set_render_feature", {"feature": "debug_draw", "enabled": True})
    require(states(session)["debug_draw"]["requested"], "Debug drawing request was not accepted")
    results["debugLines"] = session.call("draw_debug_lines", {"lines": [line], "lifetimeFrames": 600})
    require(results["debugLines"]["acceptedLines"] == 1, "Debug lines were not actually accepted")
    wait_feature(session, "debug_draw", True)
    capture_for_review(session, results, output, "debug-lines")

    click(session, "render-atmosphere")
    wait_for("UI sky disabled", lambda: ready(session, run), lambda item: not item["atmosphere"], session.timeout)
    wait_scoped(session, "scene", "atmosphere", False)
    capture_for_review(session, results, output, "sky-off")
    click(session, "render-atmosphere")
    environment = wait_scoped(session, "scene", "atmosphere", True)
    env_identity = {key: environment[key] for key in ("sceneId", "nodeId")}
    for enabled in (False, True):
        session.call("set_render_feature", {"feature": "atmosphere", "enabled": enabled, **env_identity})
        wait_scoped(session, "scene", "atmosphere", enabled)

    ground = selected_ground(session)
    click(session, "render-hex")
    wait_for("UI Hex enabled", lambda: ready(session, run), lambda item: item["hex"], session.timeout)
    wait_scoped(session, "material", "hex_tiling", True, ground)
    capture_for_review(session, results, output, "hex-on")
    click(session, "render-hex")
    wait_scoped(session, "material", "hex_tiling", False, ground)
    for enabled in (True, False):
        session.call("set_render_feature", {"feature": "hex_tiling", "enabled": enabled, **ground})
        wait_scoped(session, "material", "hex_tiling", enabled, ground)

    # A per-object edit cannot turn off other objects; native readback remains separate from scene summaries.
    original_ground = session.call("get_mesh_render_settings", {key: ground[key] for key in ("sceneId", "nodeId")})
    for feature, field in (("static_instancing", "staticInstancingEnabled"), ("static_lod", "staticLodEnabled")):
        identity = {key: ground[key] for key in ("sceneId", "nodeId")}
        session.call("set_render_feature", {"feature": feature, "enabled": False, **identity})
        require(session.call("get_mesh_render_settings", identity)[field] is False,
                f"{feature} did not update the selected MeshRenderer")
        require(states(session)[feature]["requested"], "A per-object property incorrectly changed global policy for other objects")
        session.call("undo", {"sceneId": ground["sceneId"]})
        require(session.call("get_mesh_render_settings", identity)[field] == original_ground[field], "Undo did not restore component properties")
        session.call("redo", {"sceneId": ground["sceneId"]})
        require(session.call("get_mesh_render_settings", identity)[field] is False, "Redo did not restore this object's property change")
        session.call("undo", {"sceneId": ground["sceneId"]})
    camera = session.call("get_render_features")["camera"]
    camera_identity = {key: camera[key] for key in ("sceneId", "nodeId")}
    session.call("set_render_feature", {"feature": "frustum_culling", "enabled": False, **camera_identity})
    require(session.call("get_render_features", camera_identity)["camera"]["frustumCullingEnabled"] is False,
            "Unified culling entry did not edit Camera properties")
    session.call("undo", {"sceneId": camera["sceneId"]})
    require(session.call("get_render_features", camera_identity)["camera"]["frustumCullingEnabled"] is True,
            "Camera property undo failed")
    sun = session.call("get_render_settings")["atmosphereSunNodeId"]
    require(isinstance(sun, str) and sun != "0", "Environment query did not return the actual sun Node identity")
    session.call("set_render_settings", {**env_identity, "atmosphereSunNodeId": sun})
    require(session.call("get_render_settings")["atmosphereSunNodeId"] == sun, "Incorrect sun Node/local ID conversion")
    expect_failure(session, "set_render_settings", {**env_identity, "atmosphereSunNodeId": ground["nodeId"]})
    for feature in ("frustum_culling", "static_instancing", "static_lod"):
        expect_failure(session, "set_render_feature", {"feature": feature, "enabled": False})

    session.call("set_render_feature", {"feature": "mesh_bounds", "enabled": True})
    require(wait_feature(session, "mesh_bounds", True)["requested"], "Unified renderer toggle was not applied")
    session.call("set_render_feature", {"feature": "mesh_bounds", "enabled": False})
    wait_feature(session, "mesh_bounds", False)
    expect_failure(session, "set_render_feature", {"feature": "frame_view_uniforms", "enabled": False})
    expect_failure(session, "set_render_feature", {"feature": "hex_tiling", "enabled": True, **ground, "slot": 65535})
    expect_failure(session, "set_render_feature", {"feature": "atmosphere", "enabled": False,
                   "sceneId": ground["sceneId"], "nodeId": ground["nodeId"]})
    require(wait_scoped(session, "scene", "atmosphere", True), "Invalid target changed the actual environment")

    for feature in ("wireframe", "debug_draw", "mesh_bounds", "camera_frusta", "stats"):
        session.call("set_render_feature", {"feature": feature, "enabled": previous[feature]})
    click(session, "back-menu")
    results["returned"] = wait_for("return-to-menu cleanup", lambda: read_lab(session, run),
        lambda item: item["selected"] is None and item["rendering"] is None, session.timeout)
    require({name: state["requested"] for name, state in states(session).items()} == previous,
            "Returning to the menu did not restore earlier render requests")
    remaining = session.call("get_scene", {"limit": 0})
    require(remaining["scene"] is None and remaining["totalNodes"] == 0, "Render scene objects remain after returning to the menu")
    # Reopen checks that scene material/environment changes and temporary debug work were not saved.
    click(session, "scene-render")
    reopened = ready(session, run)
    require(reopened["objects"] == 384 and reopened["atmosphere"] and not reopened["hex"] and
            not reopened["distant"], "Reentry inherited temporary edits from the previous run")
    results["reopened"] = reopened
    click(session, "back-menu")
    wait_for("return to menu again", lambda: read_lab(session, run),
        lambda item: item["selected"] is None and item["rendering"] is None, session.timeout)
    print("PASS 384 fixed objects: actual UI/MCP toggles, imported LOD, per-view submissions, debug drawing, sky/Hex and menu cleanup", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True, type=Path)
    parser.add_argument("--runtime-dir", required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    label = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    output = PROJECT / "cache" / "render-features" / label
    output.mkdir(parents=True, exist_ok=False)
    results = {"passed": False, "objects": 384, "timingsCollected": False}
    try:
        with EditorMcpSession(args.server, PROJECT, args.runtime_dir, output / "receipts.jsonl",
                              timeout=args.timeout) as session:
            check(session, results, output)
        results["functionalChecksPassed"] = True
        require(not results.get("invalidCaptures"),
                f"Screenshots are black or could not be inspected: {results.get('invalidCaptures')}")
        results["passed"] = True
        print(f"Receipts, submission counts and captures: {output}")
        return 0
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        results["error"] = str(error)
        print(f"Render functional checks failed: {error}\nPreserved receipts: {output}", file=sys.stderr)
        return 1
    finally:
        (output / "result.json").write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    raise SystemExit(main())
