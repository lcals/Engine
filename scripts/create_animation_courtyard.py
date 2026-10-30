#!/usr/bin/env python3
"""通过真实引擎 MCP 创建现代庭院、碰撞与灯光，再保存场景及建模源。"""

import argparse
from collections import Counter
import json
import math
from pathlib import Path
import random
import subprocess
import sys

from create_modern_apartment import ApartmentPlan, McpSession, Piece, ensure_node, scene_nodes

MATERIAL_ROLES = (
    "stone",
    "paving",
    "wood",
    "black_metal",
    "glass",
    "leaf",
    "leaf_light",
    "soil",
    "terracotta",
    "glow",
    "trim",
)


class CourtyardPlan(ApartmentPlan):
    def __init__(self):
        super().__init__()
        self.colliders = {}
        self.uv_density = {}

    def add(
        self,
        name,
        shape,
        parameters,
        position,
        material,
        rotation=(0, 0, 0),
        scale=(1, 1, 1),
        bevel=0.0,
    ):
        self.pieces.append(
            Piece(
                "courtyard_" + name,
                self.group,
                shape,
                parameters,
                position,
                material,
                rotation,
                scale,
                bevel,
            )
        )
        self.uv_density["courtyard_" + name] = {
            "paving": 1 / 1.825,
            "stone": 1 / 2.23,
            "wood": 1 / 1.2,
            "soil": 0.4,
        }.get(material, 0.5)

    def solid_box(self, name, size, position, material, bevel=0.0, rotation=(0, 0, 0)):
        self.box(name, size, position, material, bevel, rotation)
        self.colliders["courtyard_" + name] = {
            "shape": "box",
            "halfExtents": [value * 0.5 for value in size],
            "friction": 0.85,
        }

    def tree(self, name, x, z, scale=1.0):
        self.lathe(
            name + "_planter",
            [[0.64, 0], [0.69, 0.05], [0.81, 0.65], [0.82, 0.72], [0.72, 0.72], [0.70, 0.61]],
            (x, 0.05, z),
            "stone",
        )
        self.colliders["courtyard_" + name + "_planter"] = {
            "shape": "box",
            "halfExtents": [0.8, 0.36, 0.8],
            "center": [0, 0.36, 0],
            "friction": 0.85,
        }
        self.cylinder(name + "_soil", 0.70, 0.045, (x, 0.68, z), "soil", segments=32)
        self.cylinder(
            name + "_trunk", 0.10 * scale, 2.1 * scale, (x, 0.68 + 1.05 * scale, z), "wood"
        )
        branches = ((-0.65, 2.65, 0.25), (0.70, 2.90, 0.20), (0.10, 3.18, -0.58))
        for index, (dx, dy, dz) in enumerate(branches):
            self.beam(
                f"{name}_branch_{index}",
                (x, 0.7 + 1.5 * scale, z),
                (x + dx * scale, 0.7 + dy * scale, z + dz * scale),
                0.045 * scale,
                "wood",
            )
        crowns = (
            (-0.65, 2.65, 0.22, 0.85, 0.65, 0.74),
            (0.60, 2.85, 0.28, 0.84, 0.76, 0.79),
            (0.08, 3.20, -0.48, 0.91, 0.70, 0.73),
            (-0.18, 3.52, 0.20, 0.80, 0.61, 0.78),
            (-0.82, 3.10, -0.35, 0.64, 0.62, 0.68),
            (0.74, 3.31, -0.25, 0.62, 0.60, 0.65),
            (0.03, 2.77, 0.77, 0.67, 0.58, 0.66),
        )
        for index, (dx, dy, dz, rx, ry, rz) in enumerate(crowns):
            self.ellipsoid(
                f"{name}_foliage_{index}",
                (rx * scale, ry * scale, rz * scale),
                (x + dx * scale, 0.7 + dy * scale, z + dz * scale),
                "leaf_light" if index in (2, 3, 5) else "leaf",
                rotation=(index * 13, index * 31, -index * 7),
            )

    def bench(self, name, x, z, angle=0):
        begin = len(self.pieces)
        for index, dx in enumerate((-1.2, 1.2)):
            self.solid_box(
                f"{name}_support_{index}",
                (0.14, 0.46, 0.62),
                (dx, 0.28, 0),
                "black_metal",
                0.018,
            )
        for index in range(5):
            self.box(
                f"{name}_seat_slat_{index}",
                (3.0, 0.095, 0.125),
                (0, 0.535, -0.28 + index * 0.14),
                "wood",
                0.018,
            )
        self.colliders["courtyard_" + name + "_seat_slat_2"] = {
            "shape": "box",
            "halfExtents": [1.5, 0.048, 0.36],
            "friction": 0.7,
        }
        for index, y in enumerate((0.84, 1.04)):
            self.box(
                f"{name}_back_slat_{index}",
                (3.0, 0.17, 0.09),
                (0, y, -0.34),
                "wood",
                0.02,
            )
        for index, dx in enumerate((-1.2, 1.2)):
            self.box(
                f"{name}_back_support_{index}",
                (0.055, 0.66, 0.06),
                (dx, 0.78, -0.39),
                "black_metal",
                0.008,
            )
        radians = math.radians(angle)
        for piece in self.pieces[begin:]:
            px, py, pz = piece.position
            piece.position = (
                x + px * math.cos(radians) + pz * math.sin(radians),
                py,
                z - px * math.sin(radians) + pz * math.cos(radians),
            )
            piece.rotation = (0, angle, 0)

    def agave(self, name, x, z, size=1.0):
        self.lathe(
            name + "_pot",
            [[0.28, 0], [0.32, 0.04], [0.40, 0.55], [0.41, 0.61], [0.35, 0.61], [0.34, 0.54]],
            (x, 0.05, z),
            "terracotta",
            (size, size, size),
        )
        self.colliders["courtyard_" + name + "_pot"] = {
            "shape": "box",
            "halfExtents": [0.4, 0.30, 0.4],
            "center": [0, 0.30, 0],
            "friction": 0.8,
        }
        self.cylinder(name + "_soil", 0.34 * size, 0.02, (x, 0.05 + 0.54 * size, z), "soil")
        for index in range(7):
            angle = index * 360 / 7
            radians = math.radians(angle)
            self.ellipsoid(
                f"{name}_leaf_{index}",
                (0.08 * size, 0.035 * size, 0.53 * size),
                (
                    x + math.cos(radians) * 0.23 * size,
                    0.05 + (0.84 + (index % 2) * 0.08) * size,
                    z + math.sin(radians) * 0.23 * size,
                ),
                "leaf_light" if index % 3 == 0 else "leaf",
                rotation=(32, 90 - angle, 0),
            )


