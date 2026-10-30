#!/usr/bin/env python3
"""为已生成的公寓源场景配置碰撞、落球与原创循环音频；重复运行结果不变。"""

import argparse
import io
import json
import math
from pathlib import Path
import re
import struct
import sys
import tomllib
import uuid
import wave

AUDIO_PATH = "audio/apartment-ambience.wav"
AUDIO_GUID = str(uuid.uuid5(uuid.NAMESPACE_URL, "engine/modern-apartment/ambience-v1"))
BALL_NAME = "apartment_physics_ball"
SOURCE_NAME = "apartment_ambience"
STATIC_NAMES = {
    "floor_underlay",
    "back_wall",
    "left_wall_back",
    "left_wall_front",
    "window_low_wall",
    "window_top_wall",
    "window_glass",
    "window_sill",
    "bedroom_partition",
    "bedroom_partition_cap",
    "bedroom_low_wall",
    "bedroom_low_wall_cap",
    "living_rug",
    "bedroom_rug",
    "dining_rug",
    "sofa_base",
    "sofa_back",
    "coffee_table_top",
    "coffee_table_pedestal",
    "side_table",
    "lounge_chair_seat",
    "lounge_chair_back",
    "lounge_chair_base",
    "bed_frame",
    "mattress",
    "duvet",
    "headboard",
    "wardrobe",
    "kitchen_counter",
    "kitchen_backsplash",
    "fridge",
    "island_base",
    "island_counter",
    "dining_table_top",
    "dining_table_base",
    "entry_console",
    "entry_console_top",
}


def component_text(component):
    lines = ["    [[nodes.components]]"]
    for key, value in component.items():
        lines.append(f"    {key} = {json.dumps(value, ensure_ascii=False, allow_nan=False)}")
    return "\n".join(lines) + "\n"


def set_component(block, component):
    """仅替换指定组件；保留节点、其他组件和既有资源引用。"""
    parts = re.split(r"(?m)^    \[\[nodes\.components\]\]\s*\n", block)
    retained = [parts[0].rstrip()]
    replaced = False
    for part in parts[1:]:
        current = tomllib.loads(part)
        if current["type"] == component["type"]:
            if replaced:
                raise ValueError(f"重复组件: {component['type']}")
            retained.append(component_text(component).rstrip())
            replaced = True
        else:
            retained.append("    [[nodes.components]]\n" + part.rstrip())
    if not replaced:
        retained.append(component_text(component).rstrip())
    return "\n\n".join(retained) + "\n\n"


def node_text(node_id, name, components):
    prefix = f"[[nodes]]\nid = '{node_id}'\nname = '{name}'\nparent = '0'\n\n"
    return prefix + "\n".join(component_text(item) for item in components) + "\n"


def component(node, kind):
    matches = [item for item in node["components"] if item["type"] == kind]
    if len(matches) != 1:
        raise ValueError(f"{node['name']} 必须有且只有一个 {kind}")
    return matches[0]


def source_map(assets):
    result = {}
    for path in sorted(assets.rglob("*.meta")):
        guid = str(uuid.UUID(tomllib.loads(path.read_text(encoding="utf-8"))["guid"]))
        source = Path(str(path)[:-5])
        if guid in result:
            raise ValueError(f"GUID 冲突: {guid}: {result[guid]} / {source}")
        result[guid] = source
    return result


def needs_collider(name):
    short = name.removeprefix("apartment_")
    return (
        short in STATIC_NAMES
        or short.startswith(("sofa_arm_", "sofa_seat_"))
        or re.fullmatch(r"(?:nightstand|kitchen_cabinet|stool_seat)_\d+", short)
        or re.fullmatch(r"dining_chair_\d+_(?:seat|back|leg_\d+)", short)
    )


def static_collider(node, sources):
    mesh_path = sources[component(node, "mesh_renderer")["mesh"]]
    vertices = tomllib.loads(mesh_path.read_text(encoding="utf-8"))["vertices"]
    minimum = [min(vertex[axis] for vertex in vertices) for axis in range(3)]
    maximum = [max(vertex[axis] for vertex in vertices) for axis in range(3)]
    if node["name"] == "apartment_floor_underlay":
        # 一块连续地面覆盖拼板缝隙，顶面与可见木地板的世界高度 0.035 一致。
        maximum[1] = 0.035 - component(node, "transform")["position"][1]
    return {
        "type": "collider",
        "enabled": True,
        "shape": "box",
        "half_extents": [(maximum[i] - minimum[i]) * 0.5 for i in range(3)],
        "center": [(maximum[i] + minimum[i]) * 0.5 for i in range(3)],
        "friction": 0.65,
        "restitution": 0.12,
        "is_trigger": False,
    }


def ambience_wav():
    """原创正弦和弦：8 秒整周期，首尾连续；无外部素材或随机输入。"""
    rate, seconds = 24000, 8
    pcm = bytearray()
    for frame in range(rate * seconds):
        time = frame / rate
        swell = 0.7 + 0.3 * math.cos(math.tau * time / seconds)
        chord = (
            math.sin(math.tau * 220 * time)
            + 0.65 * math.sin(math.tau * 275 * time)
            + 0.5 * math.sin(math.tau * 330 * time)
        ) / 2.15
        pcm.extend(struct.pack("<h", round(32767 * 0.24 * swell * chord)))
    output = io.BytesIO()
    with wave.open(output, "wb") as sound:
        sound.setnchannels(1)
        sound.setsampwidth(2)
        sound.setframerate(rate)
        sound.writeframes(pcm)
    return output.getvalue()


