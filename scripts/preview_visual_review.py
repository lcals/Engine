#!/usr/bin/env python3
"""Move the editor preview camera along a fixed route and record actual captures, settings and an offline comparison report."""

import argparse
import base64
import hashlib
import html
import json
import math
from pathlib import Path, PurePosixPath
import re
import struct
import sys
import time
import tomllib
import zlib

from editor_mcp_session import EditorMcpSession, utc_now
from resource_catalog import safe_relative


def file_hash(path):
    with Path(path).open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def write_manifest(path, manifest):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def finite_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def read_route(path):
    route = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(route, dict):
        raise ValueError("Route must be a JSON object")
    scene = route.get("scene", "")
    if not isinstance(scene, str) or not scene.endswith(".scene"):
        raise ValueError("Route scene must be a scene resource path relative to assets")
    safe_relative(scene)
    rate = route.get("steps_per_second", 12)
    if not finite_number(rate) or not 1 <= rate <= 60:
        raise ValueError("steps_per_second must be within [1, 60]")
    route["steps_per_second"] = rate
    points = route.get("waypoints")
    if not isinstance(points, list) or not points:
        raise ValueError("Route requires at least one waypoint")
    names = set()
    for point in points:
        if not isinstance(point, dict):
            raise ValueError("Waypoint must be a JSON object")
        name = point.get("name", "")
        if not isinstance(name, str) or not re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,63}", name):
            raise ValueError("Waypoint name must use lowercase English letters, digits, hyphens or underscores, with at most 64 characters")
        if name in names:
            raise ValueError(f"Duplicate waypoint name: {name}")
        names.add(name)
        capture = point.get("capture", True)
        if not isinstance(capture, bool):
            raise ValueError(f"Waypoint {name} capture must be a boolean")
        point["capture"] = capture
        for key in ("position", "rotation"):
            vector = point.get(key)
            if not isinstance(vector, list) or len(vector) != 3 or not all(map(finite_number, vector)):
                raise ValueError(f"Waypoint {name} field {key} must contain three finite numbers")
        for key, default in (("travel_seconds", 2), ("settle_seconds", 1)):
            value = point.get(key, default)
            if not finite_number(value) or not 0 <= value <= 300:
                raise ValueError(f"Waypoint {name} field {key} must be within [0, 300]")
            point[key] = value
    if not any(point["capture"] for point in points):
        raise ValueError("Route requires at least one capture waypoint")
    return route


def source_camera(source):
    with source.open("rb") as stream:
        scene = tomllib.load(stream)
    camera_node = str(scene.get("active_camera", ""))
    for node in scene.get("nodes", []):
        if str(node.get("id")) == camera_node:
            return {"node": camera_node, "components": [component for component in
                    node.get("components", []) if component.get("type") == "camera"]}
    return {"node": camera_node, "components": []}


def camera_pose(point):
    return {key: point[key] for key in ("position", "rotation")}


def verify_pose(actual, expected):
    for key in ("position", "rotation"):
        vector = actual.get(key, [])
        if len(vector) != 3:
            raise RuntimeError("Preview receipt is missing the complete camera pose")
        for index, value in enumerate(vector):
            delta = value - expected[key][index]
            if key == "rotation":
                delta = (delta + 180) % 360 - 180
            if abs(delta) > 0.001:
                raise RuntimeError("Actual camera does not match the fixed waypoint; check for other input moving the camera")


def move_camera(session, start, target, rate):
    duration = target["travel_seconds"]
    steps = max(1, math.ceil(duration * rate))
    beginning = time.monotonic()
    result = None
    for step in range(1, steps + 1):
        ratio = step / steps
        # 平滑起停；旋转沿每轴最短角度移动，避免跨越 ±180 度时绕远路。
        weight = ratio * ratio * (3 - 2 * ratio)
        pose = {"position": [], "rotation": []}
        for index in range(3):
            pose["position"].append(start["position"][index] + weight * (
                target["position"][index] - start["position"][index]))
            delta = (target["rotation"][index] - start["rotation"][index] + 180) % 360 - 180
            pose["rotation"].append(start["rotation"][index] + weight * delta)
        # 末帧使用原始角度，保证报告中的固定机位可直接复用。
        if step == steps:
            pose = camera_pose(target)
        wait = beginning + duration * ratio - time.monotonic()
        if wait > 0:
            time.sleep(wait)
        result = session.call("set_preview_camera", pose)
    return result, {"steps": steps, "requested_seconds": duration,
                    "elapsed_seconds": round(time.monotonic() - beginning, 3)}


