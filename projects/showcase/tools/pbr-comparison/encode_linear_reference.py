"""将 Blender 的线性 EXR 按引擎同一显示变换编码，保留原始参考。

Blender -b -t 1 --python encode_linear_reference.py -- input.exr output.png

仅适用于曝光 1、Reinhard、gamma 2.2 且无 Bloom 的不透明受光场景。
此转换不调整光照或材质，也不等同于线性浮点缓冲对照。
"""
import sys
from pathlib import Path

import bpy
import numpy as np


def main():
    args = sys.argv[sys.argv.index('--') + 1:]
    if len(args) != 2:
        raise SystemExit('expected: input.exr output.png')
    source, destination = (Path(arg).resolve() for arg in args)
    if source.suffix.lower() != '.exr' or destination.suffix.lower() != '.png':
        raise SystemExit('expected linear EXR input and PNG output')
    image = bpy.data.images.load(str(source), check_existing=False)
    image.colorspace_settings.name = 'Non-Color'
    width, height = image.size[:]
    rgba = np.empty(width * height * 4, dtype=np.float32)
    image.pixels.foreach_get(rgba)
    rgba = rgba.reshape((height, width, 4))
    if not np.isfinite(rgba).all():
        raise SystemExit('EXR contains non-finite values')
    linear = np.maximum(rgba[:, :, :3], 0.0)
    rgba[:, :, :3] = np.power(linear / (1.0 + linear), 1.0 / 2.2)
    rgba[:, :, 3] = 1.0
    destination.parent.mkdir(parents=True, exist_ok=True)
    result = bpy.data.images.new('engine_display_reference', width=width, height=height, alpha=True)
    result.colorspace_settings.name = 'Non-Color'
    result.pixels.foreach_set(rgba.ravel())
    result.filepath_raw = str(destination)
    result.file_format = 'PNG'
    result.save()
    print(f'ENCODED_REFERENCE {width}x{height}: {destination}', flush=True)


if __name__ == '__main__':
    main()