def courtyard_plan():
    p = CourtyardPlan()
    p.group = "ground"
    p.solid_box("foundation", (22, 0.35, 28), (0, -0.125, -5), "paving", 0.025)
    # 地面材质以米制 UV 铺贴；独立大板与细缝提供近景尺度。
    for row in range(8):
        for column in range(4):
            p.box(
                f"paver_{row}_{column}",
                (1.065, 0.055, 1.585),
                (-1.635 + column * 1.09, 0.045, 3.4 - row * 1.61),
                "paving",
                0.008,
            )
    for side in (-1, 1):
        p.box(f"path_border_{side}", (0.08, 0.075, 13.6), (side * 2.25, 0.055, -2.9), "trim", 0.01)
        p.box(f"side_gravel_{side}", (6.8, 0.035, 14.8), (side * 6.0, 0.063, -2.5), "soil")
        for index in range(5):
            p.box(
                f"cross_paver_{side}_{index}",
                (1.02, 0.065, 1.15),
                (side * (2.9 + index * 1.05), 0.065, -3.45),
                "stone",
                0.014,
            )

    p.group = "architecture"
    p.solid_box("rear_pavilion", (20.6, 4.5, 0.40), (0, 2.79, -14.6), "stone", 0.04)
    p.box("roof_shadow_reveal", (21.0, 0.11, 1.5), (0, 5.04, -14.05), "black_metal", 0.02)
    p.box("roof_coping", (21.2, 0.17, 1.65), (0, 5.18, -14.05), "trim", 0.035)
    p.solid_box("terrace", (20.6, 0.54, 4.5), (0, 0.27, -12.1), "paving", 0.025)
    p.solid_box("stair_1", (5.2, 0.18, 0.60), (0, 0.09, -8.40), "stone", 0.015)
    p.solid_box("stair_2", (5.2, 0.36, 0.60), (0, 0.18, -9.00), "stone", 0.015)
    p.solid_box("stair_3", (5.2, 0.54, 0.60), (0, 0.27, -9.60), "stone", 0.015)
    for index in range(3):
        p.box(
            f"stair_nosing_{index}",
            (5.17, 0.035, 0.09),
            (0, 0.18 * (index + 1) - 0.004, -8.13 - 0.6 * index),
            "trim",
            0.009,
        )
    for side in (-1, 1):
        p.solid_box(
            f"garden_wall_{side}", (0.30, 1.05, 19), (side * 10.35, 0.525, -3.8), "stone", 0.025
        )
        p.box(
            f"garden_coping_{side}", (0.40, 0.09, 19.1), (side * 10.35, 1.085, -3.8), "trim", 0.018
        )
        p.solid_box(
            f"terrace_return_{side}", (0.30, 3.8, 3.2), (side * 10.15, 2.44, -12.8), "stone", 0.03
        )
    for index, x in enumerate((-7.4, -3.7, 0, 3.7, 7.4)):
        height = 3.4 if index == 2 else 2.8
        center = 2.30 if index == 2 else 2.60
        bottom = 0.57 if index == 2 else 1.17
        p.box(f"window_{index}", (2.95, height, 0.065), (x, center, -14.365), "glass", 0.015)
        for side in (-1, 1):
            p.box(
                f"window_jamb_{index}_{side}",
                (0.07, height + 0.15, 0.17),
                (x + side * 1.51, center, -14.28),
                "black_metal",
                0.01,
            )
        for row, y in enumerate((bottom, 4.03)):
            p.box(
                f"window_rail_{index}_{row}",
                (3.08, 0.07, 0.17),
                (x, y, -14.28),
                "black_metal",
                0.01,
            )
        p.box(
            f"window_mullion_{index}",
            (0.045, height + 0.05, 0.14),
            (x, center, -14.25),
            "black_metal",
            0.006,
        )
        p.box(f"window_sill_{index}", (3.24, 0.10, 0.36), (x, bottom - 0.06, -14.20), "trim", 0.018)
    for side in (-1, 1):
        p.box(
            f"door_pull_{side}",
            (0.035, 0.52, 0.08),
            (side * 0.16, 1.6, -14.15),
            "black_metal",
            0.01,
        )

    p.group = "pergola"
    for index, (x, z) in enumerate(((4.2, -1.4), (8.6, -1.4), (4.2, -7.0), (8.6, -7.0))):
        p.solid_box(f"pergola_post_{index}", (0.14, 3.2, 0.14), (x, 1.65, z), "black_metal", 0.012)
    for side, x in enumerate((4.2, 8.6)):
        p.box(f"pergola_side_beam_{side}", (0.18, 0.20, 6.0), (x, 3.28, -4.2), "black_metal", 0.016)
    for index in range(15):
        p.box(
            f"pergola_roof_slat_{index}",
            (4.7, 0.17, 0.15),
            (6.4, 3.38, -1.4 - index * 0.4),
            "wood",
            0.018,
        )
    for index in range(10):
        p.box(
            f"screen_slat_{index}",
            (0.09, 2.6, 0.18),
            (8.65, 1.40, -2.0 - index * 0.40),
            "wood",
            0.012,
        )

    p.group = "furniture"
    p.bench("west_bench", -5.2, -3.45, 90)
    p.bench("pergola_bench", 6.4, -5.8)
    p.cylinder("side_table_top", 0.48, 0.10, (7.7, 0.64, -3.4), "stone", segments=32)
    p.cylinder("side_table_stem", 0.07, 0.51, (7.7, 0.33, -3.4), "black_metal")
    p.cylinder("side_table_base", 0.30, 0.06, (7.7, 0.08, -3.4), "black_metal")

    p.group = "planting"
    p.tree("west_tree", -6.9, 1.3, 0.92)
    p.tree("rear_tree", -7.8, -8.0, 1.08)
    p.tree("east_tree", 7.8, 2.5, 0.92)
    p.agave("entry_agave", -3.8, 3.7, 1.05)
    p.agave("pergola_agave", 4.1, -0.1, 0.9)
    for side in (-1, 1):
        p.solid_box(
            f"raised_planter_{side}", (3.2, 0.48, 1.10), (side * 6.9, 0.30, -10.1), "stone", 0.05
        )
        p.box(f"raised_soil_{side}", (2.98, 0.045, 0.87), (side * 6.9, 0.55, -10.1), "soil", 0.012)
        for index in range(5):
            p.ellipsoid(
                f"border_shrub_{side}_{index}",
                (0.42, 0.36 + (index % 2) * 0.1, 0.39),
                (side * 6.9 - 1.10 + index * 0.55, 0.82, -10.1),
                "leaf_light" if index % 2 else "leaf",
                rotation=(0, index * 37, 0),
            )

    p.group = "fixtures"
    for index, (x, z) in enumerate(((-2.8, 2.5), (2.8, 2.5), (-2.8, -5.4), (2.8, -5.4))):
        p.solid_box(f"bollard_{index}", (0.12, 0.80, 0.12), (x, 0.45, z), "black_metal", 0.016)
        p.box(f"bollard_lens_{index}", (0.128, 0.095, 0.128), (x, 0.77, z), "glow", 0.012)
        p.box(f"bollard_cap_{index}", (0.15, 0.05, 0.15), (x, 0.85, z), "black_metal", 0.012)
    for index, x in enumerate((-1.85, 1.85)):
        p.box(f"wall_lamp_{index}", (0.13, 0.50, 0.12), (x, 2.65, -14.26), "black_metal", 0.025)
        p.box(f"wall_lamp_diffuser_{index}", (0.085, 0.36, 0.02), (x, 2.65, -14.19), "glow", 0.004)

    names = [piece.name for piece in p.pieces]
    if len(names) != len(set(names)):
        raise RuntimeError("庭院模型名称重复")
    if any(piece.material not in MATERIAL_ROLES for piece in p.pieces):
        raise RuntimeError("庭院存在未知材质角色")
    return p


