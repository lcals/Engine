#!/usr/bin/env python3
"""生成 showcase 综合破坏场景与原创音效；不执行引擎导入或修改其他示例。"""

import argparse
import copy
import io
import json
import math
from pathlib import Path
import random
import struct
import tomllib
import uuid
import wave

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "projects/showcase/assets"


def guid(path):
    return str(uuid.uuid5(uuid.NAMESPACE_URL, "engine/showcase/destruction-integration/" + path))


def meta(path, importer, extra=""):
    return f'guid = "{guid(path)}"\nimporter = "{importer}"\nmeta_version = 1\n' + extra


def sound(burst):
    """原创低通噪声、下滑正弦与衰减碎石脉冲，无录音采样。"""
    rate, duration = 48000, 1.8 if burst else 0.85
    rng = random.Random(87031 if burst else 37013)
    filtered, frames = 0.0, bytearray()
    for i in range(round(rate * duration)):
        t = i / rate
        noise = rng.uniform(-1, 1)
        filtered += (0.08 if burst else 0.18) * (noise - filtered)
        if burst:
            low = math.sin(math.tau * (58 * t - 7 * t * t)) * math.exp(-4.2 * t)
            value = 0.62 * low + filtered * 2.3 * math.exp(-2.8 * t)
            value += noise * 0.07 * math.exp(-18 * t)
        else:
            ticks = sum(math.exp(-95 * (t - start)) if t >= start else 0
                        for start in (0.0, 0.07, 0.13, 0.22, 0.36, 0.51))
            value = (filtered * 1.8 + noise * 0.24) * ticks
            value += 0.2 * math.sin(math.tau * 95 * t) * math.exp(-13 * t)
        sample = math.tanh(value) * min(1.0, t / 0.005) * (1 - t / duration) * 0.78
        frames.extend(struct.pack("<h", round(sample * 32767)))
    output = io.BytesIO()
    with wave.open(output, "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(rate)
        handle.writeframes(frames)
    return output.getvalue()


def scene():
    """复用当前两个独立实验场的声明，输出第三个独立场景。"""
    source = {}
    for filename in ("destruction.scene", "terrain.scene"):
        data = tomllib.loads((ASSETS / "scenes" / filename).read_text())
        source[filename] = {node["name"]: node for node in data["nodes"]}
    nodes = []

    def cloned(filename, name, identity):
        node = copy.deepcopy(source[filename][name])
        node["id"] = identity
        nodes.append(node)
        return node

    def component(node, kind):
        return next(value for value in node["components"] if value["type"] == kind)

    def new(identity, name, position=(0, 0, 0), scale=(1, 1, 1)):
        node = {"id": identity, "name": name, "parent": 0, "components": [
            {"type": "transform", "position": list(position), "rotation": [0, 0, 0],
             "scale": list(scale)}]}
        nodes.append(node)
        return node

    for identity, filename, name, position, rotation in (
        (1, "destruction.scene", "Overview camera", [34, 28, 42], [-26, 38, 0]),
        (2, "destruction.scene", "Front camera", [0, 12, 37], [-10, 0, 0]),
        (3, "terrain.scene", "Player camera", [15, 5, 20], [-16, 0, 0]),
    ):
        node = cloned(filename, name, identity)
        component(node, "transform").update(position=position, rotation=rotation)
        component(node, "post_process").update(
            bloom_enabled=True, bloom_threshold=1.0, bloom_intensity=0.17, bloom_radius=1.0)
        node["components"].append(
            {"type": "audio_listener", "enabled": identity == 1, "volume": 0.8})
    component(nodes[2], "character_camera")["target_node_id"] = 4
    player = cloned("terrain.scene", "Player", 4)
    component(player, "transform")["position"] = [15, 3, 14]
    component(player, "character_controller")["camera_node_id"] = 3
    terrain = cloned("terrain.scene", "Terrain", 5)
    component(terrain, "terrain").update(
        terrain=guid("terrain/foundation.terrain"), surface_color=[0.31, 0.38, 0.24, 1],
        wall_color=[0.51, 0.37, 0.23, 1])
    for identity, name in ((6, "Tower"), (7, "Wall"), (8, "Column"),
                           (9, "Environment"), (10, "Afternoon sun"), (11, "Cool fill")):
        cloned("destruction.scene", name, identity)
    for name, resource in (("Tower", "rocket-tower"), ("Wall", "rocket-wall"),
                            ("Column", "rocket-column")):
        node = next(node for node in nodes if node["name"] == name)
        component(node, "destruction")["destruction"] = guid(f"destruction/{resource}.destruction")
    beam = new(13, "Stress beam", (12, 5.8, -13))
    beam["components"].append({"type": "destruction",
        "destruction": guid("destruction/stress-beam.destruction"),
        "material": "a718a2db-32e9-4f08-9b02-541e34d5a0f7", "density": 80,
        "friction": 0.7, "restitution": 0.08})
    pedestal = new(14, "Stress foundation", (7.5, 2.9, -13), (1.5, 2.9, 2.0))
    pedestal["components"].append({"type": "mesh_renderer",
        "mesh": "ed1a3ef0-b010-47f5-a0b6-098ccd8eb54c",
        "material": "a718a2db-32e9-4f08-9b02-541e34d5a0f7"})
    pedestal["components"].append({"type": "collider", "shape": "box",
        "half_extents": [1, 1, 1], "friction": 0.7})
    ui = new(12, "Integration UI")
    ui["components"].append(
        {"type": "ui", "document": guid("ui/destruction-integration.rml")})
    # Markers remain outside the excavation zone and have no collider.
    for index, x in enumerate((-16, -8, 0, 8, 16)):
        node = new(20 + index, f"Safe zone marker {index + 1}", (x, 0.025, 10),
                   (0.75, 0.025, 0.08))
        node["components"].append(
            {"type": "mesh_renderer", "mesh": "ed1a3ef0-b010-47f5-a0b6-098ccd8eb54c",
             "material": "adf89965-c8d0-4593-96e7-349384f8191d"})
    for identity, name, text, position, size in (
        (30, "Study title", "ROCKET RANGE / DESTRUCTION", (-13, 15, -10), 0.65),
        (31, "Safety caption", "FIRE > CRATER > COLLAPSE > REROUTE", (-13, 0.6, 11), 0.36),
    ):
        node = new(identity, name, position)
        node["components"].append({"type": "text",
            "font": "d5dc034b-395d-4c74-b514-d2d4f116b09b", "text": text,
            "size": size, "color": [0.8, 0.9, 0.83, 1]})
    for index in range(14):
        burst = index >= 8
        effect, clip = ("burst", "blast") if burst else ("dust", "debris")
        node = new(40 + index,
                   f"{'Blast' if burst else 'Impact'} emitter {index - 7 if burst else index + 1}")
        node["components"].append({
            "type": "effect", "effect": guid(f"effects/destruction/{effect}.efkefc"),
            "speed": 1.0, "play_on_awake": False, "loop": False})
        node["components"].append({
            "type": "audio_source", "clip": guid(f"audio/destruction-{clip}.wav"),
            "volume": 0.65 if burst else 0.5, "pitch": 1.0, "loop": False,
            "spatial": True, "play_on_awake": False, "min_distance": 8.0, "max_distance": 140.0})
    for index in range(64):
        node = new(200 + index, f"Rocket {index + 1}", (0, -100, 0))
        node["components"].extend([
            {"type": "mesh_renderer", "mesh": guid("meshes/rocket.mesh"),
             "material": guid("materials/rocket.material"), "enabled": False},
            {"type": "effect", "effect": guid("effects/destruction/rocket-trail.efkefc"),
             "speed": 1.0, "play_on_awake": False, "loop": False}])
    launcher = new(95, "Rocket launcher", (15.45, 1.5, 13.8), (1.35, 1.35, 1.35))
    launcher["components"].append({"type": "mesh_renderer", "mesh": guid("meshes/rocket.mesh"),
        "material": guid("materials/launcher.material")})
    for index in range(48):
        node = new(100 + index, f"Route marker {index + 1}", (0, -100, 0), (0.16, 0.045, 0.16))
        node["components"].append({"type": "mesh_renderer",
            "mesh": "ed1a3ef0-b010-47f5-a0b6-098ccd8eb54c",
            "material": guid("materials/route.material"), "enabled": False})
    lines = ['name = "Combined destruction laboratory"', "active_camera = 1"]
    for node in nodes:
        lines.extend(("", "[[nodes]]"))
        lines.extend(f"{key} = {json.dumps(value)}" for key, value in node.items()
                     if key != "components")
        for item in node["components"]:
            lines.extend(("", "[[nodes.components]]"))
            lines.extend(f"{key} = {json.dumps(value)}" for key, value in item.items())
    return "\n".join(lines) + "\n"


def generated():
    result = {}
    for name, size, slices in (("rocket-tower", [6, 20, 5], [4, 16, 4]),
                               ("rocket-wall", [8, 9, 1.8], [8, 8, 2]),
                               ("rocket-column", [3, 12, 3], [2, 16, 2]),
                               ("stress-beam", [12, 2, 3], [12, 2, 3])):
        path = f"destruction/{name}.destruction"
        result[path] = (f'size = {size}\nfracture = "slices"\nslices_x = {slices[0]}\n'
            f'slices_y = {slices[1]}\nslices_z = {slices[2]}\nsites = 32\nseed = 17\n'
            'bond_health = 1\nanchor_height = 0.05\n')
        result[path + ".meta"] = meta(path, "destruction")
    for name, color, metallic, roughness in (
        ("rocket", [0.58, 0.64, 0.49, 1], 0.65, 0.3),
        ("launcher", [0.12, 0.17, 0.14, 1], 0.5, 0.55),
        ("route", [0.15, 0.94, 0.66, 1], 0, 0.7),
    ):
        path = f"materials/{name}.material"
        result[path] = ('shader = "00000000-0000-4000-8000-000000000001"\n'
            '[[parameters]]\nname = "baseColor"\ntype = "v4"\n'
            f'value = {color}\n[[parameters]]\nname = "metallic"\ntype = "float"\n'
            f'value = {metallic}\n[[parameters]]\nname = "roughness"\ntype = "float"\nvalue = {roughness}\n')
        result[path + ".meta"] = meta(path, "material")
    vertices, indices = [], []
    rings = [(0.62, 0.14), (-0.32, 0.14), (-0.82, 0.01)]
    for z, radius in rings:
        for index in range(16):
            angle = math.tau * index / 16
            vertices.append([radius * math.cos(angle), radius * math.sin(angle), z,
                             math.cos(angle), math.sin(angle), 0])
    for ring in range(2):
        for index in range(16):
            a, b = ring * 16 + index, ring * 16 + (index + 1) % 16
            indices.extend([a, b, b + 16, a, b + 16, a + 16])
    path = "meshes/rocket.mesh"
    result[path] = f'vertices = {json.dumps(vertices)}\nindices = {indices}\n'
    result[path + ".meta"] = meta(path, "mesh")
    path = "terrain/foundation.terrain"
    result[path] = """# Flat ground fully supports the original structural specimens.
cells_x = 64
cells_y = 32
cells_z = 64
voxel_size = 1.0
chunk_cells = 16
origin = [-32, -16, -32]
height = 0.0
amplitude = 0.0
frequency = 0.08
seed = 1
"""
    result[path + ".meta"] = meta(path, "terrain")
    path = "scenes/destruction-integration.scene"
    result[path], result[path + ".meta"] = scene(), meta(path, "scene")
    for name in ("blast", "debris"):
        path = f"audio/destruction-{name}.wav"
        result[path] = sound(name == "blast")
        result[path + ".meta"] = meta(
            path, "audio", "\n[audio]\nsample_rate = 48000\nchannels = 1\ngain = 1.0\n")
    for name in ("smoke", "particle-soft"):
        path = f"effects/destruction/textures/{name}.png"
        result[path + ".meta"] = meta(path, "texture", """
[texture]
format = "auto"
compression = "uastc"
compression_level = 2
rdo_quality_scalar = 1.0
srgb = true
mipmaps = true
""")
    for name in ("dust", "burst", "rocket-trail"):
        path = f"effects/destruction/{name}.efkefc"
        mappings = ["smoke"] + (["particle-soft"] if name != "dust" else [])
        extra = "".join(f'\n[[resources]]\npath = "textures/{texture}.png"\n'
                        f'resource = "project/effects/destruction/textures/{texture}.png"\n'
                        for texture in mappings)
        result[path + ".meta"] = meta(path, "effect", extra)
    for path in ("scripts/modules/destruction-integration.ts",
                 "scripts/modules/destruction-rockets.ts",
                 "scripts/modules/destruction-navigation.ts",
                 "scripts/models/destruction_range_config.ts",
                 "scripts/models/destruction_integration_model.ts",
                 "scripts/controllers/destruction_integration_controller.ts"):
        result[path + ".meta"] = meta(path, "script")
    path = "ui/destruction-integration.rml"
    result[path + ".meta"] = meta(path, "ui", """
[ui]
textures = []

[[ui.fonts]]
family = "body"
asset = "project/fonts/lato-regular.ttf"
shader = "builtin/shaders/font"

[[ui.fonts]]
family = "heading"
asset = "project/fonts/lato-bold.ttf"
shader = "builtin/shaders/font"
""")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    changed = []
    for relative, value in generated().items():
        path = ASSETS / relative
        data = value.encode("utf-8") if isinstance(value, str) else value
        if path.exists() and path.read_bytes() == data:
            continue
        changed.append(relative)
        if not args.check:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    print(f"{'Different' if args.check else 'Updated'} generated files: {len(changed)}")
    return 1 if args.check and changed else 0


if __name__ == "__main__":
    raise SystemExit(main())
