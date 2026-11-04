"""Bin existing paired SDR captures by measured GPU base-normal light angle.

The GPU normals in compare.json were inferred from quantized diagnostic PNGs;
this is a spatial diagnostic, not linear HDR or direct GPU buffer readback.
Run with Blender Python and pass compare.json, engine PNG, Blender PNG, output.
"""
import argparse
import json
from pathlib import Path
import sys

import bpy
import numpy as np


def load_png(path):
    image = bpy.data.images.load(str(path), check_existing=False)
    image.colorspace_settings.name = 'Non-Color'
    width, height = image.size[:]
    if (width, height) != (1280, 720):
        raise ValueError(f'expected 1280x720 capture: {path}')
    pixels = np.empty(width * height * 4, dtype=np.float32)
    image.pixels.foreach_get(pixels)
    return pixels.reshape(height, width, 4)


def main():
    parser = argparse.ArgumentParser()
    for name in ('normals', 'engine', 'reference', 'output'):
        parser.add_argument(name, type=Path)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    samples = json.loads(args.normals.read_text())['samples']
    engine, reference = load_png(args.engine), load_png(args.reference)
    records = []
    for sample in samples:
        x, y = sample['pixel']
        delta = (engine[y, x, :3] - reference[y, x, :3]) * 255.0
        records.append((sample['base_dot_l'], float(np.mean(np.abs(delta))),
                        float(np.mean(delta))))
    records = np.asarray(records)
    bounds = [-1, -.2, -.05, 0, .05, .1, .2, .4, .6, .8, 1.001]
    bins = []
    for lower, upper in zip(bounds, bounds[1:]):
        values = records[(records[:, 0] >= lower) & (records[:, 0] < upper)]
        if len(values):
            bins.append({'base_dot_l': [lower, upper], 'samples': len(values),
                         'mae_rgb_255': float(np.mean(values[:, 1])),
                         'mean_engine_minus_reference_rgb_255': float(np.mean(values[:, 2]))})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({'samples': len(records), 'bins': bins}, indent=2) + '\n')
    print(f'RESIDUAL_BINS {args.output}', flush=True)


if __name__ == '__main__':
    main()