def material_paths(path):
    paths = {role: f"project/materials/courtyard-{role}.material" for role in MATERIAL_ROLES}
    if path is not None:
        supplied = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(supplied, dict) or set(supplied) - set(MATERIAL_ROLES):
            raise RuntimeError("材质映射必须是已知角色到资源路径的 JSON 对象")
        paths.update(supplied)
    for role, value in paths.items():
        if (
            not isinstance(value, str)
            or not value.startswith("project/")
            or ".." in value.split("/")
        ):
            raise RuntimeError(f"材质 {role} 必须指向项目资源路径")
    return paths


def require_unique_node(scene, name):
    matches = [node for node in scene["nodes"] if node["name"] == name]
    if len(matches) != 1:
        raise RuntimeError(f"建图场景必须有且只有一个 {name}")
    return matches[0]["nodeId"]


def setup_player(session, scene_id, enabled):
    scene = scene_nodes(session)
    player = require_unique_node(scene, "Player")
    camera = require_unique_node(scene, "Camera")
    identity = {"sceneId": scene_id, "nodeId": player}
    session.batch(
        [
            {"command": "set_character_controller", "arguments": identity | {"enabled": False}},
            {"command": "set_character_motor", "arguments": identity | {"enabled": False}},
            {
                "command": "set_transform",
                "arguments": identity
                | {"position": [0, 0.05, 2], "rotation": [0, 0, 0], "scale": [1, 1, 1]},
            },
            {"command": "set_character_motor", "arguments": identity | {"enabled": enabled}},
            {
                "command": "set_character_controller",
                "arguments": identity
                | {
                    "enabled": enabled,
                    "mode": "third_person",
                    "cameraNodeId": camera,
                    "cameraDistance": 4.6,
                },
            },
        ]
    )
    session.call("set_active_camera", {"sceneId": scene_id, "nodeId": camera})


