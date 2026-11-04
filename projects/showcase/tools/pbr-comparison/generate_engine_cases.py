#!/usr/bin/env python3
"""Generate reusable standard PBR comparison cases; run from any directory.

PNG inputs are deterministic, but the editor's regular texture importer encodes
UASTC.  Captured GPU values can thus differ from the uncompressed source PNGs.
"""
from pathlib import Path
import struct
import uuid
import zlib

ASSETS = Path(__file__).resolve().parents[2] / 'assets'
PREFIX = Path('projects/showcase/assets')
NAMESPACE = uuid.UUID('cd19d031-2d69-4373-a81e-2ce974909382')
SHADER = '00000000-0000-4000-8000-000000000001'
MESH = 'a0e3606e-b1b6-440b-b79e-fa7ee383ab0e'


def path(name):
    return ASSETS / name


def guid(name):
    return str(uuid.uuid5(NAMESPACE, str(PREFIX / name)))


def write(name, data):
    target = path(name)
    target.parent.mkdir(parents=True, exist_ok=True)
    raw = data.encode() if isinstance(data, str) else data
    if not target.exists() or target.read_bytes() != raw:
        target.write_bytes(raw)


def meta(name, importer, options=''):
    write(name + '.meta', f'guid = "{guid(name)}"\nimporter = "{importer}"\nmeta_version = 1\n' + options)


def png(name, rows, srgb):
    height, width = len(rows), len(rows[0])
    channels = len(rows[0][0])
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    pixels = b''.join(b'\0' + bytes(c for pixel in row for c in pixel) for row in rows)
    data = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6 if channels == 4 else 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b''))
    name = f'textures/pbr-test-{name}.png'
    write(name, data)
    meta(name, 'texture', f'\n[texture]\nformat = "rgba8"\ncompression = "uastc"\nsrgb = {str(srgb).lower()}\nmipmaps = false\n')


def solid(name, color, srgb=False):
    png(name, [[color] * 4 for _ in range(4)], srgb)


def param(name, kind, val):
    return f'\n[[parameters]]\nname = "{name}"\ntype = "{kind}"\nvalue = {val}\n'


def texture(name, tex):
    texpath = f'textures/pbr-test-{tex}.png'
    return f'\n[[textures]]\nname = "{name}"\ntexture = "{guid(texpath)}"\n'


def base(rgb, metal=0, rough=0.5, alpha=1):
    return param('baseColor', 'v4', str([*rgb, alpha])) + param('metallic', 'float', str(metal)) + param('roughness', 'float', str(rough))


def specular(smoothness=.5):
    return base([.5] * 3) + param('specularWorkflow', 'float', '1') + param('smoothness', 'float', str(smoothness)) + param('specularColor', 'v3', '[0.04, 0.04, 0.04]')


