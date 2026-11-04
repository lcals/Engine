"""Compare the current mesh fallback calculation with Blender's MikkTSpace loops.

Run with Blender in background mode. This independently reimplements the current
MeshUtils fallback for diagnosis; it does not read back the engine GPU buffer.
"""

import json
import math
import sys
import tomllib
from pathlib import Path

import bpy
from mathutils import Vector


repo = Path(__file__).resolve().parents[4]
source = repo / 'projects/showcase/assets/meshes/pbr-comparison-sphere.mesh'
output = Path(sys.argv[sys.argv.index('--') + 1]) if '--' in sys.argv else repo / 'build/material-comparison/expanded/tangent-frame.json'
with source.open('rb') as stream:
    data = tomllib.load(stream)
if 'tangents' in data:
    raise SystemExit('This diagnostic expects a mesh using generated fallback tangents')

vertices = data['vertices']
uv = data['texcoords']
indices = data['indices']
positions = [Vector(v[:3]) for v in vertices]
normals = [Vector(v[3:6]).normalized() for v in vertices]
tan = [Vector((0, 0, 0)) for _ in vertices]
bitan = [Vector((0, 0, 0)) for _ in vertices]
degenerate_uv_faces = 0
for offset in range(0, len(indices), 3):
    ids = indices[offset:offset + 3]
    i, j, k = ids
    e1, e2 = positions[j] - positions[i], positions[k] - positions[i]
    du1, dv1 = uv[j][0] - uv[i][0], uv[j][1] - uv[i][1]
    du2, dv2 = uv[k][0] - uv[i][0], uv[k][1] - uv[i][1]
    determinant = du1 * dv2 - dv1 * du2
    if abs(determinant) <= 1.0e-20:
        degenerate_uv_faces += 1
        continue
    t = (e1 * dv2 - e2 * dv1) / determinant
    b = (e2 * du1 - e1 * du2) / determinant
    for index in ids:
        tan[index] += t
        bitan[index] += b

engine_t = []
engine_sign = []
for n, t, b in zip(normals, tan, bitan):
    t = t - n * n.dot(t)
    if t.length == 0:
        axis = Vector((0, 0, 1)) if abs(n.z) < 0.9 else Vector((0, 1, 0))
        t = axis.cross(n).normalized()
        sign = 1
    else:
        t.normalize()
        sign = -1 if n.cross(t).dot(b) < 0 else 1
    engine_t.append(t)
    engine_sign.append(sign)

# This is the same proper rotation used by the comparison scene (determinant +1).
to_blender = lambda v: (v.x, -v.z, v.y)
mesh = bpy.data.meshes.new('tangent diagnostic')
mesh.from_pydata([to_blender(p) for p in positions], [],
                 [indices[i:i + 3] for i in range(0, len(indices), 3)])
mesh.update()
layer = mesh.uv_layers.new(name='UVMap')
for loop in mesh.loops:
    layer.data[loop.index].uv = uv[loop.vertex_index]
for polygon in mesh.polygons:
    polygon.use_smooth = True
mesh.normals_split_custom_set_from_vertices([to_blender(n) for n in normals])
mesh.calc_tangents(uvmap='UVMap')

angles = []
sign_mismatches = 0
for loop in mesh.loops:
    index = loop.vertex_index
    ref = Vector(to_blender(engine_t[index]))
    cosine = max(-1.0, min(1.0, ref.dot(loop.tangent)))
    angles.append(math.degrees(math.acos(cosine)))
    sign_mismatches += int(engine_sign[index] != loop.bitangent_sign)
angles.sort()
result = {
    'mesh': str(source.relative_to(repo)),
    'description': 'Blender mesh.calc_tangents MikkTSpace vs independent MeshUtils fallback reproduction, per corner',
    'loop_count': len(angles),
    'degenerate_uv_faces': degenerate_uv_faces,
    'mean_angle_degrees': sum(angles) / len(angles),
    'median_angle_degrees': angles[len(angles) // 2],
    'p95_angle_degrees': angles[int((len(angles) - 1) * 0.95)],
    'max_angle_degrees': angles[-1],
    'above_1_degree': sum(angle > 1 for angle in angles),
    'above_5_degrees': sum(angle > 5 for angle in angles),
    'handedness_mismatches': sign_mismatches,
    'caveats': 'CPU reproduction of source, not a GPU readback; tangent interpolation and texture compression are not measured.',
}
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n')
print(json.dumps(result, indent=2, ensure_ascii=False))
