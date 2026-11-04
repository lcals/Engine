"""Compare diagnostic GPU normals, Blender EXR and existing SDR renders.

Run using Blender's Python after diagnose_gpu_normals.py:
  Blender -b -t 2 --python-exit-code 1 --python compare_gpu_normals.py -- <work>
The diagnostic PNGs contain tone-mapped, quantized normals, not HDR readback.
"""
import json
import math
from pathlib import Path
import sys

import bpy
import numpy as np

ROOT = Path(__file__).resolve().parents[4]
work = Path(sys.argv[sys.argv.index('--') + 1]).resolve()


def load(path, noncolor=False):
    image = bpy.data.images.load(str(path), check_existing=False)
    if noncolor:
        image.colorspace_settings.name = 'Non-Color'
    width, height = image.size
    pixels = np.empty(width * height * 4, dtype=np.float32)
    image.pixels.foreach_get(pixels)
    return pixels.reshape(height, width, 4)


def decode_normal(pixel):
    # The scene uses Reinhard then gamma 2.2; inverse is approximate due to
    # 8-bit PNG quantization. Map the encoded normal from [0,1] to [-1,1].
    transformed = np.power(np.clip(pixel[:3], 0, 1), 2.2)
    linear = transformed / np.maximum(1 - transformed, 1e-10)
    normal = linear * 2 - 1
    return normal / np.linalg.norm(normal)


def pixel_at_reference_resolution(image, x, y):
    height, width = image.shape[:2]
    if width not in (1280, 2560) or height != width * 9 // 16:
        raise ValueError(f'Unexpected screenshot size {width}x{height}')
    factor = width // 1280
    return image[factor*y + factor//2, factor*x + factor//2, :3]


shade = load(work/'project/cache/shading.png', True)
base = load(work/'project/cache/geometric.png', True)
blender = load(ROOT/'build/material-comparison/normal-field/blender-normal-field.exr')
engine_image = load(ROOT/'projects/showcase/cache/material-comparison/engine-normal-orm-standard-v2.png', True)
reference = load(ROOT/'build/material-comparison/display-matched/blender-normal-orm-gltf-same-mesh.png', True)
old = json.loads((ROOT/'build/material-comparison/normal-field/report.json').read_text())
light = np.array(old['light_direction_engine'])
if blender.shape[:2] != (720, 1280):
    raise ValueError(f'Unexpected Blender normal EXR dimensions: {blender.shape}')
rows = []
for y in range(181, 320, 2):
    for x in range(351, 490, 2):
        if (x - 420)**2 + (y - 250)**2 >= 70**2:
            continue
        normal = decode_normal(pixel_at_reference_resolution(shade, x, y))
        base_normal = decode_normal(pixel_at_reference_resolution(base, x, y))
        reference_normal = blender[y, x, :3] * 2 - 1
        reference_normal /= np.linalg.norm(reference_normal)
        angle = math.degrees(math.acos(np.clip(normal @ reference_normal, -1, 1)))
        engine_pixel = pixel_at_reference_resolution(engine_image, x, y)
        blender_pixel = pixel_at_reference_resolution(reference, x, y)
        rows.append({'pixel': [x, y], 'normal': normal.tolist(), 'base_normal': base_normal.tolist(),
                     'angle': angle, 'n_dot_l': float(normal @ light),
                     'base_dot_l': float(base_normal @ light),
                     'engine_brightness': float(engine_pixel.mean()),
                     'blender_brightness': float(blender_pixel.mean())})
angles = np.array([row['angle'] for row in rows])
horizon = [row for row in rows if row['n_dot_l'] > .02 and row['base_dot_l'] < -.02]
front = [row for row in rows if row['n_dot_l'] > .02 and row['base_dot_l'] > .02]


def mean(rows, key):
    return float(np.mean([row[key] for row in rows])) if rows else None


report = {'light': light.tolist(), 'count': len(rows),
          'mean_angle': float(angles.mean()), 'p95_angle': float(np.percentile(angles, 95)),
          'max_angle': float(angles.max()), 'horizon_count': len(horizon), 'front_count': len(front),
          'horizon_brightness_engine': mean(horizon, 'engine_brightness'),
          'horizon_brightness_blender': mean(horizon, 'blender_brightness'),
          'front_brightness_engine': mean(front, 'engine_brightness'),
          'front_brightness_blender': mean(front, 'blender_brightness'),
          'sample_horizon': horizon[:5], 'samples': rows}
(work/'compare.json').write_text(json.dumps(report, indent=2) + '\n')
print('GPU_NORMAL_RESULT', json.dumps({k: v for k, v in report.items() if k != 'samples'}))
