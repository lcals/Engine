#!/usr/bin/env python3
"""通过真实引擎 MCP 创建、布光并保存现代公寓；不直接生成模型或场景文件。"""

import argparse
from collections import Counter
from dataclasses import dataclass
from datetime import datetime, timezone
import json
import math
from pathlib import Path, PurePosixPath
import subprocess
import sys
import threading
import time

from mcp_client import receive, send


@dataclass
class Material:
    color: tuple
    roughness: float = 0.7
    metallic: float = 0.0
    unlit: bool = False


MATERIALS = {
    "plaster": Material((0.78, 0.75, 0.68)),
    "ivory": Material((0.88, 0.84, 0.73)),
    "linen": Material((0.73, 0.67, 0.55), 0.96),
    "cream": Material((0.86, 0.80, 0.68), 0.92),
    "oak": Material((0.43, 0.26, 0.13), 0.64),
    "oak_light": Material((0.56, 0.37, 0.21), 0.66),
    "oak_pale": Material((0.62, 0.43, 0.26), 0.68),
    "walnut": Material((0.22, 0.12, 0.07), 0.58),
    "sage": Material((0.22, 0.34, 0.27), 0.86),
    "sage_light": Material((0.38, 0.48, 0.37), 0.85),
    "terracotta": Material((0.56, 0.22, 0.12), 0.82),
    "clay": Material((0.68, 0.39, 0.26), 0.84),
    "charcoal": Material((0.032, 0.039, 0.037), 0.58),
    "black_metal": Material((0.035, 0.04, 0.042), 0.32, 0.7),
    "brass": Material((0.54, 0.37, 0.14), 0.3, 0.78),
    "steel": Material((0.44, 0.47, 0.49), 0.29, 0.86),
    "stone": Material((0.76, 0.73, 0.63), 0.34),
    "tile": Material((0.62, 0.67, 0.58), 0.48),
    "rug": Material((0.62, 0.54, 0.41), 1.0),
    "rug_border": Material((0.32, 0.28, 0.22), 1.0),
    "leaf": Material((0.10, 0.25, 0.13), 0.82),
    "leaf_light": Material((0.21, 0.37, 0.16), 0.85),
    "soil": Material((0.065, 0.042, 0.025), 1.0),
    "paper": Material((0.78, 0.75, 0.64), 0.94),
    "book_blue": Material((0.13, 0.25, 0.31), 0.8),
    "window": Material((0.57, 0.72, 0.77), 0.25, unlit=True),
    "glow": Material((1.0, 0.77, 0.42), 0.7, unlit=True),
}


@dataclass
class Piece:
    name: str
    group: str
    shape: str
    parameters: dict
    position: tuple
    material: str
    rotation: tuple = (0, 0, 0)
    scale: tuple = (1, 1, 1)
    bevel: float = 0.0


class ApartmentPlan:
    def __init__(self):
        self.pieces = []
        self.group = "architecture"

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
                "apartment_" + name,
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

    def box(self, name, size, position, material, bevel=0.0, rotation=(0, 0, 0)):
        self.add(name, "cube", {"size": size}, position, material, rotation=rotation, bevel=bevel)

    def cylinder(
        self,
        name,
        radius,
        height,
        position,
        material,
        rotation=(0, 0, 0),
        scale=(1, 1, 1),
        segments=24,
    ):
        self.add(
            name,
            "cylinder",
            {"radius": radius, "size": [1, height, 1], "segments": segments},
            position,
            material,
            rotation,
            scale,
        )

    def ellipsoid(self, name, radii, position, material, rotation=(0, 0, 0)):
        self.add(
            name,
            "sphere",
            {"radius": 1, "segments": 16, "rings": 8},
            position,
            material,
            rotation,
            radii,
        )

    def lathe(self, name, profile, position, material, scale=(1, 1, 1), cap=True):
        self.add(
            name,
            "lathe",
            {"profile": profile, "segments": 24, "angleDegrees": 360, "cap": cap},
            position,
            material,
            scale=scale,
        )

    def beam(self, name, start, end, radius, material):
        delta = [end[i] - start[i] for i in range(3)]
        length = math.sqrt(sum(value * value for value in delta))
        # Cylinder's local Y axis; Rz * Rx maps it onto the endpoint direction.
        pitch = math.degrees(math.asin(delta[2] / length))
        roll = math.degrees(math.atan2(-delta[0], delta[1]))
        self.cylinder(
            name,
            radius,
            length,
            tuple((start[i] + end[i]) * 0.5 for i in range(3)),
            material,
            rotation=(pitch, 0, roll),
            segments=12,
        )

    def plant(self, name, x, y, z, size=1):
        self.lathe(
            name + "_pot",
            [[0.19, 0], [0.23, 0.06], [0.29, 0.48], [0.30, 0.52], [0.26, 0.52], [0.25, 0.45]],
            (x, y, z),
            "clay",
            (size, size, size),
        )
        self.cylinder(name + "_soil", 0.245 * size, 0.02 * size, (x, y + 0.44 * size, z), "soil")
        for index, angle in enumerate((15, 85, 150, 220, 295)):
            radians = math.radians(angle)
            top = (
                x + math.cos(radians) * 0.24 * size,
                y + (0.95 + 0.13 * (index % 3)) * size,
                z + math.sin(radians) * 0.24 * size,
            )
            self.beam(f"{name}_stem_{index}", (x, y + 0.38 * size, z), top, 0.018 * size, "leaf")
            self.ellipsoid(
                f"{name}_leaf_{index}",
                (0.17 * size, 0.055 * size, 0.38 * size),
                top,
                "leaf_light" if index % 2 else "leaf",
                rotation=(18, 90 - angle, -15),
            )

    def books(self, name, x, y, z, count=4):
        for index in range(count):
            color = ("book_blue", "clay", "sage", "ivory")[index % 4]
            self.box(
                f"{name}_{index}",
                (0.1, 0.32 + 0.04 * (index % 2), 0.25),
                (x + index * 0.115, y + 0.18, z),
                color,
                bevel=0.007,
            )

    def chair(self, name, x, z, angle=0):
        # Rotate the complete chair around its seat's horizontal center.
        begin = len(self.pieces)
        self.box(name + "_seat", (0.72, 0.16, 0.72), (0, 0.52, 0), "sage", 0.055)
        self.box(
            name + "_back", (0.72, 0.57, 0.13), (0, 0.91, -0.30), "sage", 0.05, rotation=(-7, 0, 0)
        )
        for index, (dx, dz) in enumerate(
            ((-0.26, -0.23), (0.26, -0.23), (-0.26, 0.23), (0.26, 0.23))
        ):
            self.beam(
                f"{name}_leg_{index}", (dx * 1.18, 0.04, dz * 1.18), (dx, 0.47, dz), 0.032, "oak"
            )
        radians = math.radians(angle)
        for piece in self.pieces[begin:]:
            px, py, pz = piece.position
            piece.position = (
                x + px * math.cos(radians) + pz * math.sin(radians),
                py,
                z - px * math.sin(radians) + pz * math.cos(radians),
            )
            rx, ry, rz = piece.rotation
            # Legs are near-vertical; their small splay remains local to each chair.
            piece.rotation = (rx, ry + angle, rz)


