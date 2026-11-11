#!/usr/bin/env python3
"""Validate actual GPU IBL baking, resource publication and scene loading with a public temporary project."""
from __future__ import annotations

import argparse
import shutil
import json
import math
from pathlib import Path
import struct
import subprocess
import time
import tomllib
import zlib

import scene_lighting_gpu_test as fixture

ROOT = fixture.ROOT
WORK = ROOT / "build" / "ibl-white-furnace"


def png_center(path: Path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise RuntimeError("invalid PNG screenshot")
    offset = 8
    compressed = bytearray()
    while offset < len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        chunk = data[offset + 8:offset + 8 + length]
        offset += length + 12
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
        elif kind == b"IDAT":
            compressed.extend(chunk)
        elif kind == b"IEND":
            break
    if depth != 8 or color not in (2, 6) or interlace:
        raise RuntimeError("unsupported PNG pixel format")
    channels = 3 if color == 2 else 4
    row_size = width * channels
    raw = zlib.decompress(compressed)
    previous = bytearray(row_size)
    for y in range(height // 2 + 1):
        start = y * (row_size + 1)
        method = raw[start]
        row = bytearray(raw[start + 1:start + row_size + 1])
        for x in range(row_size):
            left = row[x - channels] if x >= channels else 0
            up = previous[x]
            upper_left = previous[x - channels] if x >= channels else 0
            if method == 1:
                predictor = left
            elif method == 2:
                predictor = up
            elif method == 3:
                predictor = (left + up) // 2
            elif method == 4:
                p = left + up - upper_left
                distances = [abs(p - left), abs(p - up), abs(p - upper_left)]
                predictor = (left, up, upper_left)[distances.index(min(distances))]
            elif method == 0:
                predictor = 0
            else:
                raise RuntimeError("unknown PNG filter")
            row[x] = (row[x] + predictor) & 255
        previous = row
    index = width // 2 * channels
    return list(previous[index:index + 3])


def make_white_ktx(path: Path):
    # Uncompressed, linear RGBA16F, equirectangular panorama. Radiance = 1.
    width, height = 32, 16
    pixel = struct.pack("<eeee", 1, 1, 1, 1)
    payload = pixel * (width * height)
    header = b"\xabKTX 11\xbb\r\n\x1a\n" + struct.pack(
        "<13I", 0x04030201, 0x140B, 2, 0x1908, 0x881A, 0x1908,
        width, height, 0, 0, 1, 1, 0
    )
    path.write_bytes(header + struct.pack("<I", len(payload)) + payload)


def read_ktx(path: Path):
    data = path.read_bytes()
    if data[:12] != b"\xabKTX 11\xbb\r\n\x1a\n":
        raise RuntimeError(f"invalid KTX1: {path}")
    header = struct.unpack_from("<13I", data, 12)
    _, _, _, _, internal, _, width, height, depth, arrays, faces, mips, kv = header
    if depth != 0 or arrays != 0 or faces not in (1, 6) or internal not in (0x881A, 0x822F):
        raise RuntimeError(f"unexpected KTX format: {header}")
    channels = 4 if internal == 0x881A else 2
    offset = 64 + kv
    levels = []
    for level in range(max(mips, 1)):
        size = struct.unpack_from("<I", data, offset)[0]
        offset += 4
        face_pixels = max(width >> level, 1) * max(height >> level, 1)
        if size != face_pixels * channels * 2:
            raise RuntimeError(f"invalid image size at level {level}: {size}")
        values = []
        for _ in range(faces):
            chunk = data[offset:offset + size]
            if len(chunk) != size:
                raise RuntimeError("truncated KTX")
            values.extend(struct.iter_unpack("<" + "e" * channels, chunk))
            offset += (size + 3) & ~3
        if not all(math.isfinite(x) and x >= 0 for pixel in values for x in pixel):
            raise RuntimeError("non-finite or negative radiance")
        levels.append(values)
    return header, levels


def integrate_brdf(no_v, roughness, samples=8192):
    """Independent quasi-Monte Carlo integration of linear split-sum Smith GGX and Schlick Fresnel."""
    vx = math.sqrt(1 - no_v * no_v)
    alpha_squared = roughness ** 4
    a = b = 0.0
    for i in range(samples):
        # Stratify the GGX polar CDF; use an irrational azimuth sequence rather
        # than the bake shader's Hammersley sequence.
        xi = (i + 0.5) / samples
        phi = 2 * math.pi * ((i * 0.6180339887498949) % 1)
        nh = math.sqrt((1 - xi) / (1 + (alpha_squared - 1) * xi))
        hx = math.sqrt(max(0.0, 1 - nh * nh)) * math.cos(phi)
        vh = max(vx * hx + no_v * nh, 0.0)
        nl = max(2 * vh * nh - no_v, 0.0)
        if nl <= 0 or vh <= 0:
            continue
        v = 0.5 / (nl * math.sqrt(no_v * no_v * (1 - alpha_squared) + alpha_squared) +
                   no_v * math.sqrt(nl * nl * (1 - alpha_squared) + alpha_squared))
        weight = 4 * v * vh * nl / nh
        fresnel = (1 - vh) ** 5
        a += (1 - fresnel) * weight
        b += fresnel * weight
    return a / samples, b / samples


def prepare():
    fixture.WORK = WORK
    fixture.PROJECT = WORK / "project"
    fixture.RUNTIME = WORK / "runtime"
    project = fixture.PROJECT
    for folder in ("assets", "settings", "cache"):
        (project / folder).mkdir(parents=True, exist_ok=True)
    for name in ("runtime-game.toml", "runtime-parallel.toml"):
        shutil.copy2(ROOT / "projects/showcase/settings" / name, project / "settings" / name)
    for name, data in {
        "editor-import.toml": "[common]\nimport_threads = 2\n",
        "editor-settings.toml": "[common]\ntarget_frame_rate = 60\n",
        "runtime-engine.toml": "[common]\nwindow_title = \"Engine validation\"\nwidth = 960\nheight = 640\ntarget_frame_rate = 60\n",
    }.items():
        (project / "settings" / name).write_text(data)
    source = project / "assets/textures/white.ktx"
    source.parent.mkdir(parents=True, exist_ok=True)
    make_white_ktx(source)
    source.with_name(source.name + ".meta").write_text(
        f'guid = "{fixture.identity("textures/white.ktx")}"\nimporter = "texture_ktx"\nmeta_version = 1\n[texture]\ncompression = "none"\nsrgb = false\n'
    )
    fixture.new_source("meshes/quad.mesh", "vertices = [[-1,-1,0,0,0,1],[1,-1,0,0,0,1],[1,1,0,0,0,1],[-1,1,0,0,0,1]]\nindices = [0,1,2,0,2,3]\ntexcoords = [[0,0],[1,0],[1,1],[0,1]]\n", "mesh")
    fixture.new_source("materials/white.material", 'shader = "00000000-0000-4000-8000-000000000001"\n[[parameters]]\nname = "baseColor"\ntype = "v4"\nvalue = [1.0,1.0,1.0,1.0]\n[[parameters]]\nname = "metallic"\ntype = "float"\nvalue = 0.5\n[[parameters]]\nname = "roughness"\ntype = "float"\nvalue = 0.5\n', "material")
    scene = 'name = "IBL bake white furnace"\nactive_camera = 1\n\n'
    scene += fixture.node(1, "Camera", '[[nodes.components]]\ntype = "camera"\nprojection = "orthographic"\northographic_size = 2.0\nnear_clip = 0.1\nfar_clip = 100.0\n\n[[nodes.components]]\ntype = "post_process"\nexposure = 1.0\nfilmic_tonemap = false\nhdr_enabled = false\nbloom_enabled = false\n', (0, 0, 3))
    scene += fixture.node(2, "Environment", '[[nodes.components]]\ntype = "environment"\nambient_color = [0.0,0.0,0.0]\nambient_intensity = 0.0\nibl_enabled = true\nibl_intensity = 1.0\nclear_color = 255\n')
    scene += fixture.node(3, "White receiver", fixture.mesh_component(fixture.identity("meshes/quad.mesh"), fixture.identity("materials/white.material"), False))
    fixture.new_source("scenes/white.scene", scene, "scene")
    return project


def inspect(outputs, project):
    result = {}
    for item in outputs:
        path = project / "assets" / item["path"]
        header, levels = read_ktx(path)
        if "lut" in path.name:
            # Actual LUT must vary across NoV and roughness, and represent a
            # bounded split-sum. This is not a CPU BRDF integration reference.
            values = levels[0]
            if not (max(p[0] for p in values) - min(p[0] for p in values) > 0.1 and
                    max(p[1] for p in values) > 0.01 and
                    all(sum(p[:2]) <= 1.03 for p in values)):
                raise RuntimeError("baked LUT has implausible or constant values")
            width, height = header[6:8]
            checks = []
            for y in (height // 8, height // 2, 7 * height // 8):
                for x in (width // 8, width // 2, 7 * width // 8):
                    no_v, roughness = (x + 0.5) / width, (y + 0.5) / height
                    reference = integrate_brdf(no_v, roughness)
                    actual = values[y * width + x]
                    error = max(abs(actual[c] - reference[c]) for c in (0, 1))
                    if error > 0.035:
                        raise RuntimeError(f"baked LUT disagrees with GGX integration at {no_v}, {roughness}: {actual}, {reference}")
                    checks.append({"no_v": no_v, "roughness": roughness, "actual": actual, "reference": reference, "max_error": error})
            result[path.name] = {"size": [width, height], "ranges": [[min(p[c] for p in values), max(p[c] for p in values)] for c in (0, 1)], "independent_integration": checks}
        else:
            per_level = []
            for level in levels:
                values = [value for pixel in level for value in pixel[:3]]
                lo, hi = min(values), max(values)
                if abs(lo - 1) > 0.025 or abs(hi - 1) > 0.025:
                    raise RuntimeError(f"white input should remain 1 in {path.name}: {lo}, {hi}")
                per_level.append([lo, hi])
            result[path.name] = {"size": [header[6], header[7]], "faces": header[10], "mip_ranges": per_level}
    if len(result) != 3:
        raise RuntimeError("expected three published IBL textures")
    return result


def run(binary_dir: Path):
    global WORK
    WORK = WORK / str(time.time_ns())
    project = prepare()
    fixture.RUNTIME.mkdir(parents=True, exist_ok=True)
    run_id = str(time.time_ns())
    folder = project / "cache" / "verification" / run_id
    folder.mkdir(parents=True)
    report = {"complete": False}
    log = (folder / "editor.log").open("wb")
    editor = subprocess.Popen([str(fixture.executable(binary_dir, "editor_app")), "--project", str(project), "--runtime-dir", str(fixture.RUNTIME), "--width", "960", "--height", "640"], stdout=log, stderr=subprocess.STDOUT)
    try:
        with fixture.EditorMcpSession(fixture.executable(binary_dir, "mcp_app"), project, fixture.RUNTIME, folder / "receipts.jsonl", timeout=60) as session:
            deadline = time.monotonic() + 60
            while True:
                if editor.poll() is not None:
                    raise RuntimeError(f"editor exited: {editor.returncode}")
                try:
                    session.call("get_scene_preview")
                    break
                except RuntimeError:
                    if time.monotonic() > deadline:
                        raise
                    time.sleep(0.25)
            report["opened"] = session.call("open_scene_preview", {"path": "scenes/white.scene"})
            report["before"] = session.capture(f"verification/{run_id}/before.png")
            accepted = session.call("bake_scene", {"scene": "scenes/white.scene", "environment": "textures/white.ktx", "effects": ["ibl"], "resolution": 64, "samples": 256, "lutSize": 128})
            state = fixture.wait_bake(session, accepted["job"], 120)
            if state["state"] != "complete":
                raise RuntimeError("bake did not complete: " + json.dumps(state))
            report["job"] = state
            report["textures"] = inspect(state["outputs"], project)
            scene = tomllib.loads((project / "assets/scenes/white.scene").read_text())
            env = next(c for n in scene["nodes"] for c in n["components"] if c["type"] == "environment")
            if not env["ibl_enabled"] or not all(env.get(key) for key in ("ibl_irradiance", "ibl_prefilter", "ibl_lut")):
                raise RuntimeError("published scene does not reference enabled IBL")
            report["environment"] = env
            report["after"] = session.capture(f"verification/{run_id}/after.png")
            report["center_rgb_before"] = png_center(Path(report["before"]["path"]))
            report["center_rgb_after"] = png_center(Path(report["after"]["path"]))
            # Screenshot is Reinhard + gamma 2.2, not linear float readback.
            # Its quantization is accounted for in the tolerance.
            def linear_sdr(value):
                encoded = (value / 255) ** 2.2
                return encoded / (1 - encoded) if encoded < 1 else math.inf
            report["center_linear_estimate"] = [linear_sdr(c) for c in report["center_rgb_after"]]
            if max(report["center_rgb_before"]) > 10 or any(
                abs(value - 1) > 0.08 for value in report["center_linear_estimate"]
            ):
                raise RuntimeError("baked IBL white-furnace receiver is inconsistent with unit radiance")
            report["close_editor"] = session.call("close_editor", {"delayMs": 500})
            try:
                report["editor_exit_code"] = editor.wait(timeout=15)
            except subprocess.TimeoutExpired as error:
                raise RuntimeError("editor did not exit after close_editor acknowledgement") from error
            if report["editor_exit_code"] != 0:
                raise RuntimeError(f"editor exited abnormally: {report['editor_exit_code']}")
            report["complete"] = True
    except Exception as error:
        report["error"] = str(error)
        raise
    finally:
        # Only stop the process created by this test if normal MCP shutdown
        # failed or an earlier stage raised an exception.
        if editor.poll() is None:
            editor.terminate()
            try:
                editor.wait(timeout=5)
            except subprocess.TimeoutExpired:
                editor.kill()
                editor.wait()
        log.close()
        (folder / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False))
        print(json.dumps({"report": str(folder / "report.json"), "complete": report["complete"], "error": report.get("error")}, ensure_ascii=False), flush=True)
    return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary-dir", type=Path, default=ROOT / "build/local-ninja-release")
    parser.add_argument("--run", action="store_true")
    args = parser.parse_args()
    if args.run:
        run(args.binary_dir.resolve())
    else:
        print(prepare())


if __name__ == "__main__":
    main()
