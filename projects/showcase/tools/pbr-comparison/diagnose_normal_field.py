"""Compare Blender's rendered normal field against a CPU model of the engine shader.

Uses the already generated normal-orm-gltf-same-mesh Blender scene. Run with:
  Blender -b -t 8 --python projects/showcase/tools/pbr-comparison/diagnose_normal_field.py
The raster model does not read the engine GPU; it isolates Blender's normal-map
node from the BRDF and display transform for diagnosis, not a conformance test.
"""
import json
import math
import tomllib
from pathlib import Path

import bpy
import numpy as np

repo = Path(__file__).resolve().parents[4]
case = repo / 'build/material-comparison/expanded/blender-normal-orm-gltf-same-mesh.blend'
out = repo / 'build/material-comparison/normal-field'
out.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(case))
scene = bpy.context.scene
# Fourth sphere uses normalScale=1 and is in the bottom-left cell.
sphere = bpy.data.objects['Sphere_4']
for obj in scene.objects:
    if obj.name.startswith('Sphere_') and obj != sphere:
        obj.hide_render = True
scene.render.resolution_percentage = 100
scene.cycles.samples = 128
scene.cycles.use_denoising = False
scene.world.node_tree.nodes['Background'].inputs['Color'].default_value = (0, 0, 0, 1)
material = sphere.data.materials[0]
nodes = material.node_tree.nodes
links = material.node_tree.links
normal_map = next(node for node in nodes if node.bl_idname == 'ShaderNodeNormalMap')
# A normal map node outputs a world-space normal. Convert Blender xyz to engine
# xyz: (x, z, -y), then encode [-1,1] as [0,1] in an emission shader.
sep = nodes.new('ShaderNodeSeparateXYZ')
links.new(normal_map.outputs['Normal'], sep.inputs['Vector'])
neg = nodes.new('ShaderNodeMath'); neg.operation = 'MULTIPLY'; neg.inputs[1].default_value = -1
links.new(sep.outputs['Y'], neg.inputs[0])
combine = nodes.new('ShaderNodeCombineXYZ')
links.new(sep.outputs['X'], combine.inputs['X'])
links.new(sep.outputs['Z'], combine.inputs['Y'])
links.new(neg.outputs[0], combine.inputs['Z'])
scale = nodes.new('ShaderNodeVectorMath'); scale.operation = 'SCALE'; scale.inputs[3].default_value = .5
links.new(combine.outputs[0], scale.inputs[0])
add = nodes.new('ShaderNodeVectorMath'); add.operation = 'ADD'; add.inputs[1].default_value = (.5,.5,.5)
links.new(scale.outputs[0], add.inputs[0])
emission = nodes.new('ShaderNodeEmission')
links.new(add.outputs[0], emission.inputs['Color'])
output = next(node for node in nodes if node.bl_idname == 'ShaderNodeOutputMaterial')
links.new(emission.outputs[0], output.inputs['Surface'])
scene.render.image_settings.file_format = 'OPEN_EXR'
scene.render.image_settings.color_mode = 'RGBA'
scene.render.image_settings.color_depth = '32'
scene.render.filepath = str(out / 'blender-normal-field.exr')
bpy.ops.render.render(write_still=True)
# Reload saved linear EXR; use nearest interior pixels and ignore silhouette.
image = bpy.data.images.load(scene.render.filepath, check_existing=False)
w, h = image.size
pixels = np.empty(w*h*4, dtype=np.float32)
image.pixels.foreach_get(pixels)
pixels = pixels.reshape((h,w,4))
with (repo / 'projects/showcase/assets/meshes/pbr-comparison-sphere.mesh').open('rb') as stream:
    data = tomllib.load(stream)
vertices = np.asarray(data['vertices'], dtype=np.float64)
pos = vertices[:,:3]; normals = vertices[:,3:6]
uv = np.asarray(data['texcoords'], dtype=np.float64)
triangles = np.asarray(data['indices'], dtype=np.int32).reshape(-1,3)
tan = np.zeros_like(pos); bitan = np.zeros_like(pos)
for tri in triangles:
    i,j,k = tri
    e1,e2 = pos[j]-pos[i],pos[k]-pos[i]
    d1,d2 = uv[j]-uv[i],uv[k]-uv[i]
    det = d1[0]*d2[1]-d1[1]*d2[0]
    if abs(det) <= 1e-20:
        continue
    t = (e1*d2[1]-e2*d1[1])/det
    b = (e2*d1[0]-e1*d2[0])/det
    tan[tri] += t; bitan[tri] += b
normals /= np.linalg.norm(normals,axis=1)[:,None]
tan -= normals*np.einsum('ij,ij->i', normals,tan)[:,None]
length = np.linalg.norm(tan,axis=1)
for i in np.flatnonzero(length <= 1e-20):
    axis = np.array((0,0,1) if abs(normals[i,2]) < .9 else (0,1,0))
    tan[i] = np.cross(axis,normals[i]); length[i] = np.linalg.norm(tan[i])