def setup_lighting(session, scene_id):
    lights = (
        ("Sun", "directional", (0, 9, 0), (-34, -38, 0), (1.0, 0.90, 0.76, 1), 3.1, 40),
        ("Fill light", "directional", (0, 8, 0), (-35, 140, 0), (0.69, 0.82, 1.0, 1), 0.48, 40),
        (
            "courtyard_portico_light",
            "point",
            (0, 3.2, -13.4),
            (0, 0, 0),
            (1.0, 0.74, 0.48, 1),
            2.0,
            5,
        ),
        (
            "courtyard_pergola_light",
            "point",
            (6.3, 2.9, -4.2),
            (0, 0, 0),
            (1.0, 0.81, 0.60, 1),
            1.5,
            5,
        ),
    )
    for name, kind, position, rotation, color, intensity, light_range in lights:
        node = ensure_node(session, scene_id, name, "light")
        identity = {"sceneId": scene_id, "nodeId": node}
        session.batch(
            [
                {
                    "command": "set_transform",
                    "arguments": identity
                    | {"position": position, "rotation": rotation, "scale": [1, 1, 1]},
                },
                {
                    "command": "set_light",
                    "arguments": identity
                    | {"kind": kind, "color": color, "intensity": intensity, "range": light_range},
                },
            ]
        )
    # 渲染设置写入场景组件；随后 save_scene 与几何、灯光一同保存。
    session.call(
        "set_render_settings",
        {
            "ambientColor": [0.82, 0.87, 1.0],
            "ambientIntensity": 0.28,
            "exposure": 1.05,
            "filmicTonemap": True,
            "directionalShadows": True,
            "shadowResolution": 4096,
            "shadowBias": 0.0015,
            "clearColor": [0.40, 0.55, 0.65, 1.0],
        },
    )