def apartment_plan():
    p = ApartmentPlan()
    p.box("foundation", (14.5, 0.3, 10.5), (0, -0.20, 0), "plaster", 0.055)
    p.box("plinth", (14.15, 0.15, 10.15), (0, -0.41, 0), "charcoal", 0.035)
    p.box("floor_underlay", (14, 0.055, 10), (0, -0.025, 0), "walnut")
    for row in range(10):
        for column in range(4):
            p.box(
                f"floor_{row}_{column}",
                (3.49, 0.04, 0.985),
                (-5.25 + column * 3.5, 0.015, -4.5 + row),
                ("oak_light", "oak_pale", "oak_light", "oak")[(row + column * 3) % 4],
            )
    p.box("back_wall", (14.2, 3.1, 0.20), (0, 1.55, -5.05), "plaster")
    p.box("left_wall_back", (0.20, 3.1, 5.2), (-7.05, 1.55, -2.5), "plaster")
    p.box("left_wall_front", (0.20, 3.1, 1), (-7.05, 1.55, 4.5), "plaster")
    p.box("window_low_wall", (0.20, 0.75, 3.9), (-7.05, 0.375, 2), "plaster")
    p.box("window_top_wall", (0.20, 0.35, 3.9), (-7.05, 2.925, 2), "plaster")
    p.box("window_glass", (0.025, 1.95, 3.65), (-7.06, 1.75, 2), "window")
    for z in (0.1, 2, 3.9):
        p.box("window_mullion_" + str(z), (0.12, 2.1, 0.055), (-6.95, 1.78, z), "charcoal")
    for y in (0.75, 2.78):
        p.box("window_rail_" + str(y), (0.14, 0.06, 3.9), (-6.95, y, 2), "charcoal")
    p.box("window_sill", (0.34, 0.07, 4.0), (-6.87, 0.77, 2), "stone", 0.018)
    p.box("back_skirting", (13.9, 0.13, 0.06), (0, 0.1, -4.92), "ivory")
    p.box("left_skirting", (0.06, 0.13, 9.9), (-6.92, 0.1, 0), "ivory")
    p.box("bedroom_partition", (0.16, 1.45, 4.1), (-1.4, 0.725, -2.9), "plaster")
    p.box("bedroom_partition_cap", (0.23, 0.07, 4.1), (-1.4, 1.485, -2.9), "oak_light", 0.02)
    p.box("bedroom_low_wall", (3.95, 0.95, 0.16), (-5, 0.475, -0.65), "plaster")
    p.box("bedroom_low_wall_cap", (4.0, 0.07, 0.23), (-5, 0.985, -0.65), "oak_light", 0.02)

    p.group = "living"
    p.box("living_rug_border", (4.7, 0.035, 3.7), (-4.1, 0.065, 2.55), "rug_border", 0.01)
    p.box("living_rug", (4.48, 0.025, 3.48), (-4.1, 0.092, 2.55), "rug", 0.008)
    for index in range(7):
        p.box(
            f"rug_stripe_{index}", (4.2, 0.006, 0.018), (-4.1, 0.108, 1.22 + index * 0.43), "linen"
        )
    p.box("sofa_base", (3.5, 0.35, 1.08), (-4.45, 0.43, 0.42), "cream", 0.11)
    p.box("sofa_back", (3.5, 0.72, 0.27), (-4.45, 0.94, 0.00), "cream", 0.09)
    for x in (-6.08, -2.82):
        p.box("sofa_arm_" + str(x), (0.24, 0.58, 1.13), (x, 0.78, 0.43), "cream", 0.075)
    for index in range(3):
        x = -5.5 + index * 1.05
        p.box(f"sofa_seat_{index}", (1.0, 0.22, 0.83), (x, 0.69, 0.48), "linen", 0.075)
        p.box(
            f"sofa_back_cushion_{index}",
            (0.97, 0.53, 0.22),
            (x, 1.02, 0.24),
            "cream",
            0.075,
            rotation=(-10, 0, 0),
        )
    for index, (x, color, angle) in enumerate(((-5.66, "terracotta", -13), (-3.28, "sage", 11))):
        p.box(
            f"sofa_throw_pillow_{index}",
            (0.54, 0.54, 0.20),
            (x, 0.99, 0.52),
            color,
            0.075,
            rotation=(-18, 0, angle),
        )
    for index, x in enumerate((-5.85, -3.05)):
        p.box(f"sofa_foot_{index}", (0.13, 0.22, 0.78), (x, 0.17, 0.43), "walnut", 0.025)
    p.box("sofa_throw", (0.72, 0.055, 0.75), (-3.76, 0.822, 0.60), "sage_light", 0.016)
    p.box("sofa_throw_drape", (0.72, 0.38, 0.055), (-3.76, 0.65, 0.98), "sage_light", 0.016)
    p.cylinder("coffee_table_top", 1, 0.12, (-4.25, 0.52, 2.55), "stone", scale=(1.25, 1, 0.72))
    p.cylinder("coffee_table_pedestal", 0.44, 0.40, (-4.25, 0.26, 2.55), "oak")
    p.cylinder("coffee_table_tray", 0.26, 0.035, (-4.1, 0.602, 2.55), "walnut")
    p.lathe(
        "coffee_vase",
        [[0.10, 0], [0.15, 0.07], [0.14, 0.19], [0.07, 0.30]],
        (-4.1, 0.63, 2.55),
        "ivory",
    )
    p.box(
        "coffee_book_cover",
        (0.5, 0.07, 0.33),
        (-4.8, 0.615, 2.57),
        "book_blue",
        0.008,
        rotation=(0, 12, 0),
    )
    p.box(
        "coffee_book_pages", (0.47, 0.045, 0.315), (-4.8, 0.62, 2.57), "paper", rotation=(0, 12, 0)
    )
    p.cylinder("side_table", 0.39, 0.08, (-6.37, 0.62, 1.50), "walnut")
    p.cylinder("side_table_stem", 0.055, 0.55, (-6.37, 0.31, 1.50), "brass")
    p.cylinder("side_table_base", 0.27, 0.045, (-6.37, 0.065, 1.50), "brass")
    p.lathe(
        "side_ceramic",
        [[0.08, 0], [0.12, 0.09], [0.09, 0.20], [0.045, 0.24]],
        (-6.37, 0.67, 1.50),
        "terracotta",
    )
    p.box(
        "lounge_chair_seat",
        (0.96, 0.25, 1.00),
        (-1.95, 0.48, 3.17),
        "sage",
        0.09,
        rotation=(0, -35, 0),
    )
    p.box(
        "lounge_chair_back",
        (0.96, 0.74, 0.24),
        (-1.71, 0.89, 3.48),
        "sage",
        0.085,
        rotation=(10, -35, 0),
    )
    p.cylinder("lounge_chair_base", 0.32, 0.33, (-1.95, 0.23, 3.17), "walnut")
    p.plant("living_plant", -6.25, 0.07, 4.13, 1.3)
    p.cylinder("floor_lamp_base", 0.29, 0.06, (-2.25, 0.07, 0.66), "black_metal")
    p.cylinder("floor_lamp_stem", 0.024, 1.8, (-2.25, 0.97, 0.66), "brass")
    p.lathe(
        "floor_lamp_shade",
        [[0.34, 0], [0.23, 0.43], [0.20, 0.43], [0.31, 0]],
        (-2.25, 1.67, 0.66),
        "ivory",
        cap=False,
    )
    p.ellipsoid("floor_lamp_bulb", (0.09, 0.09, 0.09), (-2.25, 1.82, 0.66), "glow")

    p.group = "bedroom"
    p.box("bedroom_rug", (3.65, 0.04, 3.75), (-4.8, 0.065, -2.9), "linen", 0.01)
    p.box("bed_frame", (2.75, 0.28, 3.0), (-4.7, 0.33, -2.95), "oak", 0.065)
    p.box("mattress", (2.65, 0.28, 2.85), (-4.7, 0.59, -2.95), "ivory", 0.10)
    p.box("duvet", (2.72, 0.18, 2.02), (-4.7, 0.8, -2.53), "cream", 0.075)
    p.box("bed_runner", (2.77, 0.055, 0.68), (-4.7, 0.925, -1.93), "clay", 0.023)
    p.box("runner_drape_left", (0.05, 0.30, 0.68), (-6.1, 0.77, -1.93), "clay", 0.016)
    p.box("runner_drape_right", (0.05, 0.30, 0.68), (-3.3, 0.77, -1.93), "clay", 0.016)
    p.box("headboard", (2.93, 1.3, 0.19), (-4.7, 0.72, -4.41), "walnut", 0.045)
    for index in range(15):
        p.box(
            f"headboard_slat_{index}",
            (0.13, 1.1, 0.05),
            (-6.03 + index * 0.19, 0.82, -4.28),
            "oak_light",
            0.015,
        )
    for index, x in enumerate((-5.4, -4.0)):
        p.box(
            f"bed_pillow_{index}",
            (1.12, 0.23, 0.62),
            (x, 0.86, -3.92),
            "linen",
            0.085,
            rotation=(8, 0, 0),
        )
        p.box(
            f"bed_accent_pillow_{index}",
            (0.63, 0.30, 0.32),
            (x, 0.98, -3.56),
            "sage_light",
            0.075,
            rotation=(12, 0, 0),
        )
    for index, x in enumerate((-6.5, -2.88)):
        p.box(f"nightstand_{index}", (0.59, 0.48, 0.64), (x, 0.36, -4.04), "oak", 0.035)
        p.box(f"nightstand_drawer_{index}", (0.53, 0.22, 0.02), (x, 0.43, -3.704), "oak_light")
        p.cylinder(
            f"nightstand_knob_{index}",
            0.035,
            0.04,
            (x, 0.44, -3.67),
            "brass",
            rotation=(90, 0, 0),
            segments=12,
        )
        p.lathe(
            f"bedside_lamp_base_{index}",
            [[0.12, 0], [0.15, 0.15], [0.08, 0.34]],
            (x, 0.61, -4.04),
            "terracotta",
        )
        p.lathe(
            f"bedside_lamp_shade_{index}",
            [[0.25, 0], [0.17, 0.28], [0.145, 0.28], [0.225, 0]],
            (x, 0.91, -4.04),
            "ivory",
            cap=False,
        )
    # Wall art is modeled as a frame, canvas and low-relief abstract composition.
    p.box("bed_art_frame", (1.50, 1.05, 0.065), (-4.7, 2.25, -4.9), "oak")
    p.box("bed_art_canvas", (1.37, 0.92, 0.02), (-4.7, 2.25, -4.85), "ivory")
    p.cylinder("bed_art_sun", 0.24, 0.015, (-4.94, 2.42, -4.83), "terracotta", rotation=(90, 0, 0))
    p.box("bed_art_field", (1.23, 0.24, 0.02), (-4.7, 1.97, -4.83), "sage")
    p.box("wardrobe", (0.55, 2.2, 2.45), (-1.83, 1.12, -3.36), "ivory", 0.035)
    for index in range(3):
        z = -4.16 + index * 0.8
        p.box(f"wardrobe_door_{index}", (0.025, 2.12, 0.77), (-2.122, 1.12, z), "linen")
        p.box(f"wardrobe_pull_{index}", (0.05, 0.31, 0.025), (-2.17, 1.17, z + 0.28), "brass")

    p.group = "kitchen"
    p.box("kitchen_tile_floor", (7.95, 0.05, 4.05), (2.7, 0.065, -2.91), "tile")
    p.box("kitchen_toekick", (6.5, 0.14, 0.59), (2.55, 0.16, -4.41), "charcoal")
    for index in range(6):
        x = -0.12 + index * 1.07
        p.box(f"kitchen_cabinet_{index}", (1.045, 0.78, 0.85), (x, 0.59, -4.4), "sage", 0.014)
        p.box(f"kitchen_front_{index}", (0.99, 0.70, 0.03), (x, 0.59, -3.955), "sage_light", 0.01)
        p.box(f"kitchen_handle_{index}", (0.36, 0.025, 0.035), (x, 0.88, -3.91), "brass")
    p.box("kitchen_counter", (6.65, 0.10, 1.02), (2.55, 1.04, -4.36), "stone", 0.025)
    p.box("kitchen_backsplash", (6.70, 0.66, 0.035), (2.55, 1.41, -4.91), "ivory")
    for index in range(4):
        x = 0.08 + index * 1.12
        p.box(f"kitchen_upper_{index}", (1.08, 0.68, 0.4), (x, 2.18, -4.69), "oak_light", 0.018)
        p.box(f"kitchen_upper_reveal_{index}", (1.02, 0.018, 0.025), (x, 1.86, -4.47), "walnut")
    p.box("kitchen_upper_light", (4.45, 0.025, 0.16), (1.75, 1.826, -4.50), "glow")
    p.box("sink_rim", (0.95, 0.022, 0.62), (1.0, 1.101, -4.30), "steel", 0.008)
    p.box("sink_bowl", (0.81, 0.025, 0.48), (1.0, 1.117, -4.30), "charcoal", 0.008)
    p.cylinder("sink_drain", 0.055, 0.012, (1.0, 1.138, -4.30), "steel")
    p.add(
        "tap",
        "sweep",
        {
            "profile": [[-0.022, -0.022], [0.022, -0.022], [0.022, 0.022], [-0.022, 0.022]],
            "path": [[0, 0, 0], [0, 0.31, 0], [0, 0.4, 0.08], [0, 0.4, 0.25], [0, 0.34, 0.29]],
            "closedProfile": True,
            "cap": True,
        },
        (1.0, 1.11, -4.66),
        "brass",
    )
    p.box("hob", (1.1, 0.035, 0.66), (3.9, 1.108, -4.3), "charcoal", 0.012)
    for index, (x, z) in enumerate(((3.63, -4.48), (4.18, -4.48), (3.63, -4.13), (4.18, -4.13))):
        p.add(
            f"hob_ring_{index}",
            "torus",
            {"radius": 0.12, "innerRadius": 0.009, "segments": 20, "rings": 6},
            (x, 1.132, z),
            "steel",
        )
    p.cylinder("cooking_pot", 0.17, 0.22, (3.63, 1.26, -4.48), "sage")
    p.cylinder("cooking_pot_lid", 0.18, 0.025, (3.63, 1.384, -4.48), "steel")
    p.cylinder("pot_handle", 0.035, 0.04, (3.63, 1.416, -4.48), "walnut", segments=12)
    p.box(
        "cutting_board", (0.53, 0.035, 0.36), (2.4, 1.11, -4.25), "oak", 0.015, rotation=(0, -9, 0)
    )
    p.ellipsoid("bread", (0.19, 0.09, 0.12), (2.4, 1.21, -4.25), "oak_pale")
    p.box("fridge", (0.97, 2.15, 0.95), (6.42, 1.12, -4.39), "steel", 0.035)
    p.box("fridge_door", (0.91, 1.37, 0.025), (6.42, 1.47, -3.897), "charcoal", 0.008)
    p.box("fridge_drawer", (0.91, 0.59, 0.025), (6.42, 0.46, -3.897), "charcoal", 0.008)
    p.box("fridge_handle", (0.032, 0.70, 0.07), (6.06, 1.45, -3.84), "steel")
    p.box("island_plinth", (2.75, 0.12, 1.01), (2.65, 0.13, -1.3), "charcoal")
    p.box("island_base", (2.85, 0.85, 1.12), (2.65, 0.62, -1.3), "oak", 0.025)
    p.box("island_counter", (3.2, 0.12, 1.55), (2.65, 1.12, -1.13), "stone", 0.03)
    for index in range(12):
        p.box(
            f"island_flute_{index}",
            (0.13, 0.79, 0.04),
            (1.34 + index * 0.238, 0.62, -0.72),
            "oak_light",
            0.014,
        )
    for index, x in enumerate((1.7, 3.57)):
        p.cylinder(f"stool_seat_{index}", 0.32, 0.10, (x, 0.72, -0.12), "walnut")
        for leg, (dx, dz) in enumerate(((-0.19, -0.18), (0.19, -0.18), (0, 0.22))):
            p.beam(
                f"stool_leg_{index}_{leg}",
                (x + dx * 1.4, 0.05, -0.12 + dz * 1.4),
                (x + dx, 0.68, -0.12 + dz),
                0.028,
                "black_metal",
            )
    p.lathe(
        "fruit_bowl",
        [[0.10, 0], [0.24, 0.07], [0.32, 0.15], [0.29, 0.15], [0.20, 0.08], [0.08, 0.04]],
        (2.65, 1.19, -1.1),
        "terracotta",
    )
    for index, (dx, dz) in enumerate(((-0.12, -0.03), (0.12, 0), (0, 0.13))):
        p.ellipsoid(
            f"fruit_{index}",
            (0.10, 0.10, 0.10),
            (2.65 + dx, 1.36, -1.1 + dz),
            "clay" if index == 1 else "sage_light",
        )

    p.group = "dining"
    p.cylinder("dining_rug", 2.15, 0.025, (3.6, 0.073, 2.5), "linen", segments=48)
    p.cylinder("dining_table_top", 1.08, 0.12, (3.6, 0.87, 2.5), "oak_light", segments=48)
    p.lathe(
        "dining_table_base",
        [[0.50, 0], [0.45, 0.08], [0.28, 0.65], [0.40, 0.73]],
        (3.6, 0.10, 2.5),
        "walnut",
    )
    for index, (x, z, rotation) in enumerate(
        ((3.6, 0.95, 0), (5.15, 2.5, -90), (3.6, 4.05, 180), (2.05, 2.5, 90))
    ):
        p.chair(f"dining_chair_{index}", x, z, rotation)
    p.lathe(
        "dining_vase",
        [[0.11, 0], [0.17, 0.09], [0.16, 0.26], [0.07, 0.38]],
        (3.6, 0.94, 2.5),
        "ivory",
    )
    for index, dx in enumerate((-0.15, 0, 0.17)):
        p.beam(
            f"dining_branch_{index}",
            (3.6, 1.15, 2.5),
            (3.6 + dx, 1.67 + index * 0.06, 2.5 + dx),
            0.009,
            "walnut",
        )
        p.ellipsoid(
            f"dining_branch_leaf_{index}",
            (0.09, 0.035, 0.20),
            (3.6 + dx, 1.60 + index * 0.06, 2.5 + dx),
            "leaf",
            rotation=(15, index * 65, 0),
        )
    for index, (x, z) in enumerate(((3.6, 1.81), (4.3, 2.5), (3.6, 3.2), (2.9, 2.5))):
        p.cylinder(f"dining_plate_{index}", 0.20, 0.018, (x, 0.944, z), "ivory")
        p.cylinder(f"dining_plate_center_{index}", 0.15, 0.008, (x, 0.957, z), "stone")
    p.cylinder("pendant_stem", 0.015, 0.65, (3.6, 2.89, 2.5), "black_metal", segments=12)
    p.lathe(
        "pendant_shade",
        [
            [0.48, 0],
            [0.43, 0.18],
            [0.25, 0.42],
            [0.08, 0.48],
            [0.07, 0.45],
            [0.22, 0.39],
            [0.40, 0.16],
            [0.45, 0],
        ],
        (3.6, 2.09, 2.5),
        "clay",
        cap=False,
    )
    p.cylinder("pendant_diffuser", 0.41, 0.015, (3.6, 2.105, 2.5), "glow")

    p.group = "decor"
    p.box("entry_console", (0.55, 0.70, 2.05), (6.5, 0.48, -0.67), "oak", 0.025)
    p.box("entry_console_top", (0.66, 0.07, 2.17), (6.5, 0.88, -0.67), "stone", 0.02)
    p.plant("entry_plant", 6.20, 0.07, 4.07, 1.1)
    p.plant("console_plant", 6.50, 0.92, -1.25, 0.48)
    p.books("console_books", 6.32, 0.93, -0.23, 3)
    p.box("back_art_frame", (1.0, 1.3, 0.06), (-2.22, 2.20, -4.9), "charcoal")
    p.box("back_art_canvas", (0.91, 1.21, 0.02), (-2.22, 2.20, -4.85), "linen")
    p.cylinder("back_art_circle", 0.30, 0.014, (-2.22, 2.41, -4.83), "sage", rotation=(90, 0, 0))
    p.box(
        "back_art_line",
        (0.09, 0.79, 0.015),
        (-2.37, 2.0, -4.82),
        "terracotta",
        rotation=(0, 0, -18),
    )
    p.box("kitchen_open_shelf", (1.20, 0.06, 0.31), (4.64, 2.09, -4.71), "walnut")
    p.books("shelf_books", 4.17, 2.13, -4.68, 3)
    p.lathe(
        "shelf_vase",
        [[0.09, 0], [0.13, 0.12], [0.09, 0.27], [0.06, 0.30]],
        (4.94, 2.13, -4.68),
        "clay",
    )
    names = [piece.name for piece in p.pieces]
    if len(names) != len(set(names)):
        raise RuntimeError("公寓模型名称重复")
    return p