tan /= length[:,None]
sign = np.where(np.einsum('ij,ij->i',np.cross(normals,tan),bitan)<0,-1.,1.)
# Compare at pixel centers on a coarse grid safely inside the sphere; orthographic
# camera: 100 px per engine unit, center of sphere at (420,250).
coords = [(x,y) for y in range(190,311,5) for x in range(360,481,5) if (x-420)**2+(y-250)**2 < 55**2]
# The engine shader uses +Z of the light world transform as the vector towards
# the light. Reproduce the WorldSystem Euler matrix from the scene, and compare
# to Blender's actual light matrix (not the light's descriptive position).
with (repo / 'projects/showcase/assets/scenes/pbr-test-normal-orm.scene').open('rb') as stream:
    engine_scene = tomllib.load(stream)
light_node = next(node for node in engine_scene['nodes'] if node['name'] == 'Directional light')
rotation = next(c['rotation'] for c in light_node['components'] if c['type'] == 'transform')
rx, ry, rz = (math.radians(v) for v in rotation)
sx,cx,sy,cy,sz,cz = math.sin(rx),math.cos(rx),math.sin(ry),math.cos(ry),math.sin(rz),math.cos(rz)
engine_light = np.array((cz*sy*cx + sz*sx, sz*sy*cx - cz*sx, cy*cx))
engine_light /= np.linalg.norm(engine_light)
blender_light = bpy.data.objects['White directional light']
blender_vector = blender_light.matrix_world.to_3x3() @ __import__('mathutils').Vector((0,0,1))
blender_light_engine = np.array((blender_vector.x, blender_vector.z, -blender_vector.y))
blender_light_engine /= np.linalg.norm(blender_light_engine)
light_angle = math.degrees(math.acos(np.clip(engine_light @ blender_light_engine,-1,1)))
result=[]
map_normal=np.array((191.,128.,238.))/255*2-1
map_normal/=np.linalg.norm(map_normal)
for x,y in coords:
    p=np.array(((x+.5-420)/100,(y+.5-250)/100))
    best=None
    for tri in triangles:
        xy=pos[tri,:2]; ab=xy[1]-xy[0]; ac=xy[2]-xy[0]; ap=p-xy[0]
        det=ab[0]*ac[1]-ab[1]*ac[0]
        if abs(det)<1e-14: continue
        v=(ap[0]*ac[1]-ap[1]*ac[0])/det
        u=(ab[0]*ap[1]-ab[1]*ap[0])/det
        weights=np.array((1-v-u,v,u))
        if weights.min() < -1e-7: continue
        z=weights @ pos[tri,2]
        if best is None or z>best[0]: best=(z,tri,weights)
    if best is None: continue
    _,tri,weights=best
    n=weights @ normals[tri]; n/=np.linalg.norm(n)
    t=weights @ tan[tri]; t-=n*np.dot(n,t); t/=np.linalg.norm(t)
    hand=weights @ sign[tri]
    b=np.cross(n,t)*hand
    predicted=map_normal[0]*t+map_normal[1]*b+map_normal[2]*n
    predicted/=np.linalg.norm(predicted)
    observed=pixels[y,x,:3]*2-1
    observed/=np.linalg.norm(observed)
    angle=np.degrees(np.arccos(np.clip(np.dot(predicted,observed),-1,1)))
    engine_ndotl = max(0., float(predicted @ engine_light))
    blender_ndotl = max(0., float(observed @ blender_light_engine))
    result.append({'pixel':[x,y], 'angle_degrees':float(angle), 'predicted':predicted.tolist(), 'blender':observed.tolist(),
                   'engine_ndotl':engine_ndotl, 'blender_ndotl':blender_ndotl, 'ndotl_abs_error':abs(engine_ndotl-blender_ndotl)})
angles=np.array([r['angle_degrees'] for r in result])
ndotl_errors=np.array([r['ndotl_abs_error'] for r in result])
report={'comparison':'CPU reproduction of engine per-fragment normal vs Blender Cycles normal-map emission',
        'light_direction_engine':engine_light.tolist(), 'light_direction_blender_in_engine_space':blender_light_engine.tolist(),
        'light_direction_angle_degrees':light_angle,
        'ndotl_mean_abs_error':float(ndotl_errors.mean()), 'ndotl_p95_abs_error':float(np.percentile(ndotl_errors,95)),
        'ndotl_max_abs_error':float(ndotl_errors.max()),
        'count':len(result),'mean_degrees':float(angles.mean()),'median_degrees':float(np.median(angles)),
        'p95_degrees':float(np.percentile(angles,95)),'max_degrees':float(angles.max()),
        'samples':result,'caveat':'No engine GPU readback; rasterization, sampling and Cycles normal handling may differ.'}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print('NORMAL_FIELD_RESULT',json.dumps({k:v for k,v in report.items() if k!='samples'}))