def build_models(session, scene_id, plan, materials):
    identity = {"sceneId": scene_id}
    models = session.call("modeling_get", identity).get("models", [])
    by_name = {model["name"]: model for model in models}
    if len(by_name) != len(models):
        raise RuntimeError("场景中建模名称重复，无法按名称继续")
    for offset in range(0, len(plan.pieces), 8):
        chunk = plan.pieces[offset : offset + 8]
        missing = [piece for piece in chunk if piece.name not in by_name]
        if missing:
            session.batch(
                [
                    {
                        "command": "modeling_create",
                        "arguments": identity
                        | {
                            "name": piece.name,
                            "shape": piece.shape,
                            "parameters": piece.parameters,
                        },
                    }
                    for piece in missing
                ]
            )
            by_name = {
                model["name"]: model for model in session.call("modeling_get", identity)["models"]
            }
        for piece in chunk:
            model = by_name[piece.name]
            if piece.bevel and model["faceCount"] == 6:
                target = identity | {"modelId": model["modelId"]}
                mesh = session.call("modeling_get", target)
                edges = set()
                for face in mesh["faces"]:
                    vertices = face["vertices"]
                    for index, first in enumerate(vertices):
                        edges.add(
                            tuple(sorted((first, vertices[(index + 1) % len(vertices)]), key=int))
                        )
                session.call(
                    "modeling_edit",
                    target
                    | {
                        "operation": "bevel",
                        "selection": {"edges": sorted(edges)},
                        "parameters": {"width": piece.bevel, "segments": 3, "profile": 0.5},
                    },
                )
        geometry = []
        for piece in chunk:
            target = identity | {"modelId": by_name[piece.name]["modelId"]}
            density = plan.uv_density[piece.name]
            geometry.append(
                {
                    "command": "modeling_edit",
                    "arguments": target
                    | {
                        "operation": "project_uv",
                        "selection": {"all": True},
                        "parameters": {
                            "projection": "box",
                            "normal": [0, 1, 0],
                            "scale": [density, density],
                            "offset": [0, 0],
                        },
                    },
                }
            )
            if piece.shape != "cube" or piece.bevel:
                geometry.append(
                    {
                        "command": "modeling_edit",
                        "arguments": target
                        | {"operation": "auto_smooth", "parameters": {"angleDegrees": 55}},
                    }
                )
        session.batch(geometry)
        by_name = {
            model["name"]: model for model in session.call("modeling_get", identity)["models"]
        }
        appearance = []
        for piece in chunk:
            target = identity | {"nodeId": by_name[piece.name]["nodeId"]}
            appearance.extend(
                [
                    {
                        "command": "set_transform",
                        "arguments": target
                        | {
                            "position": piece.position,
                            "rotation": piece.rotation,
                            "scale": piece.scale,
                        },
                    },
                    {
                        "command": "set_material_type",
                        "arguments": target | {"slot": 0, "path": materials[piece.material]},
                    },
                ]
            )
            if piece.name in plan.colliders:
                appearance.append(
                    {
                        "command": "set_collider",
                        "arguments": target | {"enabled": True} | plan.colliders[piece.name],
                    }
                )
        session.batch(appearance)
        print(
            f"{min(offset + 8, len(plan.pieces))}/{len(plan.pieces)} 庭院模型完成（{chunk[-1].group}）",
            flush=True,
        )
    return session.call("modeling_get", identity)["models"]


