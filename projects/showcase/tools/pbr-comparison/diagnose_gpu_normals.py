"""Render real GPU shading and geometric normals in an isolated public project.

Uses the regular project-shader resource pipeline and MCP screenshot. Output is
8-bit SDR after the normal display transform; it is not floating-point readback.
"""
from __future__ import annotations
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[4]
SOURCE = ROOT / 'projects/showcase/assets'
sys.path.insert(0, str(ROOT / 'scripts'))
from editor_mcp_session import EditorMcpSession

work = ROOT / 'build/material-comparison/gpu-normal-field' / str(time.time_ns())
project, runtime = work/'project', work/'runtime'
for part in ('assets', 'cache', 'settings'):
    (project/part).mkdir(parents=True)
runtime.mkdir()
for name, content in {
    'import.toml': 'import_threads = 8\n',
    'editor.toml': '[editor]\ntarget_frame_rate = 60\n',
    'runtime.toml': 'version = 1\n[defaults.engine]\nwidth = 1280\nheight = 720\ntarget_frame_rate = 60\n[profiles.desktop]\n[profiles.mobile]\n[overrides]\n',
}.items():
    (project/'settings'/name).write_text(content)
shutil.copytree(SOURCE/'shaders', project/'assets/shaders')
for rel in ('meshes/pbr-comparison-sphere.mesh', 'textures/pbr-test-normal.png'):
    for suffix in ('', '.meta'):
        target = project/'assets'/f'{rel}{suffix}'
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(SOURCE/f'{rel}{suffix}', target)
base_scene = (SOURCE/'scenes/pbr-test-normal-orm.scene').read_text().split('[[prefab_instances]]')[0]
mesh = 'a0e3606e-b1b6-440b-b79e-fa7ee383ab0e'
for kind in ('shading', 'geometric'):
    shader_guid, frag_guid, material_guid, scene_guid = [str(uuid.uuid4()) for _ in range(4)]
    shader = (SOURCE/'shaders/pbr.shader').read_text().replace('5c3a334f-6214-4e4e-a964-f5a3eeff0629', frag_guid)
    shader_path = project/f'assets/shaders/normal-{kind}.shader'
    shader_path.write_text(shader)
    shader_path.with_name(shader_path.name+'.meta').write_text(f'guid = "{shader_guid}"\nimporter = "shader"\nmeta_version = 1\n')
    frag = project/f'assets/shaders/normal-{kind}.frag.sc'
    expression = 'surface.normal' if kind == 'shading' else 'normalize(v_world_normal)'
    frag.write_text('''$input v_world_position, v_world_normal, v_world_tangent, v_texcoord0, v_color0, v_lightmap_uv
#include "pbr_surface.sh"
uniform vec4 u_camera_position;
void main()
{
    vec3 view_dir = pbr_view_direction(u_camera_position.xyz, v_world_position);
    PbrSurface surface = sample_pbr_surface(v_texcoord0, v_color0, v_world_normal, v_world_tangent, view_dir);
    vec3 diagnostic = NORMAL_EXPR;
    if (u_emission.w > 1000.0) diagnostic += surface.emission;
    gl_FragData[0] = vec4(diagnostic * 0.5 + 0.5, 1.0);
    gl_FragData[1] = vec4_splat(0.0);
}
'''.replace('NORMAL_EXPR',expression))
    frag.with_name(frag.name+'.meta').write_text(f'guid = "{frag_guid}"\nimporter = "source"\nmeta_version = 1\n')
    material = (SOURCE/'materials/pbr-test-normal-orm-4.material').read_text().replace('00000000-0000-4000-8000-000000000001', shader_guid)
    mat = project/f'assets/materials/normal-{kind}.material'; mat.parent.mkdir(parents=True, exist_ok=True)
    mat.write_text(material); mat.with_name(mat.name+'.meta').write_text(f'guid = "{material_guid}"\nimporter = "material"\nmeta_version = 1\n')
    scene = project/f'assets/scenes/normal-{kind}.scene'; scene.parent.mkdir(parents=True, exist_ok=True)
    scene.write_text(base_scene + f'''[[nodes]]
id = 4
name = "Same source sphere"
parent = 0
[[nodes.components]]
type = "transform"
position = [-2.2, -1.1, 0.0]
[[nodes.components]]
type = "mesh_renderer"
mesh = "{mesh}"
material = "{material_guid}"
''')
    scene.with_name(scene.name+'.meta').write_text(f'guid = "{scene_guid}"\nimporter = "scene"\nmeta_version = 1\n')
bin_dir=ROOT/'build/local-ninja-release'
log=(work/'editor.log').open('wb')
editor=subprocess.Popen([str(bin_dir/'editor_app'), '--project', str(project), '--runtime-dir', str(runtime), '--width', '1280', '--height', '720'], stdout=log, stderr=subprocess.STDOUT)
report={'complete':False}
try:
    with EditorMcpSession(bin_dir/'mcp_app', project, runtime, work/'receipts.jsonl', timeout=90) as session:
        deadline=time.monotonic()+90
        while True:
            if editor.poll() is not None: raise RuntimeError(f'editor exited {editor.returncode}')
            try: session.call('get_scene_preview'); break
            except RuntimeError:
                if time.monotonic()>deadline: raise
                time.sleep(.25)
        for kind in ('shading','geometric'):
            report[kind+'_open']=session.call('open_scene_preview', {'path':f'scenes/normal-{kind}.scene'})
            report[kind+'_capture']=session.capture(f'{kind}.png')
        report['close']=session.call('close_editor', {'delayMs':500})
        report['exit']=editor.wait(timeout=15)
        if report['exit'] != 0: raise RuntimeError(f'editor exit {report["exit"]}')
        report['complete']=True
except Exception as error:
    report['error']=str(error)
    raise
finally:
    if editor.poll() is None:
        editor.terminate()
        try: editor.wait(timeout=5)
        except subprocess.TimeoutExpired: editor.kill(); editor.wait()
    log.close()
    (work/'report.json').write_text(json.dumps(report,indent=2,ensure_ascii=False))
    print(json.dumps({'work':str(work),'complete':report['complete'],'error':report.get('error')}),flush=True)