def compressed_png(path):
    """Recompress only IDAT, preserving filtered pixel data and other PNG chunks without rewriting original captures."""
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Report capture is not a PNG file")
    chunks = []
    image_data = bytearray()
    offset = 8
    while offset < len(data):
        if offset + 12 > len(data):
            raise ValueError("Report PNG chunk is incomplete")
        size = struct.unpack_from(">I", data, offset)[0]
        end = offset + 12 + size
        if end > len(data):
            raise ValueError("Report PNG data is incomplete")
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + size]
        if zlib.crc32(kind + payload) != struct.unpack_from(">I", data, end - 4)[0]:
            raise ValueError("Report PNG checksum failed")
        chunks.append((kind, data[offset:end]))
        if kind == b"IDAT":
            image_data.extend(payload)
        offset = end
    if not image_data or not chunks or chunks[-1][0] != b"IEND":
        raise ValueError("Report PNG is missing image data or its end chunk")
    compressed = zlib.compress(zlib.decompress(image_data), level=6)
    replacement = (struct.pack(">I", len(compressed)) + b"IDAT" + compressed
                   + struct.pack(">I", zlib.crc32(b"IDAT" + compressed)))
    result = bytearray(data[:8])
    inserted = False
    for kind, chunk in chunks:
        if kind == b"IDAT":
            if not inserted:
                result.extend(replacement)
                inserted = True
        else:
            result.extend(chunk)
    return bytes(result)


def embedded_png(path):
    return "data:image/png;base64," + base64.b64encode(compressed_png(path)).decode("ascii")


def render_parameters(settings):
    return {key: value for key, value in settings.items() if key not in ("undoCount", "redoCount")}


def create_html(output, manifest, baseline=None, baseline_directory=None):
    before = {shot["name"]: shot for shot in baseline.get("captures", [])} if baseline else {}
    cards = []
    for shot in manifest["captures"]:
        name = html.escape(shot["name"])
        current = embedded_png(output / shot["file"])
        earlier = before.get(shot["name"])
        image = f'<img src="{current}" alt="{name} current capture">'
        control = ""
        if earlier:
            previous = embedded_png(baseline_directory / earlier["file"])
            image = (f'<div class="compare"><img src="{previous}" alt="{name} baseline capture">'
                     f'<img class="after" src="{current}" alt="{name} current capture"></div>')
            control = ('<label>Baseline ← drag to compare → Current '
                       '<input type="range" min="0" max="100" value="50" '
                       'aria-label="Capture comparison position" oninput="this.closest(\'section\')'
                       '.querySelector(\'.after\').style.clipPath = '
                       '\'inset(0 0 0 \'+this.value+\'%)\'"></label>')
        details = html.escape(json.dumps({key: shot[key] for key in (
            "camera", "render_settings", "width", "height", "movement")}, ensure_ascii=False, indent=2))
        cards.append(f'<section><h2>{name}</h2>{image}{control}'
                     f'<details><summary>Camera and render settings</summary><pre>{details}</pre></details></section>')
    warnings = "".join(f"<li>{html.escape(warning)}</li>" for warning in manifest.get("warnings", []))
    information = html.escape(json.dumps({key: manifest.get(key) for key in (
        "status", "created_at", "scene", "route_sha256", "source_sha256", "session",
        "error", "restore_error")}, ensure_ascii=False, indent=2))
    document = '''<!doctype html><html lang="zh-CN"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Scene preview comparison</title>
<style>:root{color-scheme:light dark}body{font:16px/1.6 system-ui,sans-serif;max-width:1280px;
margin:auto;padding:16px}h1{font-size:24px}h2{font-size:19px}section{margin:28px 0}img{width:100%;
display:block}label{display:block;margin:12px 0}input{display:block;width:100%;margin:8px 0}
.compare{position:relative}.after{position:absolute;inset:0;clip-path:inset(0 0 0 50%)}
pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px}summary{cursor:pointer}
</style><h1>Fixed-camera scene preview</h1><p>Actual engine captures. The preview camera is not saved to the scene.
Direct image comparison requires the same route and capture dimensions; pixel values are not visual quality scores.</p>'''
    if baseline:
        document += '<p>Comparison: baseline on the left, current capture on the right. Drag the slider to move the divider.</p>'
    if warnings:
        document += f"<ul>{warnings}</ul>"
    document += "".join(cards) + f"<details><summary>Capture records</summary><pre>{information}</pre></details>"
    (output / "index.html").write_text(document, encoding="utf-8")