def leaf_matrix(position, rotation, scale):
    """列主序仿射矩阵，仅作为 MCP transform 操作的参数。"""
    rx, ry, rz = (math.radians(value) for value in rotation)
    cx, sx = math.cos(rx), math.sin(rx)
    cy, sy = math.cos(ry), math.sin(ry)
    cz, sz = math.cos(rz), math.sin(rz)
    rows = (
        (cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx),
        (sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx),
        (-sy, cy * sx, cy * cx),
    )
    matrix = []
    for column in range(3):
        matrix.extend(rows[row][column] * scale[column] for row in range(3))
        matrix.append(0)
    return matrix + list(position) + [1]


def refine_foliage(session, scene_id, materials):
    """用已有复制面/变换操作生成叶簇，每棵树只保留一个 Mesh。"""
    identity = {"sceneId": scene_id}
    crowns = (
        (-0.65, 2.65, 0.22, 0.85, 0.65, 0.74),
        (0.60, 2.85, 0.28, 0.84, 0.76, 0.79),
        (0.08, 3.20, -0.48, 0.91, 0.70, 0.73),
        (-0.18, 3.52, 0.20, 0.80, 0.61, 0.78),
        (-0.82, 3.10, -0.35, 0.64, 0.62, 0.68),
        (0.74, 3.31, -0.25, 0.62, 0.60, 0.65),
        (0.03, 2.77, 0.77, 0.67, 0.58, 0.66),
    )
    for tree_index, (tree, x, z, size) in enumerate(
        (
            ("west_tree", -6.9, 1.3, 0.92),
            ("rear_tree", -7.8, -8.0, 1.08),
            ("east_tree", 7.8, 2.5, 0.92),
        )
    ):
        name = f"courtyard_{tree}_leaves"
        working = name + "_working"
        models = {
            model["name"]: model for model in session.call("modeling_get", identity)["models"]
        }
        # 中断的工作对象只由本脚本创建，无碰撞/子节点，可安全重新生成；成品不重复创建。
        if working in models:
            session.call("modeling_delete", identity | {"modelId": models[working]["modelId"]})
        if name not in models:
            created = session.call(
                "modeling_create",
                identity
                | {
                    "name": working,
                    "shape": "sphere",
                    "parameters": {"radius": 1, "segments": 6, "rings": 3},
                },
            )
            target = identity | {"modelId": created["modelId"]}
            topology = session.call("modeling_get", target)
            seed = [face["id"] for face in topology["faces"]]
            face_count = topology["model"]["faceCount"]
            if len(seed) != face_count:
                raise RuntimeError("叶片种子拓扑分页不完整")
            generator = random.Random(8201 + tree_index)
            for index in range(280):
                center_x, center_y, center_z, radius_x, radius_y, radius_z = crowns[index // 40]
                angle = generator.uniform(0, 2 * math.pi)
                height = generator.uniform(-1, 1)
                radius = generator.random() ** (1 / 3)
                horizontal = math.sqrt(1 - height * height)
                position = (
                    x + size * (center_x + radius_x * radius * horizontal * math.cos(angle)),
                    0.7 + size * (center_y + radius_y * radius * height),
                    z + size * (center_z + radius_z * radius * horizontal * math.sin(angle)),
                )
                rotation = (
                    generator.uniform(-70, 70),
                    generator.uniform(-180, 180),
                    generator.uniform(-35, 35),
                )
                scale = (
                    generator.uniform(0.16, 0.24) * size,
                    generator.uniform(0.018, 0.033) * size,
                    generator.uniform(0.075, 0.11) * size,
                )
                session.call(
                    "modeling_edit",
                    target | {"operation": "duplicate_faces", "selection": {"faces": seed}},
                )
                page = session.call(
                    "modeling_get", target | {"offset": face_count, "limit": len(seed)}
                )
                faces = [face["id"] for face in page["faces"]]
                if len(faces) != len(seed):
                    raise RuntimeError("MCP 未返回完整的新叶片 face ID")
                face_count += len(faces)
                shade = generator.uniform(0.58, 1.0)
                session.batch(
                    [
                        {
                            "command": "modeling_edit",
                            "arguments": target
                            | {
                                "operation": "transform",
                                "selection": {"faces": faces},
                                "parameters": {"matrix": leaf_matrix(position, rotation, scale)},
                            },
                        },
                        {
                            "command": "modeling_edit",
                            "arguments": target
                            | {
                                "operation": "color",
                                "selection": {"faces": faces},
                                "parameters": {
                                    "value": [shade, min(1, shade + 0.08), shade * 0.85, 1]
                                },
                            },
                        },
                    ]
                )
                if (index + 1) % 40 == 0:
                    print(f"{tree}：{index + 1}/280 叶片合入单个 Mesh", flush=True)
            session.call(
                "modeling_edit", target | {"operation": "erase_faces", "selection": {"faces": seed}}
            )
            session.call(
                "modeling_edit",
                target | {"operation": "auto_smooth", "parameters": {"angleDegrees": 55}},
            )
            node = session.call("modeling_get", target)["model"]["nodeId"]
            session.call(
                "set_material_type",
                identity | {"nodeId": node, "slot": 0, "path": materials["leaf_light"]},
            )
            session.call("modeling_duplicate", target | {"name": name})
            session.call("modeling_delete", target)
        old_crowns = [
            model
            for model_name, model in models.items()
            if model_name.startswith(f"courtyard_{tree}_foliage_")
        ]
        if old_crowns:
            session.batch(
                [
                    {
                        "command": "modeling_delete",
                        "arguments": identity | {"modelId": model["modelId"]},
                    }
                    for model in old_crowns
                ]
            )


def refresh_soil(session, scene_id, plan, materials):
    identity = {"sceneId": scene_id}
    models = {model["name"]: model for model in session.call("modeling_get", identity)["models"]}
    for piece in plan.pieces:
        if piece.material != "soil" or piece.name not in models:
            continue
        target = identity | {"modelId": models[piece.name]["modelId"]}
        session.call(
            "modeling_edit",
            target
            | {
                "operation": "project_uv",
                "selection": {"all": True},
                "parameters": {
                    "projection": "box",
                    "normal": [0, 1, 0],
                    "scale": [0.4, 0.4],
                    "offset": [0, 0],
                },
            },
        )
        node = session.call("modeling_get", target)["model"]["nodeId"]
        session.call(
            "set_material_type", identity | {"nodeId": node, "slot": 0, "path": materials["soil"]}
        )


def import_saved_scene(session):
    """保存只写源文件；同步并重启项目后才验证运行缓存。"""
    session.call("stop_project")
    result, _ = session.request_tool("sync_assets")
    if result.get("isError"):
        raise RuntimeError("场景同步失败：" + json.dumps(result, ensure_ascii=False))
    receipts = [
        json.loads(item["text"]) for item in result.get("content", []) if item.get("type") == "text"
    ]
    if not any(
        receipt.get("runtimeDir") == session.runtime and receipt.get("availableRevision")
        for receipt in receipts
    ):
        raise RuntimeError("同步回执未确认目标运行目录与版本")
    session.call("start_project")
    scene = scene_nodes(session)
    setup_lighting(session, scene["sceneId"])
    return scene


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", type=Path, default=Path("build/local-ninja-release/mcp_app"))
    parser.add_argument("--project", type=Path, default=Path("projects/showcase"))
    parser.add_argument(
        "--runtime-dir", type=Path, default=Path("build/animation-courtyard/runtime")
    )
    parser.add_argument(
        "--report", type=Path, default=Path("build/animation-courtyard/mcp-receipts.jsonl")
    )
    parser.add_argument("--material-map", type=Path, help="材质角色到项目资源路径的 JSON 映射")
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument(
        "--dry-run", action="store_true", help="只检查构图、节点及材质列表，不连接引擎"
    )
    parser.add_argument("--start", action="store_true", help="没有活动场景时调用 start_project")
    parser.add_argument("--no-save", action="store_true", help="只构建内存场景，暂不写源资源")
    parser.add_argument(
        "--lighting-only", action="store_true", help="仅恢复灯光与本次会话的渲染设置"
    )
    parser.add_argument(
        "--foliage-only", action="store_true", help="细化已保存庭院的叶簇与碎石材质"
    )
    parser.add_argument("--save-path", default="scenes/playground.scene")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout 必须大于零")
    try:
        plan = courtyard_plan()
        materials = material_paths(args.material_map)
        if args.dry_run:
            print(
                json.dumps(
                    {
                        "models": len(plan.pieces),
                        "refinedModels": len(plan.pieces) - 18,
                        "leavesPerTree": 280,
                        "groups": dict(Counter(piece.group for piece in plan.pieces)),
                        "colliders": len(plan.colliders),
                        "beveledModels": sum(bool(piece.bevel) for piece in plan.pieces),
                        "materials": materials,
                        "savePath": args.save_path,
                        "player": [0, 0.05, 2],
                    },
                    ensure_ascii=False,
                    indent=2,
                )
            )
            return 0
        with McpSession(args) as session:
            required = {
                "modeling_create",
                "modeling_edit",
                "modeling_duplicate",
                "modeling_delete",
                "set_collider",
                "set_character_controller",
                "set_character_motor",
                "set_render_settings",
                "save_scene",
                "sync_assets",
            }
            if not required.issubset(session.tools):
                raise RuntimeError(
                    "当前 MCP 缺少庭院建图命令：" + ", ".join(sorted(required - session.tools))
                )
            scene = session.call("get_scene").get("scene")
            if not scene and args.start:
                session.call("start_project")
                scene = scene_nodes(session)
            if not scene:
                raise RuntimeError("项目没有活动场景；先启动编辑器与项目")
            scene_id = scene["sceneId"]
            if args.lighting_only:
                setup_lighting(session, scene_id)
                print(
                    json.dumps(
                        {"success": True, "sceneId": scene_id, "lighting": "courtyard-daylight"},
                        ensure_ascii=False,
                    )
                )
                return 0
            if any(node["name"] == "Avatar" for node in scene["nodes"]):
                raise RuntimeError(
                    "请使用只加载场景、不动态生成 Avatar 的建图项目，避免把运行角色重复保存"
                )
            if any(
                node["name"]
                in {
                    "Ground",
                    "Orange block",
                    "Step 1",
                    "Step 2",
                    "Step 3",
                    "Step 4",
                    "Tall wall",
                    "Back wall",
                }
                for node in scene["nodes"]
            ):
                raise RuntimeError("建图项目仍有旧灰盒，请先准备仅保留 Player/Camera/灯光的场景")
            setup_player(session, scene_id, False)
            setup_lighting(session, scene_id)
            if not args.foliage_only:
                build_models(session, scene_id, plan, materials)
            refine_foliage(session, scene_id, materials)
            refresh_soil(session, scene_id, plan, materials)
            models = session.call("modeling_get", {"sceneId": scene_id})["models"]
            setup_player(session, scene_id, True)
            scene = scene_nodes(session)
            if not args.no_save:
                expected_nodes = len(scene["nodes"])
                expected_models = {model["name"]: model["triangleCount"] for model in models}
                session.call("save_scene", {"sceneId": scene_id, "path": args.save_path})
                scene = import_saved_scene(session)
                scene_id = scene["sceneId"]
                models = session.call("modeling_get", {"sceneId": scene_id})["models"]
                if (
                    len(scene["nodes"]) != expected_nodes
                    or {model["name"]: model["triangleCount"] for model in models}
                    != expected_models
                ):
                    raise RuntimeError("保存后重载的节点或模型拓扑与保存前不一致")
            print(
                json.dumps(
                    {
                        "success": True,
                        "sceneId": scene_id,
                        "models": len(models),
                        "nodes": len(scene["nodes"]),
                        "triangles": sum(model["triangleCount"] for model in models),
                        "colliders": len(plan.colliders),
                        "saved": None if args.no_save else args.save_path,
                        "reloaded": not args.no_save,
                        "mcpCalls": session.calls,
                        "report": str(args.report.resolve()),
                    },
                    ensure_ascii=False,
                    indent=2,
                )
            )
        return 0
    except (OSError, ValueError, RuntimeError, KeyError, subprocess.TimeoutExpired) as error:
        print(
            f"庭院创建未完成：{error}\n已完成操作保留在 MCP 回执中，修复后可按名称继续。",
            file=sys.stderr,
        )
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
