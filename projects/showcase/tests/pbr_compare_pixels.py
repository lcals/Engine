"""Compare two 1280x720 PBR six-sphere captures using Blender Python.

Usage: blender -b -t 1 --python projects/showcase/tests/pbr_compare_pixels.py -- engine.png blender.png output-dir [case-labels.json]
Optional JSON supplies six labels in top-to-bottom, left-to-right screen order.
Read images without color conversion; report display-encoded values, not linear radiance.
"""
import json
from pathlib import Path
import sys

import bpy
import numpy as np


def load(path):
    image = bpy.data.images.load(str(path.resolve()), check_existing=False)
    image.colorspace_settings.name = "Non-Color"
    width, height = image.size[:]
    rgba = np.empty(width * height * 4, dtype=np.float32)
    image.pixels.foreach_get(rgba)
    return width, height, rgba.reshape(height, width, 4)[:, :, :3].copy()


def measure(rgb, mask):
    luma = rgb @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    values = luma[mask]
    peak = float(values.max())
    bright = mask & (luma >= max(peak * 0.8, 1.0 / 255))
    y, x = np.nonzero(bright)
    return {
        "mean_rgb_255": (rgb[mask].mean(axis=0) * 255).astype(float).tolist(),
        "mean_luma_255": float(values.mean() * 255),
        "p95_luma_255": float(np.percentile(values, 95) * 255),
        "peak_luma_255": peak * 255,
        "bright_pixels_over_80pct_peak": int(bright.sum()),
        "bright_centroid_top_left": [float(x.mean()), float(720 - y.mean())] if x.size else None,
    }


def main():
    args = sys.argv[sys.argv.index("--") + 1:]
    if len(args) not in (3, 4):
        raise SystemExit("expected: engine.png blender.png output-dir [case-labels.json]")
    engine_path, blender_path, out = map(Path, args[:3])
    labels = json.loads(Path(args[3]).read_text()) if len(args) == 4 else [str(i + 1) for i in range(6)]
    if not isinstance(labels, list) or len(labels) != 6 or not all(isinstance(v, str) for v in labels):
        raise SystemExit("labels must be a JSON array of six strings")
    width, height, engine = load(engine_path)
    w2, h2, blender = load(blender_path)
    if (width, height) != (1280, 720) or (w2, h2) != (width, height):
        raise SystemExit(f"expected identical 1280x720 images, got {(width, height)} and {(w2, h2)}")
    out.mkdir(parents=True, exist_ok=True)
    delta = np.abs(engine - blender)
    yy, xx = np.ogrid[:height, :width]
    cases = []
    centers = [(420, 250), (640, 250), (860, 250),
               (420, 470), (640, 470), (860, 470)]
    for label, (cx, top_y) in zip(labels, centers):
        mask = (xx - cx) ** 2 + (yy - (height - top_y)) ** 2 <= 72 ** 2
        differences = delta[mask]
        cases.append({"label": label, "center_top_left": [cx, top_y], "radius_px": 72,
                      "pixels": int(mask.sum()),
                      "mae_rgb_255": float(differences.mean() * 255),
                      "rmse_rgb_255": float(np.sqrt(np.mean(differences ** 2)) * 255),
                      "max_difference_255": float(differences.max() * 255),
                      "engine": measure(engine, mask), "blender": measure(blender, mask)})
    report = {"engine": str(engine_path), "blender": str(blender_path),
              "size": [width, height], "cases": cases,
              "notes": "Display-encoded RGB, no alignment or color correction; light units, tone mapping, geometry and antialiasing may differ."}
    (out / "comparison.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    image = bpy.data.images.new("absolute_pixel_difference", width=width, height=height, alpha=True)
    image.colorspace_settings.name = "Non-Color"
    rgba = np.ones((height, width, 4), dtype=np.float32)
    rgba[:, :, :3] = delta
    image.pixels.foreach_set(rgba.ravel())
    image.filepath_raw = str((out / "absolute-difference.png").resolve())
    image.file_format = "PNG"
    image.save()
    print(f"Compared {len(cases)} cases: {out / 'comparison.json'}")


if __name__ == "__main__":
    main()
