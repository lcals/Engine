"""Encode Blender linear EXR with the engine display transform while preserving the reference.

Blender -b -t 1 --python encode_linear_reference.py -- input.exr output.png
Blender -b -t 1 --python encode_linear_reference.py -- --all
Applies only to opaque lit scenes with exposure 1, Reinhard, exact sRGB and no Bloom.
Does not change lighting/materials and is not a linear floating-point buffer comparison.
"""
import sys
from pathlib import Path

import bpy
import numpy as np


ROOT = Path(__file__).resolve().parents[4]
HISTORICAL = ROOT / 'build/material-comparison/display-matched'


def encode(source, destination):
    source, destination = source.resolve(), destination.resolve()
    if source.suffix.lower() != '.exr' or destination.suffix.lower() != '.png':
        raise SystemExit('expected linear EXR input and PNG output')
    if destination.is_relative_to(HISTORICAL):
        raise SystemExit('refusing to overwrite historical gamma 2.2 references')
    image = bpy.data.images.load(str(source), check_existing=False)
    image.colorspace_settings.name = 'Non-Color'
    width, height = image.size[:]
    rgba = np.empty(width * height * 4, dtype=np.float32)
    image.pixels.foreach_get(rgba)
    rgba = rgba.reshape((height, width, 4))
    if not np.isfinite(rgba).all():
        raise SystemExit('EXR contains non-finite values')
    linear = np.maximum(rgba[:, :, :3], 0.0)
    mapped = linear / (1.0 + linear)
    rgba[:, :, :3] = np.where(mapped <= 0.0031308, 12.92 * mapped,
                             1.055 * np.power(mapped, 1.0 / 2.4) - 0.055)
    rgba[:, :, 3] = 1.0
    destination.parent.mkdir(parents=True, exist_ok=True)
    result = bpy.data.images.new('engine_display_reference', width=width, height=height, alpha=True)
    result.colorspace_settings.name = 'Non-Color'
    result.pixels.foreach_set(rgba.ravel())
    result.filepath_raw = str(destination)
    result.file_format = 'PNG'
    result.save()
    bpy.data.images.remove(image)
    bpy.data.images.remove(result)
    print(f'ENCODED_REFERENCE {width}x{height}: {destination}', flush=True)


def main():
    args = sys.argv[sys.argv.index('--') + 1:]
    if args == ['--all']:
        sources = sorted((ROOT / 'build/material-comparison/expanded').glob('blender-*-linear.exr'))
        if not sources:
            raise SystemExit('no linear EXR references found')
        for source in sources:
            name = source.name.removesuffix('-linear.exr') + '.png'
            encode(source, ROOT / 'build/material-comparison/display-srgb' / name)
    elif len(args) == 2:
        encode(Path(args[0]), Path(args[1]))
    else:
        raise SystemExit('expected: input.exr output.png, or --all')


if __name__ == '__main__':
    main()