def main():
    for name, rgb, srgb in [('color', (128,64,32), True), ('normal', (191,128,238), False), ('orm-low', (255,64,0), False), ('orm-high', (255,191,0), False), ('orm-metal', (255,255,255), False)]:
        solid(name, rgb, srgb)
    for v in [0,64,128,191,255]:
        solid(f'ao-{v}', (v,255,0))
        solid(f'spec-rgb-{v}', (v,v,v,255))
    for v in [64,191]:
        solid(f'spec-alpha-{v}', (255,255,255,v))
    for v in [64,127,128,129,191,255]:
        solid(f'base-alpha-{v}', (255,255,255,v), True)
    pattern = [[(220,40,30) if x < 32 and y < 32 else (30,190,60) if y < 32 else (30,70,220) if x < 32 else (220,190,30) for x in range(64)] for y in range(64)]
    png('uv-pattern', pattern, True)
    groups = {}
    groups['basecolor'] = [base([v]*3) for v in [.18,.5,.8]] + [base([.8,.05,.05]), base([.05,.8,.05]), base([1]*3) + texture('baseColorTexture','color')]
    groups['metallic'] = [base([.5]*3,m) for m in [0,.25,.5,.75,1]] + [base([.5]*3,1) + texture('ormTexture','orm-metal')]
    groups['roughness'] = [base([.5]*3,0,r) for r in [0,.045,.2,.5,.8,1]]
    groups['normal-orm'] = [base([.5]*3)] + [base([.5]*3) + param('normalScale','float',str(v)) + texture('normalTexture','normal') for v in [0,.5,1]] + [base([.5]*3,0,1) + texture('ormTexture',f'orm-{v}') for v in ['low','high']]
    groups['ao'] = [base([.5]*3) + texture('ormTexture',f'ao-{v}') for v in [0,64,128,191,255,255]]
    groups['emission'] = [base([0]*3) + param('emissionColor','v3',str([v]*3)) for v in [0,.25,.5,1]] + [base([0]*3) + param('emissionColor','v3','[1,0,0]'), base([0]*3) + param('emissionColor','v3','[1,1,1]') + texture('emissionTexture','color')]
    groups['uv'] = [base([1]*3) + param('uvTransform','v4',str(v)) + texture('baseColorTexture','uv-pattern') for v in [[1,1,0,0],[2,2,0,0],[1,1,.25,0],[1,1,0,.25],[.5,.5,0,0],[1,1,.5,.5]]]
    groups['alpha'] = [base([.5]*3,alpha=a) + param('alphaCutoff','float',str(c)) for a,c in [(.49,.5),(.5,.5),(.51,.5),(0,0),(.5,0),(1,0)]]
    groups['specular'] = []
    for ior in [1,1.2,1.5,1.8,2,2.5]:
        f0 = ((ior-1)/(ior+1))**2
        groups['specular'].append(base([.5]*3) + param('specularWorkflow','float','1') + param('smoothness','float','0.5') + param('specularColor','v3',str([f0]*3)))
    groups['specular-map'] = [specular() + texture('specularTexture',f'spec-rgb-{v}') for v in [0,64,128,191,255]] + [specular() + texture('specularTexture','spec-rgb-255') + param('specularHighlights','float','0')]
    groups['smoothness-source'] = [specular(v) for v in [0,.5,1]] + [specular(1) + texture('specularTexture',f'spec-alpha-{v}') for v in [64,191]] + [specular(.2) + param('smoothnessSource','float','1') + texture('baseColorTexture','base-alpha-128')]
    groups['alpha-map'] = [base([.5]*3) + param('alphaCutoff','float','0.5') + texture('baseColorTexture',f'base-alpha-{v}') for v in [64,127,128,129,191,255]]
    template = path('scenes/pbr-comparison.scene').read_text().split('[[prefab_instances]]')[0]
    for group, materials in groups.items():
        prefab = f'prefabs/scenes/pbr-test-{group}.prefab'
        text = 'root = "1"\n\n[[nodes]]\nid = "1"\nname = "Material comparison"\nparent = "0"\n\n[[nodes.components]]\ntype = "transform"\n'
        for i, contents in enumerate(materials):
            material = f'materials/pbr-test-{group}-{i+1}.material'
            write(material, f'shader = "{SHADER}"\n' + contents)
            meta(material, 'material')
            x, y = [-2.2,0,2.2][i%3], [1.1,-1.1][i//3]
            text += f'\n[[nodes]]\nid = "{i+2}"\nname = "{group} {i+1}"\nparent = "1"\n\n[[nodes.components]]\ntype = "transform"\nposition = [{x}, {y}, 0.0]\n\n[[nodes.components]]\ntype = "mesh_renderer"\nmesh = "{MESH}"\nmaterial = "{guid(material)}"\n'
        write(prefab, text)
        meta(prefab, 'prefab')
        scene = f'scenes/pbr-test-{group}.scene'
        content = template + f'[[prefab_instances]]\nprefab = "{guid(prefab)}"\nroot = "4"\n'
        for i in range(1,8):
            content += f'\n[[prefab_instances.node_mappings]]\nsource = "{i}"\ntarget = "{i+3}"\n'
        write(scene, content)
        meta(scene, 'scene')
        print(scene)


if __name__ == '__main__':
    main()