class McpSession:
    def __init__(self, args):
        self.args = args
        self.project = str(args.project.resolve())
        self.runtime = str(args.runtime_dir.resolve())
        self.process = None
        self.next_id = 1
        self.calls = 0
        self.ledger = None
        self.tools = set()

    def __enter__(self):
        self.args.report.parent.mkdir(parents=True, exist_ok=True)
        self.ledger = self.args.report.open("a", encoding="utf-8")
        self.process = subprocess.Popen(
            [
                str(self.args.server.resolve()),
                "--project",
                self.project,
                "--runtime-dir",
                self.runtime,
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=None,
            text=True,
            encoding="utf-8",
        )
        initialized = self.rpc(
            "initialize",
            {
                "protocolVersion": "2025-06-18",
                "capabilities": {},
                "clientInfo": {"name": "modern-apartment-builder", "version": "1.0.0"},
            },
        )
        if initialized.get("protocolVersion") != "2025-06-18":
            raise RuntimeError("MCP 协议版本不匹配")
        send(self.process, {"jsonrpc": "2.0", "method": "notifications/initialized"})
        cursor = None
        while True:
            listing = self.rpc("tools/list", {"cursor": cursor} if cursor else {})
            self.tools.update(item["name"] for item in listing.get("tools", []))
            cursor = listing.get("nextCursor")
            if not cursor:
                break
        return self

    def __exit__(self, _kind, _value, _traceback):
        if self.process is not None:
            if self.process.poll() is None:
                self.process.stdin.close()
                try:
                    self.process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait()
        if self.ledger is not None:
            self.ledger.close()

    def rpc(self, method, params):
        request_id = self.next_id
        self.next_id += 1
        timer = threading.Timer(self.args.timeout, self.process.kill)
        timer.daemon = True
        timer.start()
        try:
            send(
                self.process,
                {"jsonrpc": "2.0", "id": request_id, "method": method, "params": params},
            )
            return receive(self.process, request_id)
        finally:
            timer.cancel()

    def request_tool(self, name, arguments=None):
        arguments = arguments or {}
        result = self.rpc("tools/call", {"name": name, "arguments": arguments})
        receipt = None
        for block in result.get("content", []):
            if block.get("type") == "text":
                try:
                    value = json.loads(block.get("text", ""))
                except json.JSONDecodeError:
                    continue
                if isinstance(value, dict) and (
                    "success" in value or "lastRequestSucceeded" in value or "activation" in value
                ):
                    receipt = value
                    break
        entry = {
            "time": datetime.now(timezone.utc).isoformat(),
            "tool": name,
            "arguments": arguments,
            "receipt": receipt,
            "isError": result.get("isError", False),
        }
        self.ledger.write(json.dumps(entry, ensure_ascii=False) + "\n")
        self.ledger.flush()
        self.calls += 1
        return result, receipt

    def call(self, name, arguments=None):
        deadline = time.monotonic() + self.args.timeout
        result, receipt = self.request_tool(name, arguments)
        while receipt is not None and receipt.get("activation") == "pending":
            if (
                receipt.get("projectRoot") != self.project
                or receipt.get("runtimeDir") != self.runtime
            ):
                raise RuntimeError(f"MCP {name} 待处理回执所属项目或运行实例不匹配")
            if name in ("start_project", "stop_project"):
                raise RuntimeError(
                    f"MCP {name} 生命周期请求尚未完成；请先查询 get_status，不要重复提交"
                )
            identity = {"session": receipt.get("session"), "requestId": receipt.get("requestId")}
            if not all(isinstance(value, str) and value for value in identity.values()):
                raise RuntimeError(f"MCP {name} 待处理回执缺少身份")
            if time.monotonic() >= deadline:
                raise RuntimeError(f"MCP {name} 仍在处理；原请求 {identity} 已记入回执，不重复发送")
            time.sleep(0.25)
            result, receipt = self.request_tool("get_editor_receipt", identity)
            if receipt is not None and any(
                receipt.get(key) != value for key, value in identity.items()
            ):
                raise RuntimeError(f"MCP {name} 返回了其他请求的回执")
        succeeded = (
            receipt.get("success", receipt.get("lastRequestSucceeded")) if receipt else False
        )
        if result.get("isError") or receipt is None or succeeded is not True:
            raise RuntimeError(
                f"MCP {name} 失败：{json.dumps(receipt or result, ensure_ascii=False)}"
            )
        if receipt.get("projectRoot") != self.project:
            raise RuntimeError(f"MCP {name} 返回了其他项目的回执")
        if receipt.get("runtimeDir") != self.runtime:
            raise RuntimeError(f"MCP {name} 返回了其他运行实例的回执")
        if receipt.get("activation") != "completed":
            raise RuntimeError(f"MCP {name} 尚未完成，停止发送后续编辑")
        return receipt.get("result", {})

    def batch(self, commands):
        for offset in range(0, len(commands), 60):
            self.call("execute_batch", {"commands": commands[offset : offset + 60]})


def camera_rotation(eye, target):
    delta = [target[index] - eye[index] for index in range(3)]
    length = math.sqrt(sum(value * value for value in delta))
    return [
        math.degrees(math.asin(delta[1] / length)),
        math.degrees(math.atan2(-delta[0], -delta[2])),
        0,
    ]


VIEWS = {
    "wide": ((14.8, 13.4, 18.2), (0, 0.7, -0.1), "orthographic", 6.4, 43),
    "living": ((1.4, 6.2, 9.4), (-3.9, 0.8, 1.75), "perspective", 6, 47),
    "kitchen": ((9.4, 5.8, 4.8), (2.7, 0.9, -2.6), "perspective", 6, 46),
    "bedroom": ((2.5, 7.8, 4.0), (-4.5, 0.8, -2.9), "perspective", 6, 44),
}


def scene_nodes(session):
    scene = session.call("get_scene").get("scene")
    if not scene:
        raise RuntimeError("项目尚无活动场景；先通过 MCP start_project 启动项目")
    return scene


def ensure_node(session, scene_id, name, component):
    scene = scene_nodes(session)
    matches = [node for node in scene["nodes"] if node["name"] == name]
    if len(matches) > 1:
        raise RuntimeError(f"场景包含重名节点：{name}")
    if not matches:
        session.call("create_node", {"sceneId": scene_id, "name": name, "component": component})
        scene = scene_nodes(session)
        matches = [node for node in scene["nodes"] if node["name"] == name]
    if len(matches) != 1:
        raise RuntimeError(f"无法确认节点创建结果：{name}")
    return matches[0]["nodeId"]


def setup_view(session, scene_id, view):
    camera = ensure_node(session, scene_id, "Camera", "camera")
    identity = {"sceneId": scene_id, "nodeId": camera}
    eye, target, projection, size, fov = VIEWS[view]
    session.batch(
        [
            {
                "command": "set_transform",
                "arguments": identity
                | {"position": eye, "rotation": camera_rotation(eye, target), "scale": [1, 1, 1]},
            },
            {
                "command": "set_camera",
                "arguments": identity
                | {
                    "projection": projection,
                    "orthographicSize": size,
                    "fovDegrees": fov,
                    "nearClip": 0.1,
                    "farClip": 100,
                },
            },
        ]
    )
    session.call("set_active_camera", identity)


def setup_lighting(session, scene_id):
    lights = [
        ("Sun", "directional", (0, 8, 0), (-55, -35, 0), (1.0, 0.89, 0.72, 1), 3.6, 50),
        ("apartment_fill", "directional", (0, 8, 0), (-38, 145, 0), (0.73, 0.84, 1.0, 1), 0.65, 50),
        (
            "apartment_living_light",
            "point",
            (-2.25, 1.78, 0.66),
            (0, 0, 0),
            (1.0, 0.67, 0.36, 1),
            2.2,
            6,
        ),
        (
            "apartment_dining_light",
            "point",
            (3.6, 2.04, 2.5),
            (0, 0, 0),
            (1.0, 0.76, 0.47, 1),
            3.4,
            7,
        ),
        (
            "apartment_bedroom_light",
            "point",
            (-4.7, 2.65, -3.6),
            (0, 0, 0),
            (1.0, 0.78, 0.52, 1),
            2.3,
            6,
        ),
    ]
    for name, kind, position, rotation, color, intensity, light_range in lights:
        node_id = ensure_node(session, scene_id, name, "light")
        identity = {"sceneId": scene_id, "nodeId": node_id}
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
    if "set_render_settings" in session.tools:
        session.call(
            "set_render_settings",
            {
                "ambientColor": [0.80, 0.86, 1.0],
                "ambientIntensity": 0.35,
                "exposure": 1.15,
                "filmicTonemap": True,
                "directionalShadows": True,
                "shadowResolution": 2048,
                "shadowBias": 0.002,
                "clearColor": [0.10, 0.14, 0.16, 1.0],
            },
        )
    else:
        print("渲染设置工具尚未注册；本次使用现有渲染器设置。", flush=True)


def apply_style(session, scene_id, pairs):
    commands = []
    for piece, model in pairs:
        identity = {"sceneId": scene_id, "nodeId": model["nodeId"]}
        commands.append(
            {
                "command": "set_transform",
                "arguments": identity
                | {"position": piece.position, "rotation": piece.rotation, "scale": piece.scale},
            }
        )
        material = MATERIALS[piece.material]
        commands.append(
            {
                "command": "set_material_type",
                "arguments": identity
                | {
                    "slot": 0,
                    "path": (
                        "builtin/materials/default-unlit"
                        if material.unlit
                        else "builtin/materials/default-pbr"
                    ),
                },
            }
        )
        parameters = [("baseColor", "v4", list(material.color) + [1])]
        if not material.unlit:
            parameters.extend(
                [
                    ("roughness", "float", material.roughness),
                    ("metallic", "float", material.metallic),
                ]
            )
        for name, kind, value in parameters:
            commands.append(
                {
                    "command": "set_material_parameter",
                    "arguments": identity | {"slot": 0, "name": name, "type": kind, "value": value},
                }
            )
    session.batch(commands)


def build_models(session, scene_id, pieces):
    identity = {"sceneId": scene_id}
    models = session.call("modeling_get", identity).get("models", [])
    names = [model["name"] for model in models]
    if len(names) != len(set(names)):
        raise RuntimeError("当前建模场景存在同名模型，无法安全恢复")
    by_name = {model["name"]: model for model in models}
    for offset in range(0, len(pieces), 12):
        chunk = pieces[offset : offset + 12]
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
            current = session.call("modeling_get", identity)["models"]
            by_name = {model["name"]: model for model in current}
        for piece in chunk:
            model = by_name[piece.name]
            if piece.bevel and model["faceCount"] == 6:
                target = identity | {"modelId": model["modelId"]}
                mesh = session.call("modeling_get", target)
                edges = set()
                for face in mesh["faces"]:
                    vertices = face["vertices"]
                    for index, first in enumerate(vertices):
                        second = vertices[(index + 1) % len(vertices)]
                        edges.add(tuple(sorted((first, second), key=int)))
                session.call(
                    "modeling_edit",
                    target
                    | {
                        "operation": "bevel",
                        "selection": {"edges": sorted(edges)},
                        "parameters": {"width": piece.bevel, "segments": 3, "profile": 0.5},
                    },
                )
        smooth = [piece for piece in chunk if piece.shape != "cube" or piece.bevel]
        if smooth:
            session.batch(
                [
                    {
                        "command": "modeling_edit",
                        "arguments": identity
                        | {
                            "modelId": by_name[piece.name]["modelId"],
                            "operation": "auto_smooth",
                            "parameters": {"angleDegrees": 55},
                        },
                    }
                    for piece in smooth
                ]
            )
        # Geometry edits can rebuild nodes; query current identities before transform/material edits.
        current = session.call("modeling_get", identity)["models"]
        by_name = {model["name"]: model for model in current}
        apply_style(session, scene_id, [(piece, by_name[piece.name]) for piece in chunk])
        print(
            f"{min(offset + len(chunk), len(pieces))}/{len(pieces)} 模型完成 "
            f"({chunk[-1].group})",
            flush=True,
        )
    return session.call("modeling_get", identity)["models"]


def capture_frame(session, relative_path):
    required = {"capture_frame", "get_frame_capture"}
    if not required.issubset(session.tools):
        raise RuntimeError("当前 MCP 未注册截图工具；请使用已包含渲染截图模块的构建")
    expected = (session.args.project / "cache" / relative_path).resolve()
    deadline = time.monotonic() + session.args.timeout
    result = session.call("capture_frame", {"path": relative_path})
    while True:
        if result.get("path") != str(expected):
            raise RuntimeError("截图回执路径与本次请求不一致")
        state = result.get("state")
        if state == "complete":
            if not expected.is_file():
                raise RuntimeError("截图回执已完成，但输出文件不存在")
            with expected.open("rb") as source:
                if source.read(8) != b"\x89PNG\r\n\x1a\n":
                    raise RuntimeError("截图输出不是有效的 PNG 文件头")
            return {"state": "complete", "path": str(expected), "bytes": expected.stat().st_size}
        if state == "failed":
            raise RuntimeError("引擎截图失败；请查看运行日志后重新请求截图")
        if state != "pending":
            raise RuntimeError(f"引擎返回了未知截图状态：{state}")
        if time.monotonic() >= deadline:
            raise RuntimeError("截图仍在处理；请查询 get_frame_capture，不要重复发送原请求")
        time.sleep(0.1)
        result = session.call("get_frame_capture")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", type=Path, default=Path("build/ninja-engine-release/mcp_app"))
    parser.add_argument("--project", type=Path, default=Path("projects/showcase"))
    parser.add_argument("--runtime-dir", type=Path, default=Path("build/apartment-runtime"))
    parser.add_argument("--report", type=Path, default=Path("build/apartment-mcp-receipts.jsonl"))
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--dry-run", action="store_true", help="只打印构图与数量，不连接引擎")
    parser.add_argument("--start", action="store_true", help="无活动场景时调用 start_project")
    parser.add_argument("--groups", help="只处理逗号分隔的分区；默认全部")
    parser.add_argument("--view", choices=VIEWS, default="wide")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--view-only", action="store_true", help="只切换相机，不创建或保存模型")
    mode.add_argument(
        "--save-only", action="store_true", help="只通过 MCP 保存当前场景，不重建模型或改视角"
    )
    parser.add_argument(
        "--configure-view",
        action="store_true",
        help="切换视角时同时恢复公寓灯光和渲染设置；可与 --view-only 合用",
    )
    parser.add_argument(
        "--capture",
        metavar="CACHE_RELATIVE_PNG",
        help="通过 MCP 截取真实引擎帧；路径相对于项目 cache，等待实际写入完成",
    )
    parser.add_argument("--no-save", action="store_true", help="保留内存场景供迭代，不保存")
    parser.add_argument("--save-path", default="scenes/apartment.scene")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout 必须大于零")
    if args.save_only and (args.no_save or args.configure_view or args.groups):
        parser.error("--save-only 不能与 --no-save、--configure-view 或 --groups 合用")
    if args.capture:
        path = PurePosixPath(args.capture)
        if (
            path.is_absolute()
            or path.suffix != ".png"
            or "\\" in args.capture
            or "\0" in args.capture
            or ":" in args.capture
            or any(part in ("", ".", "..") for part in args.capture.split("/"))
        ):
            parser.error("--capture 必须是 cache 内的相对 .png 路径，不允许目录越界")
    plan = apartment_plan()
    counts = Counter(piece.group for piece in plan.pieces)
    if args.groups:
        selected = set(args.groups.split(","))
        unknown = selected - set(counts)
        if unknown:
            parser.error("未知分区：" + ", ".join(sorted(unknown)))
        plan.pieces = [piece for piece in plan.pieces if piece.group in selected]
    if args.dry_run:
        print(
            json.dumps(
                {
                    "models": len(plan.pieces),
                    "groups": dict(counts),
                    "beveledModels": sum(bool(piece.bevel) for piece in plan.pieces),
                    "shapes": dict(Counter(piece.shape for piece in plan.pieces)),
                    "view": args.view,
                    "savePath": args.save_path,
                    "saveOnly": args.save_only,
                    "configureView": args.configure_view,
                    "capture": args.capture,
                },
                ensure_ascii=False,
                indent=2,
            )
        )
        return 0
    try:
        with McpSession(args) as session:
            scene = session.call("get_scene").get("scene")
            if not scene and args.start:
                session.call("start_project")
                scene = scene_nodes(session)
            if not scene:
                raise RuntimeError("项目没有活动场景；先启动编辑器及项目，或传入 --start")
            scene_id = scene["sceneId"]
            report = {"success": True, "sceneId": scene_id, "report": str(args.report.resolve())}
            if args.save_only:
                session.call("save_scene", {"sceneId": scene_id, "path": args.save_path})
                report["saved"] = args.save_path
            else:
                setup_view(session, scene_id, args.view)
                report["view"] = args.view
            if args.configure_view or (not args.view_only and not args.save_only):
                setup_lighting(session, scene_id)
            if not args.view_only and not args.save_only:
                models = build_models(session, scene_id, plan.pieces)
                if not args.no_save:
                    session.call("save_scene", {"sceneId": scene_id, "path": args.save_path})
                report.update(
                    {
                        "models": len(models),
                        "triangles": sum(model["triangleCount"] for model in models),
                        "saved": None if args.no_save else args.save_path,
                    }
                )
            if args.capture:
                report["capture"] = capture_frame(session, args.capture)
            report["mcpCalls"] = session.calls
            print(json.dumps(report, ensure_ascii=False, indent=2))
        return 0
    except (OSError, ValueError, RuntimeError, KeyError, subprocess.TimeoutExpired) as error:
        print(
            f"公寓创建未完成：{error}\n已完成操作写入回执；修复后可重跑，按名称复用已有模型。",
            file=sys.stderr,
        )
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