def perform_review(session, args, route, output, manifest, baseline):
    state = session.call("get_scene_preview")
    if state.get("active") and state.get("path") != route["scene"]:
        raise RuntimeError("Editor is previewing another scene; close it explicitly before inspection. It will not be replaced automatically.")
    if state.get("reloadPending"):
        raise RuntimeError("Preview has pending external changes; save or refresh the scene first")
    first = route["waypoints"][0]
    if not state.get("active"):
        state = session.call("open_scene_preview", {"path": route["scene"], "camera": camera_pose(first)})
    else:
        state = session.call("set_preview_camera", camera_pose(first))
    manifest["session"] = session.session_id
    manifest["preview_initial"] = state
    if state.get("modified"):
        manifest["warnings"].append("Preview contains unsaved edits; source_sha256 and source_camera describe only the scene on disk.")
    manifest["render_settings_initial"] = session.call("get_render_settings")
    initial_parameters = render_parameters(manifest["render_settings_initial"])
    dimensions = None
    before = {shot["name"]: shot for shot in baseline.get("captures", [])} if baseline else {}
    try:
        for index, point in enumerate(route["waypoints"]):
            movement = {"steps": 0, "requested_seconds": 0, "elapsed_seconds": 0}
            if index:
                state, movement = move_camera(session, state["camera"], point, route["steps_per_second"])
            time.sleep(point["settle_seconds"])
            state = session.call("get_scene_preview")
            if not state.get("active") or state.get("path") != route["scene"] or state.get("reloadPending"):
                raise RuntimeError("Preview scene changed or is awaiting reload; stopping capture")
            verify_pose(state.get("camera", {}), point)
            settings = session.call("get_render_settings")
            if render_parameters(settings) != initial_parameters:
                raise RuntimeError("Render settings changed during inspection; finish edits separately before running another capture")
            manifest["waypoints"].append({"name": point["name"], "time": utc_now(),
                                          "camera": state["camera"], "movement": movement,
                                          "capture": point["capture"]})
            if not point["capture"]:
                write_manifest(output / "manifest.json", manifest)
                print(f"[{index + 1}/{len(route['waypoints'])}] {point['name']}: transition waypoint complete", flush=True)
                continue
            name = f"{index + 1:02d}-{point['name']}.png"
            capture = session.capture((PurePosixPath(args.output) / name).as_posix())
            after = session.call("get_scene_preview")
            if not after.get("active") or after.get("path") != route["scene"] or after.get("reloadPending"):
                raise RuntimeError("Scene changed or is awaiting reload during capture; stopping")
            verify_pose(after.get("camera", {}), point)
            after_settings = session.call("get_render_settings")
            if render_parameters(after_settings) != initial_parameters:
                raise RuntimeError("Render settings changed during capture; this run cannot be used as a fixed comparison")
            actual_dimensions = [capture["width"], capture["height"]]
            if dimensions is not None and actual_dimensions != dimensions:
                raise RuntimeError("Window capture dimensions changed during inspection; this run cannot be used as a fixed comparison")
            dimensions = actual_dimensions
            earlier = before.get(point["name"])
            if earlier and [earlier["width"], earlier["height"]] != dimensions:
                raise RuntimeError("Capture dimensions differ from the baseline; restore the window size and capture again")
            shot = {"name": point["name"], "file": name, "time": utc_now(),
                    "camera": state["camera"], "preview_modified": state.get("modified"),
                    "render_settings": settings, "movement": movement,
                    **{key: capture[key] for key in ("width", "height", "bytes")},
                    "sha256": file_hash(capture["path"])}
            manifest["captures"].append(shot)
            write_manifest(output / "manifest.json", manifest)
            print(f"[{index + 1}/{len(route['waypoints'])}] {point['name']}: "
                  f"{capture['width']}×{capture['height']}, capture complete", flush=True)
    finally:
        if not args.leave_open and session.process.poll() is None:
            try:
                current = session.call("get_scene_preview")
                if not current.get("active") or current.get("path") != route["scene"]:
                    raise RuntimeError("Preview was closed or switched externally; preserving current state without restoring the old scene camera")
                restored = session.call("set_preview_camera", camera_pose(first))
                verify_pose(restored.get("camera", {}), first)
                manifest["preview_final"] = restored
            except (OSError, RuntimeError, ValueError) as error:
                manifest["restore_error"] = str(error)
        elif state:
            manifest["preview_final"] = state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--runtime-dir", type=Path, required=True)
    parser.add_argument("--route", type=Path, required=True)
    parser.add_argument("--output", required=True, help="New directory under project cache; existing captures are not overwritten")
    parser.add_argument("--compare", type=Path, help="Baseline manifest.json for the same route")
    parser.add_argument("--leave-open", action="store_true", help="Keep the final waypoint; defaults to restoring the first waypoint")
    parser.add_argument("--timeout", type=float, default=120)
    args = parser.parse_args()
    output = None
    manifest = None
    baseline = None
    exit_code = 1
    try:
        if not math.isfinite(args.timeout) or args.timeout <= 0:
            raise ValueError("timeout must be finite and positive")
        route = read_route(args.route)
        route_hash = hashlib.sha256(json.dumps(route, sort_keys=True, separators=(",", ":"),
                                              ensure_ascii=False).encode("utf-8")).hexdigest()
        cache = (args.project / "cache").resolve()
        output = (cache / args.output).resolve()
        if (Path(args.output).is_absolute() or output == cache or not output.is_relative_to(cache)
                or ".." in Path(args.output).parts or "\\" in args.output):
            raise ValueError("output must be a relative directory within the project cache")
        source = (args.project / "assets" / route["scene"][len(""):]).resolve()
        if not source.is_relative_to((args.project / "assets").resolve()):
            raise ValueError("Scene source file is outside project assets")
        source_hash = file_hash(source)
        projection = source_camera(source)
        if args.compare:
            baseline = json.loads(args.compare.read_text(encoding="utf-8"))
            if baseline.get("status") != "complete" or baseline.get("route_sha256") != route_hash:
                raise ValueError("Baseline must be complete and use the same route")
            if len(baseline.get("captures", [])) != sum(point["capture"] for point in route["waypoints"]):
                raise ValueError("Baseline captures are incomplete")
            if baseline.get("source_camera") != projection:
                raise ValueError("Scene camera projection differs from the baseline and cannot be compared directly")
        # 排他创建，避免失败或重复运行覆盖上一轮证据。
        output.mkdir(parents=True, exist_ok=False)
        manifest = {"version": 1, "status": "running", "created_at": utc_now(),
                    "project": str(args.project.resolve()), "runtime_dir": str(args.runtime_dir.resolve()),
                    "scene": route["scene"], "source_sha256": source_hash,
                    "route_sha256": route_hash, "route": route, "source_camera": projection,
                    "captures": [], "waypoints": [], "warnings": []}
        if baseline:
            manifest["baseline"] = str(args.compare.resolve())
        write_manifest(output / "manifest.json", manifest)
        with EditorMcpSession(args.server, args.project, args.runtime_dir,
                              output / "receipts.jsonl", args.timeout) as session:
            perform_review(session, args, route, output, manifest, baseline)
        manifest["source_sha256_after"] = file_hash(source)
        if source_hash != manifest["source_sha256_after"]:
            raise RuntimeError("Scene source changed during inspection; this run cannot serve as a fixed baseline")
        if manifest.get("restore_error"):
            raise RuntimeError("Capture completed but restoring the first waypoint failed; details recorded")
        manifest["status"] = "complete"
        exit_code = 0
    except KeyboardInterrupt:
        if manifest:
            manifest.update(status="interrupted", error="User interrupted capture")
        print("Inspection interrupted; captures and receipts are preserved.", file=sys.stderr)
        exit_code = 130
    except (OSError, ValueError, RuntimeError, KeyError, TypeError) as error:
        if manifest:
            manifest.update(status="failed", error=str(error))
        print(f"Inspection did not complete: {error}", file=sys.stderr)
        exit_code = 1
    finally:
        if manifest is not None:
            manifest["finished_at"] = utc_now()
            try:
                write_manifest(output / "manifest.json", manifest)
                create_html(output, manifest, baseline, args.compare.parent if args.compare else None)
                print(f"Capture records: {output / 'manifest.json'}\nOffline report: {output / 'index.html'}")
            except (OSError, ValueError, KeyError, zlib.error) as error:
                manifest["report_error"] = str(error)
                if exit_code == 0:
                    manifest["status"] = "failed"
                    exit_code = 1
                try:
                    write_manifest(output / "manifest.json", manifest)
                except OSError as write_error:
                    print(f"Failed to save capture records: {write_error}", file=sys.stderr)
                print(f"Report generation failed; captures and records are preserved: {error}", file=sys.stderr)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
