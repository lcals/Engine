#!/usr/bin/env python3
"""Validate GPU lighting bakes in a small public scene using an existing editor MCP session.

By default prepare original geometry only; --run checks one/two bounces, probes, cancellation and reopen.
--work-dir selects the test directory; generated output stays under ignored build/ by default.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import tomllib
import uuid

ROOT = Path(__file__).resolve().parents[4]
WORK = ROOT / "build" / "scene-lighting-gpu-test"
PROJECT = WORK / "project"
RUNTIME = WORK / "runtime"
sys.path.insert(0, str(ROOT / "scripts"))
from editor_mcp_session import EditorMcpSession

NAMESPACE = uuid.UUID("c980173a-fdf2-4b2f-a476-b98b6c5b42c7")


def identity(path):
    return str(uuid.uuid5(NAMESPACE, path))


def new_source(path, text, importer):
    target = PROJECT / "assets" / path
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists():
        target.write_text(text, encoding="utf-8")
    meta = target.with_name(target.name + ".meta")
    if meta.exists() and tomllib.loads(meta.read_text(encoding="utf-8")).get("guid") != identity(
        path
    ):
        raise RuntimeError(f"Test directory contains resource identities from another project: {meta}")
    if not meta.exists():
        meta.write_text(
            f'guid = "{identity(path)}"\nimporter = "{importer}"\nmeta_version = 1\n',
            encoding="utf-8",
        )


def transform(position=(0, 0, 0), rotation=(0, 0, 0), scale=(1, 1, 1)):
    return (
        '[[nodes.components]]\ntype = "transform"\n'
        f"position = {list(position)}\nrotation = {list(rotation)}\nscale = {list(scale)}\n\n"
    )


def node(number, name, components, position=(0, 0, 0), rotation=(0, 0, 0), scale=(1, 1, 1)):
    return (
        f'[[nodes]]\nid = {number}\nname = "{name}"\nparent = 0\n\n'
        + transform(position, rotation, scale)
        + components
        + "\n"
    )


def mesh_component(mesh, material, static):
    return (
        '[[nodes.components]]\ntype = "mesh_renderer"\n'
        f'mesh = "{mesh}"\nmaterial = "{material}"\nbake_static = {str(static).lower()}\n'
    )


def prepare():
    for part in ("assets", "settings", "cache"):
        (PROJECT / part).mkdir(parents=True, exist_ok=True)
    for name in ("runtime-game.toml", "runtime-parallel.toml"):
        shutil.copy2(ROOT / "projects/showcase/settings" / name, PROJECT / "settings" / name)
    settings = {
        "editor-import.toml": "[common]\nimport_threads = 2\n",
        "editor-settings.toml": "[common]\ntarget_frame_rate = 60\n",
        "runtime-engine.toml": "[common]\nwindow_title = \"Engine validation\"\nwidth = 960\nheight = 640\ntarget_frame_rate = 60\n",
    }
    for filename, text in settings.items():
        target = PROJECT / "settings" / filename
        target.write_text(text, encoding="utf-8")
    new_source(
        "meshes/floor.mesh",
        "vertices = [[-2,0,-2,0,1,0],[-2,0,2,0,1,0],[2,0,2,0,1,0],[2,0,-2,0,1,0]]\nindices = [0,1,2,0,2,3]\ntexcoords = [[0,0],[0,1],[1,1],[1,0]]\n",
        "mesh",
    )
    new_source(
        "meshes/wall.mesh",
        "vertices = [[-2,0,-2,0,0,1],[2,0,-2,0,0,1],[2,3,-2,0,0,1],[-2,3,-2,0,0,1]]\nindices = [0,1,2,0,2,3]\ntexcoords = [[0,0],[1,0],[1,1],[0,1]]\n",
        "mesh",
    )
    for name, color in (("white", [0.72, 0.72, 0.72, 1]), ("red", [0.75, 0.045, 0.025, 1])):
        material = (
            'shader = "00000000-0000-4000-8000-000000000001"\n'
            f'[[parameters]]\nname = "baseColor"\ntype = "v4"\nvalue = {color}\n'
            '[[parameters]]\nname = "metallic"\ntype = "float"\nvalue = 0\n'
            '[[parameters]]\nname = "roughness"\ntype = "float"\nvalue = 0.85\n'
        )
        new_source(f"materials/{name}.material", material, "material")
    scene = 'name = "Public diffuse lighting verification"\nactive_camera = 1\n\n'
    scene += node(
        1,
        "Camera",
        '[[nodes.components]]\ntype = "camera"\nfov_degrees = 50\nnear_clip = 0.05\nfar_clip = 100\n\n[[nodes.components]]\ntype = "post_process"\nexposure = 1\nfilmic_tonemap = true\nhdr_enabled = true\n',
        (3.6, 2.8, 6.5),
        (-15, 25, 0),
    )
    scene += node(
        2,
        "Fixed white sun",
        '[[nodes.components]]\ntype = "light"\nkind = "directional"\ncolor = [1,1,1,1]\nintensity = 3\ncast_shadows = true\nshadow_resolution = 512\nshadow_cascades = 1\nshadow_distance = 20\nshadow_bias = 0.001\n',
        (0, 5, 5),
        (-55, -20, 0),
    )
    scene += node(
        3,
        "Environment",
        '[[nodes.components]]\ntype = "environment"\nambient_color = [0.02,0.02,0.02]\nambient_intensity = 1\nfog_mode = "none"\nclear_color = 252777471\n',
    )
    scene += node(
        4,
        "Static white floor",
        mesh_component(identity("meshes/floor.mesh"), identity("materials/white.material"), True),
    )
    scene += node(
        5,
        "Static red wall",
        mesh_component(identity("meshes/wall.mesh"), identity("materials/red.material"), True),
    )
    scene += node(
        6, "Room probes", '[[nodes.components]]\ntype = "light_probe_volume"\nprobes = ""\n'
    )
    scene += node(
        7,
        "Dynamic probe receiver",
        mesh_component(
            "00000000-0000-4000-8000-000000000005", identity("materials/white.material"), False
        ),
        (0, 0.45, -0.4),
        scale=(0.6, 0.6, 0.6),
    )
    new_source("scenes/lighting.scene", scene, "scene")
    (WORK / "fixture-description.txt").write_text(
        "Public test geometry: two independent quads, builtin PBR materials, one fixed directional light, a 2x2x2 probe grid, and a nonstatic cube receiving probes. No private data. The project lives under ignored build/.\n",
        encoding="utf-8",
    )
    print(
        json.dumps(
            {
                "prepared": str(PROJECT),
                "scene": "scenes/lighting.scene",
                "static_nodes": [4, 5],
                "light": 2,
                "probe": 6,
            },
            ensure_ascii=False,
        ),
        flush=True,
    )


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def assets_snapshot():
    return {
        str(p.relative_to(PROJECT / "assets")): digest(p)
        for p in sorted((PROJECT / "assets").rglob("*"))
        if p.is_file()
    }


def ktx_stats(path):
    data = Path(path).read_bytes()
    if data[:12] != b"\xabKTX 11\xbb\r\n\x1a\n":
        raise RuntimeError("Lightmap did not publish a KTX1 source")
    (
        endian,
        gl_type,
        type_size,
        gl_format,
        gl_internal,
        gl_base,
        width,
        height,
        depth,
        arrays,
        faces,
        mips,
        kv,
    ) = struct.unpack_from("<13I", data, 12)
    # bimg identifies its half-float KTX through glInternalFormat; its writer
    # emits zero glType/glFormat and typeSize=1 even for this uncompressed payload.
    if (endian, gl_internal, gl_base, depth, arrays, faces, mips) != (
        0x04030201,
        0x881A,
        0x1908,
        0,
        0,
        1,
        1,
    ) or (gl_type, type_size, gl_format) not in {(0x140B, 2, 0x1908), (0, 1, 0)}:
        raise RuntimeError(f"Unexpected lightmap container layout: {path}")
    size = struct.unpack_from("<I", data, 64 + kv)[0]
    values = list(struct.iter_unpack("<eeee", data[68 + kv : 68 + kv + size]))
    if len(values) != width * height or not all(
        math.isfinite(x) and x >= 0 for pixel in values for x in pixel
    ):
        raise RuntimeError("Lightmap dimensions or radiance are invalid")
    lit = [pixel for pixel in values if pixel[3] > 0.5]
    if not lit:
        raise RuntimeError("Lightmap has no valid texels")
    result = {
        "path": str(path),
        "size": [width, height],
        "valid_texels": len(lit),
        "mean_rgb": [sum(v[i] for v in lit) / len(lit) for i in range(3)],
        "maximum_rgb": [max(v[i] for v in lit) for i in range(3)],
        "sha256": digest(path),
    }
    if max(result["maximum_rgb"]) <= 0:
        raise RuntimeError("Lightmap is entirely black despite direct-lit fixed surfaces")
    return result


def flatc_path(binary_dir):
    for line in (binary_dir / "CMakeCache.txt").read_text().splitlines():
        if line.startswith("ENGINE_FLATC_EXECUTABLE:FILEPATH="):
            return Path(line.split("=", 1)[1])
    raise RuntimeError("Configured flatc executable was not found")


def decode_probes(binary_dir, source, folder):
    subprocess.run(
        [
            str(flatc_path(binary_dir)),
            "-t",
            "--strict-json",
            "--defaults-json",
            "-o",
            str(folder),
            "-I",
            str(ROOT / "include/schemas/runtime"),
            str(ROOT / "include/schemas/runtime/light_probe.fbs"),
            "--",
            str(source),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    data = json.loads((folder / (source.stem + ".json")).read_text())
    if (
        [data[k] for k in ("grid_x", "grid_y", "grid_z")] != [2, 2, 2]
        or len(data["coefficients"]) != 72
        or len(data["valid"]) != 8
        or not all(data["valid"])
    ):
        raise RuntimeError("Probe grid did not publish usable SH9 samples")
    if not all(math.isfinite(c[a]) for c in data["coefficients"] for a in ("x", "y", "z")):
        raise RuntimeError("Probe radiance contains non-finite values")
    magnitude = max(abs(c[a]) for c in data["coefficients"] for a in ("x", "y", "z"))
    if magnitude <= 0:
        raise RuntimeError("Probe radiance contains no reflected light")
    return {
        "path": str(source),
        "valid": sum(data["valid"]),
        "coefficients": len(data["coefficients"]),
        "maximum_absolute_sh": magnitude,
        "sha256": digest(source),
    }


def compare_bounces(bakes):
    first, second = bakes
    changes = []
    for before, after in zip(first["lightmaps"], second["lightmaps"]):
        if Path(before["path"]).name != Path(after["path"]).name:
            raise RuntimeError("Bounce publications refer to different meshes")
        change = after["mean_rgb"][0] - before["mean_rgb"][0]
        if before["sha256"] == after["sha256"] or change <= 0.005:
            raise RuntimeError("Second bounce did not add measurable red reflected light")
        changes.append({"mesh": Path(after["path"]).stem, "red_mean_increase": change})
    floor = second["lightmaps"][0]["mean_rgb"]
    if floor[0] <= 2 * floor[1]:
        raise RuntimeError("White floor did not receive reflected red wall lighting")
    return changes


def executable(binary_dir, name):
    path = binary_dir / name
    return path if path.exists() else path.with_suffix(".exe")


def wait_bake(session, job, seconds):
    deadline = time.monotonic() + seconds
    last = None
    while time.monotonic() < deadline:
        state = session.call("get_scene_bake", {"job": job})
        stamp = (state["state"], state["completed"], state["total"])
        if stamp != last:
            print(json.dumps({"bake": state}, ensure_ascii=False), flush=True)
            last = stamp
        if state["state"] in {"complete", "cancelled", "failed"}:
            return state
        time.sleep(0.15)
    raise RuntimeError("Lighting job did not complete before the verification deadline")


def run(binary_dir, seconds, resume_report=None):
    prepare()
    binary_dir = binary_dir.resolve()
    run_id = time.strftime("%Y%m%d-%H%M%S") + f"-{time.time_ns()%1000000}"
    folder = PROJECT / "cache" / "verification" / run_id
    folder.mkdir(parents=True, exist_ok=False)
    RUNTIME.mkdir(parents=True, exist_ok=True)
    prior = json.loads(resume_report.read_text()) if resume_report else None
    if prior and (
        prior["project"] != str(PROJECT)
        or prior.get("verified_source_snapshot") != assets_snapshot()
        or [b["bounces"] for b in prior["bakes"]] != [1]
    ):
        raise RuntimeError("Resume report does not match the published first-bounce sources")
    report = {
        "project": str(PROJECT),
        "runtime": str(RUNTIME),
        "run": run_id,
        "bakes": prior["bakes"] if prior else [],
        "complete": False,
    }
    if prior:
        report["resumed_from"] = str(resume_report)
    log = (folder / "editor.log").open("wb")
    editor = subprocess.Popen(
        [
            str(executable(binary_dir, "editor_app")),
            "--project",
            str(PROJECT),
            "--runtime-dir",
            str(RUNTIME),
            "--width",
            "960",
            "--height",
            "640",
        ],
        stdout=log,
        stderr=subprocess.STDOUT,
    )
    report["owned_editor_pid"] = editor.pid
    (WORK / "active-run.json").write_text(
        json.dumps(
            {
                "pid": editor.pid,
                "report": str(folder / "report.json"),
                "log": str(folder / "editor.log"),
            },
            indent=2,
        )
    )
    try:
        with EditorMcpSession(
            executable(binary_dir, "mcp_app"),
            PROJECT,
            RUNTIME,
            folder / "receipts.jsonl",
            timeout=60,
        ) as session:
            deadline = time.monotonic() + 60
            while True:
                if editor.poll() is not None:
                    raise RuntimeError(f"Owned editor exited during startup: {editor.returncode}")
                try:
                    session.call("get_scene_preview")
                    break
                except RuntimeError:
                    if time.monotonic() > deadline:
                        raise
                    time.sleep(0.25)
            report["opened"] = session.call("open_scene_preview", {"path": "scenes/lighting.scene"})
            report["before"] = session.capture(f"verification/{run_id}/before.png")
            request = {
                "scene": "scenes/lighting.scene",
                "effects": ["lighting"],
                "resolution": 16,
                "hemisphereResolution": 16,
                "bounces": 1,
                "interpolationPasses": 2,
                "interpolationThreshold": 0.1,
                "lightNodeIds": [2],
                "probeVolumes": [
                    {
                        "nodeId": 6,
                        "boundsMin": [-1.2, 0.35, -1.4],
                        "boundsMax": [1.2, 1.7, 0.8],
                        "grid": [2, 2, 2],
                    }
                ],
            }
            first_guids = (
                {o["path"]: o["guid"] for o in prior["bakes"][0]["job"]["outputs"]}
                if prior
                else None
            )
            if prior:
                report["bakes"][0]["capture"] = report["before"]
            for bounces in ((2,) if prior else (1, 2)):
                request["bounces"] = bounces
                accepted = session.call("bake_scene", request)
                state = wait_bake(session, accepted["job"], seconds)
                if state["state"] != "complete":
                    raise RuntimeError(
                        "GPU lighting bake did not publish: "
                        + json.dumps(state, ensure_ascii=False)
                    )
                guids = {o["path"]: o["guid"] for o in state["outputs"]}
                if first_guids is not None and guids != first_guids:
                    raise RuntimeError("Rebaking changed established output GUIDs")
                first_guids = guids
                phase = folder / f"bounce-{bounces}"
                phase.mkdir()
                (phase / "published.scene").write_bytes(
                    (PROJECT / "assets/scenes/lighting.scene").read_bytes()
                )
                lightmaps = []
                probes = []
                for output in state["outputs"]:
                    source = PROJECT / "assets" / output["path"]
                    if not source.exists() or not source.with_name(source.name + ".meta").exists():
                        raise RuntimeError("Published output or stable metadata is missing")
                    shutil.copy2(source, phase / source.name)
                    if source.suffix == ".ktx":
                        lightmaps.append(ktx_stats(source))
                    elif source.suffix == ".lightprobe":
                        probes.append(decode_probes(binary_dir, source, phase))
                scene = tomllib.loads((PROJECT / "assets/scenes/lighting.scene").read_text())
                bindings = [
                    c
                    for n in scene["nodes"]
                    for c in n["components"]
                    if c["type"] == "mesh_renderer" and c.get("bake_static")
                ]
                if (
                    len(lightmaps) != 2
                    or len(probes) != 1
                    or len(bindings) != 2
                    or any(
                        len(c.get("lightmap_transform", [])) != 16
                        or len(c.get("lightmap_mesh_hash", "")) != 16
                        or not c.get("lightmap")
                        for c in bindings
                    )
                ):
                    raise RuntimeError("Scene did not bind the complete generated lighting batch")
                capture = session.capture(f"verification/{run_id}/bounce-{bounces}.png")
                report["bakes"].append(
                    {
                        "bounces": bounces,
                        "job": state,
                        "lightmaps": lightmaps,
                        "probes": probes,
                        "capture": capture,
                    }
                )
                report["verified_source_snapshot"] = assets_snapshot()
                (folder / "report.json").write_text(
                    json.dumps(report, indent=2, ensure_ascii=False)
                )
            report["bounce_changes"] = compare_bounces(report["bakes"])
            report["verified_source_snapshot"] = assets_snapshot()
            old = assets_snapshot()
            accepted = session.call("bake_scene", request)
            report["cancel_requested"] = session.call("cancel_scene_bake", {"job": accepted["job"]})
            report["cancel_result"] = wait_bake(session, accepted["job"], 30)
            if report["cancel_result"]["state"] != "cancelled" or assets_snapshot() != old:
                raise RuntimeError("Cancellation changed published source files")
            session.call("close_scene_preview")
            report["reopened"] = session.call(
                "open_scene_preview", {"path": "scenes/lighting.scene"}
            )
            report["reopened_scene"] = session.call("get_scene", {"offset": 0, "limit": 20})
            report["reopened_capture"] = session.capture(f"verification/{run_id}/reopened.png")
            report["preview"] = session.call("get_scene_preview")
            if (
                not report["preview"].get("active")
                or report["preview"].get("lastError")
                or report["preview"].get("reloadPending")
            ):
                raise RuntimeError("Published lighting scene did not reopen cleanly")
            session.call("close_scene_preview")
            report["complete"] = True
    except BaseException as error:
        report["error"] = str(error)
        raise
    finally:
        if editor.poll() is None:
            editor.terminate()
            try:
                editor.wait(timeout=5)
            except subprocess.TimeoutExpired:
                editor.kill()
                editor.wait()
        report["owned_editor_stopped"] = editor.poll() is not None
        report["editor_exit_code"] = editor.returncode
        log.close()
        (folder / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False))
        print(
            json.dumps(
                {
                    "report": str(folder / "report.json"),
                    "complete": report["complete"],
                    "error": report.get("error"),
                },
                ensure_ascii=False,
            ),
            flush=True,
        )
    return report


def main():
    global WORK, PROJECT, RUNTIME
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--resume-report",
        type=Path,
        help="Continue the verified first-bounce publication without repeating it",
    )
    parser.add_argument(
        "--run",
        action="store_true",
        help="Launch own editor and perform GPU verification; omit to prepare files only",
    )
    parser.add_argument(
        "--work-dir",
        type=Path,
        default=WORK,
        help="Directory containing only this generated test fixture and its results",
    )
    parser.add_argument("--binary-dir", type=Path, default=ROOT / "build/ninja-engine-debug")
    parser.add_argument("--job-timeout", type=float, default=300)
    args = parser.parse_args()
    WORK = args.work_dir.resolve()
    PROJECT = WORK / "project"
    RUNTIME = WORK / "runtime"
    if args.run:
        run(args.binary_dir, args.job_timeout, args.resume_report)
    else:
        prepare()


if __name__ == "__main__":
    main()
