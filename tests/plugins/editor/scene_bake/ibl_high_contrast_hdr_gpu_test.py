#!/usr/bin/env python3
"""用高对比 HDR 小亮斑检查 GPU 烘焙和 GGX 预过滤。"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import struct
import subprocess
import time
import tomllib

import ibl_gpu_test as base
import ibl_directional_hdr_gpu_test as directional
import scene_lighting_gpu_test as fixture

WORK = fixture.ROOT / 'build' / 'ibl-high-contrast-hdr'
BASE = (0.2, 0.2, 0.2)
PEAK = (32.0, 16.0, 8.0)
CAP_RADIUS = math.radians(10.0)


def make_panorama(path, full_mips):
    width, height = 1024, 512
    payload = bytearray()
    for y in range(height):
        theta = math.pi * (y + 0.5) / height
        for x in range(width):
            phi = 2 * math.pi * ((x + 0.5) / width - 0.5)
            z = math.sin(theta) * math.sin(phi)
            # Angular cap centered on +Z; peak values are linear scene radiance.
            lit = z >= math.cos(CAP_RADIUS)
            payload.extend(struct.pack('<eeee', *(PEAK[c] if lit else BASE[c]
                                                   for c in range(3)), 1.0))
    levels = [bytes(payload)]
    mip_width, mip_height = width, height
    while full_mips and (mip_width > 1 or mip_height > 1):
        next_width, next_height = max(1, mip_width // 2), max(1, mip_height // 2)
        previous = levels[-1]
        current = bytearray()
        for y in range(next_height):
            for x in range(next_width):
                values = [struct.unpack_from('<eeee', previous,
                          (min(2*y+dy, mip_height-1)*mip_width + min(2*x+dx, mip_width-1))*8)
                          for dy in range(2) for dx in range(2)]
                current.extend(struct.pack('<eeee', *(sum(v[c] for v in values) / 4 for c in range(4))))
        levels.append(bytes(current))
        mip_width, mip_height = next_width, next_height
    header = b'\xabKTX 11\xbb\r\n\x1a\n' + struct.pack(
        '<13I', 0x04030201, 0x140B, 2, 0x1908, 0x881A, 0x1908,
        width, height, 0, 0, 1, len(levels), 0)
    path.write_bytes(header + b''.join(struct.pack('<I', len(level)) + level for level in levels))


def make_cube(path, full_mips):
    size = 128
    faces = []
    for face in range(6):
        values = bytearray()
        for y in range(size):
            for x in range(size):
                n = directional.direction(face, x, y, size)
                color = PEAK if n[2] >= math.cos(CAP_RADIUS) else BASE
                values.extend(struct.pack('<eeee', *color, 1.0))
        faces.append(bytes(values))
    levels = [faces]
    previous_size = size
    while full_mips and previous_size > 1:
        next_size = previous_size // 2
        next_faces = []
        for face in levels[-1]:
            output = bytearray()
            for y in range(next_size):
                for x in range(next_size):
                    pixels = [struct.unpack_from('<eeee', face, ((2*y+dy)*previous_size + 2*x+dx)*8)
                              for dy in range(2) for dx in range(2)]
                    output.extend(struct.pack('<eeee', *(sum(p[c] for p in pixels) / 4 for c in range(4))))
            next_faces.append(bytes(output))
        levels.append(next_faces)
        previous_size = next_size
    header = b'\xabKTX 11\xbb\r\n\x1a\n' + struct.pack(
        '<13I', 0x04030201, 0x140B, 2, 0x1908, 0x881A, 0x1908,
        size, size, 0, 0, 6, len(levels), 0)
    path.write_bytes(header + b''.join(struct.pack('<I', len(level[0])) + b''.join(level)
                                        for level in levels))


def cap_ggx_weight(normal, roughness, steps=32768):
    """独立积分：V=N 的 GGX 预过滤，以 L 的极角积分和球冠方位弧长求值。"""
    delta = math.acos(max(-1.0, min(1.0, normal[2])))
    if roughness == 0:
        return float(delta <= CAP_RADIUS)
    alpha_squared = roughness ** 4
    cos_radius = math.cos(CAP_RADIUS)
    cos_delta, sin_delta = math.cos(delta), math.sin(delta)
    numerator = denominator = 0.0
    for i in range(steps):
        theta = (i + 0.5) * (math.pi / 2) / steps
        cos_t, sin_t = math.cos(theta), math.sin(theta)
        cos_h_squared = (1 + cos_t) / 2
        d = alpha_squared / (1 + (alpha_squared - 1) * cos_h_squared) ** 2
        weight = d * cos_t * sin_t
        denominator += weight
        radial = sin_t * sin_delta
        if radial < 1e-15:
            fraction = float(cos_t * cos_delta >= cos_radius)
        else:
            bound = (cos_radius - cos_t * cos_delta) / radial
            fraction = 1.0 if bound <= -1 else 0.0 if bound >= 1 else math.acos(bound) / math.pi
        numerator += weight * fraction
    return numerator / denominator


def inspect(outputs, project):
    prefilter = next(project / 'assets' / o['path'] for o in outputs if 'prefilter' in o['path'])
    header, levels = base.read_ktx(prefilter)
    rows = []
    for mip, level in enumerate(levels):
        size = max(header[6] >> mip, 1)
        roughness = mip / (len(levels) - 1)
        # Directional samples at all six face centers. Sizes >1 use the same
        # pixel center direction as the compute output, avoiding false alignment.
        for face in range(6):
            x = y = size // 2
            n = directional.direction(face, x, y, size)
            fraction = cap_ggx_weight(n, roughness)
            expected = [BASE[c] + (PEAK[c] - BASE[c]) * fraction for c in range(3)]
            actual = list(level[face * size * size + y * size + x][:3])
            rows.append({'mip': mip, 'roughness': roughness, 'face': face,
                         'pixel': [x, y], 'normal': n, 'actual': actual,
                         'reference': expected, 'absolute_error': [abs(actual[c]-expected[c]) for c in range(3)]})
    return {'size': list(header[6:8]), 'mips': len(levels), 'source': str(prefilter),
            'face_order': ['+X', '-X', '+Y', '-Y', '+Z', '-Z'], 'samples': rows}


def run(binary_dir, samples, full_mips, source_cube):
    base.WORK = WORK / str(time.time_ns())
    project = base.prepare()
    source_path = project / 'assets/textures/white.ktx'
    if source_cube:
        make_cube(source_path, full_mips)
    else:
        make_panorama(source_path, full_mips)
    fixture.RUNTIME.mkdir(parents=True, exist_ok=True)
    folder = project / 'cache/verification' / str(time.time_ns())
    folder.mkdir(parents=True)
    report = {'complete': False,
              'source': {'size': [128, 128] if source_cube else [1024, 512],
                         'projection': 'cubemap' if source_cube else 'panorama',
                         'format': 'linear RGBA16F', 'background': BASE,
                         'cap_rgb': PEAK, 'radius_degrees': 10, 'cap_direction': [0, 0, 1],
                         'mips': 'linear box-filtered full chain' if full_mips else 'single level'},
              'bake_samples': samples,
              'reference': 'independent numerical integration of GGX-weighted cap solid angle, 32768 steps',
              'limitations': 'ideal analytic cap versus pixelated panorama, finite cubemap and mip sampling; screen capture is SDR'}
    log = (folder / 'editor.log').open('wb')
    editor = subprocess.Popen([str(fixture.executable(binary_dir, 'editor_app')), '--project', str(project),
                               '--runtime-dir', str(fixture.RUNTIME), '--width', '960', '--height', '640'],
                              stdout=log, stderr=subprocess.STDOUT)
    try:
        with fixture.EditorMcpSession(fixture.executable(binary_dir, 'mcp_app'), project, fixture.RUNTIME,
                                      folder / 'receipts.jsonl', timeout=60) as session:
            deadline = time.monotonic() + 60
            while True:
                if editor.poll() is not None:
                    raise RuntimeError(f'editor exited: {editor.returncode}')
                try:
                    session.call('get_scene_preview')
                    break
                except RuntimeError:
                    if time.monotonic() > deadline:
                        raise
                    time.sleep(0.25)
            session.call('open_scene_preview', {'path': 'scenes/white.scene'})
            report['before'] = session.capture(f'verification/{folder.name}/before.png')
            accepted = session.call('bake_scene', {'scene': 'scenes/white.scene',
                'environment': 'textures/white.ktx', 'effects': ['ibl'], 'resolution': 64,
                'samples': samples, 'lutSize': 128})
            state = fixture.wait_bake(session, accepted['job'], 120)
            if state['state'] != 'complete':
                raise RuntimeError('bake failed: ' + json.dumps(state))
            report['job'] = state
            report['prefilter'] = inspect(state['outputs'], project)
            if samples == 256 and not full_mips:
                # This deliberately limited test guards against a lost bright
                # cap. Tolerance is 2% of the *input HDR contrast* in each
                # channel, not 2% of a small filtered output. It accounts for
                # source rasterization and finite filtered-importance samples.
                selected = [row for row in report['prefilter']['samples']
                            if row['face'] == 4 and row['mip'] in (4, 5, 6)]
                tolerances = [0.02 * (PEAK[c] - BASE[c]) for c in range(3)]
                report['acceptance'] = {'scope': '+Z mip 4/5/6, 256 samples, single-level source',
                                        'absolute_tolerances': tolerances, 'samples': selected}
                if len(selected) != 3 or any(
                    row['absolute_error'][c] > tolerances[c] for row in selected for c in range(3)
                ):
                    raise RuntimeError('high-contrast source mip regression exceeded 2% of input contrast')
            scene = tomllib.loads((project / 'assets/scenes/white.scene').read_text())
            env = next(c for n in scene['nodes'] for c in n['components'] if c['type'] == 'environment')
            if not env['ibl_enabled'] or not all(env.get(k) for k in ('ibl_irradiance','ibl_prefilter','ibl_lut')):
                raise RuntimeError('published scene does not reference enabled IBL')
            report['after'] = session.capture(f'verification/{folder.name}/after.png')
            report['center_before'] = base.png_center(Path(report['before']['path']))
            report['center_after'] = base.png_center(Path(report['after']['path']))
            report['close_editor'] = session.call('close_editor', {'delayMs': 500})
            report['editor_exit_code'] = editor.wait(timeout=15)
            if report['editor_exit_code'] != 0:
                raise RuntimeError('editor exited abnormally')
            # 'complete' means this measurement finished, not that the renderer
            # passes an arbitrary error threshold for high-contrast input.
            report['complete'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        if editor.poll() is None:
            editor.terminate()
            try:
                editor.wait(timeout=5)
            except subprocess.TimeoutExpired:
                editor.kill()
                editor.wait()
        log.close()
        (folder / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False))
        print(json.dumps({'report': str(folder / 'report.json'), 'complete': report['complete'],
                          'error': report.get('error')}, ensure_ascii=False), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary-dir', type=Path, default=fixture.ROOT / 'build/local-ninja-release')
    parser.add_argument('--samples', type=int, default=256)
    parser.add_argument('--source-mips', action='store_true', help='generate full linear box-filtered panorama mips')
    parser.add_argument('--source-cube', action='store_true', help='generate a cubemap input instead of a panorama')
    args = parser.parse_args()
    if not 16 <= args.samples <= 1024:
        parser.error('--samples must be between 16 and 1024')
    run(args.binary_dir.resolve(), args.samples, args.source_mips, args.source_cube)