def prepare(project):
    assets = project / "assets"
    scene_path = assets / "scenes/apartment.scene"
    source = scene_path.read_text(encoding="utf-8")
    scene = tomllib.loads(source)
    nodes = {node["name"]: node for node in scene["nodes"]}
    if len(nodes) != len(scene["nodes"]):
        raise ValueError("公寓节点名称必须唯一")
    ids = {int(node["id"]) for node in scene["nodes"]}
    if len(ids) != len(scene["nodes"]):
        raise ValueError("场景节点 ID 必须唯一")
    sources = source_map(assets)
    audio_path = assets / AUDIO_PATH
    if AUDIO_GUID in sources and sources[AUDIO_GUID] != audio_path:
        raise ValueError("示例音频 GUID 已被其他资源使用")
    if audio_path.with_suffix(".wav.meta").exists() and sources.get(AUDIO_GUID) != audio_path:
        raise ValueError("示例音频已有不同 GUID；不会覆盖其身份")
    camera = next((node for node in nodes.values() if node["id"] == scene["active_camera"]), None)
    if camera is None:
        raise ValueError("场景没有有效的活动相机")
    blocks = re.split(r"(?m)(?=^\[\[nodes\]\]\s*$)", source)
    result = [blocks[0]]
    collider_count = 0
    for block in blocks[1:]:
        node = tomllib.loads(block)["nodes"][0]
        if node["name"] in (BALL_NAME, SOURCE_NAME):
            continue
        if node["id"] == camera["id"]:
            block = set_component(block, {"type": "audio_listener", "enabled": True, "volume": 1.0})
        if needs_collider(node["name"]):
            block = set_component(block, static_collider(node, sources))
            collider_count += 1
        result.append(block)

    next_id = max(ids) + 1

    def identity(name):
        nonlocal next_id
        if name in nodes:
            return nodes[name]["id"]
        value = str(next_id)
        next_id += 1
        return value

    sphere = component(nodes["apartment_fruit_0"], "mesh_renderer")
    # 复用既有单位球 Mesh / 材质，不复制 GUID，也不改变原有水果节点。
    result.append(
        node_text(
            identity(BALL_NAME),
            BALL_NAME,
            [
                {
                    "type": "transform",
                    "enabled": True,
                    "position": [0.1, 2.2, 2.4],
                    "rotation": [0.0, 0.0, 0.0],
                    "scale": [0.22, 0.22, 0.22],
                },
                sphere,
                {
                    "type": "collider",
                    "enabled": True,
                    "shape": "sphere",
                    "radius": 1.0,
                    "center": [0.0, 0.0, 0.0],
                    "friction": 0.65,
                    "restitution": 0.48,
                    "is_trigger": False,
                },
                {
                    "type": "rigid_body",
                    "enabled": True,
                    "motion": "dynamic",
                    "mass": 0.35,
                    "gravity_factor": 1.0,
                    "linear_damping": 0.15,
                    "angular_damping": 0.25,
                    "linear_velocity": [0.0, 0.0, 0.0],
                    "angular_velocity": [0.0, 0.0, 0.0],
                },
            ],
        )
    )
    result.append(
        node_text(
            identity(SOURCE_NAME),
            SOURCE_NAME,
            [
                {
                    "type": "transform",
                    "enabled": True,
                    "position": [0.0, 1.0, 0.0],
                    "rotation": [0.0, 0.0, 0.0],
                    "scale": [1.0, 1.0, 1.0],
                },
                {
                    "type": "audio_source",
                    "enabled": True,
                    "clip": AUDIO_GUID,
                    "volume": 0.2,
                    "pitch": 1.0,
                    "loop": True,
                    "play_on_awake": True,
                    "spatial": False,
                    "min_distance": 1.0,
                    "max_distance": 20.0,
                },
            ],
        )
    )
    updated = "\n\n".join(part.rstrip() for part in result).rstrip() + "\n"
    tomllib.loads(updated)
    meta = (
        f'guid = "{AUDIO_GUID}"\nimporter = "audio"\nmeta_version = 1\n\n'
        "[audio]\nsample_rate = 48000\nchannels = 2\ngain = 1.0\n"
    )
    return {
        scene_path: updated.encode("utf-8"),
        audio_path: ambience_wav(),
        Path(str(audio_path) + ".meta"): meta.encode("utf-8"),
    }, collider_count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=Path("projects/showcase"))
    parser.add_argument("--check", action="store_true", help="只检查是否已生成相同配置")
    args = parser.parse_args()
    outputs, collider_count = prepare(args.project)
    changed = [
        path for path, data in outputs.items() if not path.exists() or path.read_bytes() != data
    ]
    if args.check and changed:
        for path in changed:
            print(f"需要更新: {path}", file=sys.stderr)
        return 1
    if not args.check:
        for path in changed:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(outputs[path])
    print(
        f"公寓配置: {collider_count} 个静态碰撞体、1 个动态球、1 个监听器、1 个循环音源；"
        f"{'检查通过' if args.check else f'更新 {len(changed)} 个文件'}"
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, StopIteration) as error:
        print(f"公寓物理音频配置失败: {error}", file=sys.stderr)
        sys.exit(1)
