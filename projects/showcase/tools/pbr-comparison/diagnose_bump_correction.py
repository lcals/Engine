"""Isolate Blender Cycles bump-correction's contribution to an existing reference.

Run in Blender with the reference .blend already opened. This diagnostic disables
one Cycles material option in memory and writes a separate linear EXR. It does
not save or overwrite the input .blend and is not a replacement for the default
reference. Encode and compare the result using the existing comparison scripts.
"""

import argparse
import json
from pathlib import Path
import sys

import bpy


def main():
    if '--' not in sys.argv:
        raise SystemExit('expected -- --output-dir <directory>')
    parser = argparse.ArgumentParser()
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    scene = bpy.context.scene
    if scene.render.engine != 'CYCLES':
        raise SystemExit('reference scene must use Cycles')
    materials = {m.name: bool(m.cycles.use_bump_map_correction)
                 for m in bpy.data.materials if m.use_nodes}
    if not materials or not all(materials.values()):
        raise SystemExit('expected node materials with bump correction enabled')
    objects = {o.name: {'shadow_terminator_offset': o.cycles.shadow_terminator_offset,
                         'shadow_terminator_geometry_offset': o.cycles.shadow_terminator_geometry_offset,
                         'visible_shadow': o.visible_shadow}
               for o in bpy.data.objects if o.type == 'MESH'}
    info = {'blender_version': bpy.app.version_string,
            'source_file': bpy.data.filepath,
            'source_bump_correction': materials, 'object_settings': objects,
            'resolution': [scene.render.resolution_x, scene.render.resolution_y,
                           scene.render.resolution_percentage],
            'samples': scene.cycles.samples,
            'diagnostic_bump_correction': False}
    (output / 'settings.json').write_text(json.dumps(info, indent=2) + '\n')
    for material in bpy.data.materials:
        if material.use_nodes:
            material.cycles.use_bump_map_correction = False
    scene.render.image_settings.file_format = 'OPEN_EXR'
    scene.render.image_settings.color_mode = 'RGB'
    scene.render.image_settings.color_depth = '32'
    bpy.ops.render.render()
    path = output / 'blender-bump-off.exr'
    bpy.data.images['Render Result'].save_render(str(path), scene=scene)
    print(f'BUMP_DIAGNOSTIC {path}', flush=True)


if __name__ == '__main__':
    main()
