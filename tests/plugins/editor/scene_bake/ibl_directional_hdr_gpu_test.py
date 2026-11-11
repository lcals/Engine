#!/usr/bin/env python3
"""Bake directional linear HDR on the GPU and compare the full result against analytic integration."""
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
import scene_lighting_gpu_test as fixture

WORK = fixture.ROOT / 'build' / 'ibl-directional-hdr'
# Distinct channels encode world directions, with values above display white.
# L(w) = (2 + 1.2 wx, 1.5 + .8 wy, 1 + .7 wz).
BIAS = (2.0, 1.5, 1.0)
SLOPE = (1.2, 0.8, 0.7)


def make_panorama(path):
    width, height = 512, 256
    payload = bytearray()
    for y in range(height):
        theta = math.pi * (y + 0.5) / height
        for x in range(width):
            phi = 2 * math.pi * ((x + 0.5) / width - 0.5)
            direction = (math.sin(theta) * math.cos(phi), math.cos(theta),
                         math.sin(theta) * math.sin(phi))
            payload.extend(struct.pack('<eeee', *(BIAS[c] + SLOPE[c] * direction[c]
                                                   for c in range(3)), 1.0))
    header = b'\xabKTX 11\xbb\r\n\x1a\n' + struct.pack(
        '<13I', 0x04030201, 0x140B, 2, 0x1908, 0x881A, 0x1908,
        width, height, 0, 0, 1, 1, 0)
    path.write_bytes(header + struct.pack('<I', len(payload)) + payload)


def direction(face, x, y, size):
    u = 2 * (x + 0.5) / size - 1
    v = 1 - 2 * (y + 0.5) / size
    d = ((1, v, -u), (-1, v, u), (u, 1, -v), (u, -1, v),
         (u, v, 1), (-u, v, -1))[face]
    length = math.sqrt(sum(q*q for q in d))
    return tuple(q / length for q in d)


def ggx_first_moment(roughness, count=131072):
    # Integrate GGX half-vector density independently in 1D. For V=N,
    # the azimuth integrates out and the weighted mean of L is k*N.
    if roughness == 0:
        return 1.0
    a2 = roughness ** 4
    numerator = denominator = 0.0
    for i in range(count):
        h2 = (1 - (i + 0.5) / count) / (1 + (a2 - 1) * (i + 0.5) / count)
        ndotl = max(2 * h2 - 1, 0.0)
        numerator += ndotl * ndotl
        denominator += ndotl
    return numerator / denominator


def inspect(outputs, project):
    summary = {}
    for output in outputs:
        path = project / 'assets' / output['path']
        header, levels = base.read_ktx(path)
        if 'lut' in path.name:
            summary[path.name] = {'size': list(header[6:8]), 'role': 'existing BRDF LUT; checked in white-furnace test'}
            continue
        is_diffuse = 'irradiance' in path.name
        if not is_diffuse and 'prefilter' not in path.name:
            raise RuntimeError(f'unexpected output {path}')
        samples = []
        # Three interior positions per face, avoid cube seam/filter edge behavior.
        for mip, level in enumerate(levels):
            size = max(header[6] >> mip, 1)
            k = 2 / 3 if is_diffuse else ggx_first_moment(mip / max(len(levels) - 1, 1))
            points = sorted(set((size // 4, size // 2, min(3 * size // 4, size - 1))))
            for face in range(6):
                for y in points:
                    for x in points:
                        n = direction(face, x, y, size)
                        expected = [BIAS[c] + SLOPE[c] * k * n[c] for c in range(3)]
                        actual = level[face * size * size + y * size + x][:3]
                        error = max(abs(actual[c] - expected[c]) for c in range(3))
                        samples.append({'mip': mip, 'face': face, 'xy': [x,y],
                                        'actual': actual, 'expected': expected,
                                        'max_error': error})
        worst = max(samples, key=lambda s: s['max_error'])
        summary[path.name] = {'size': list(header[6:8]), 'faces': header[10],
                              'mips': len(levels), 'samples': len(samples),
                              'max_error': worst['max_error'], 'worst': worst}
        # Finite source resolution, mip filtering, seam sampling, half floats and
        # 256 stochastic samples prevent an exact analytic match.
        if worst['max_error'] > 0.15:
            raise RuntimeError(f'nonuniform HDR integral mismatch: {path.name}: {worst}')
    if len(summary) != 3:
        raise RuntimeError('expected three published IBL textures')
    return summary


def run(binary_dir):
    run_dir = WORK / str(time.time_ns())
    base.WORK = run_dir
    project = base.prepare()
    make_panorama(project / 'assets/textures/white.ktx')
    fixture.RUNTIME.mkdir(parents=True, exist_ok=True)
    folder = project / 'cache/verification' / str(time.time_ns())
    folder.mkdir(parents=True)
    report = {'complete': False, 'source': 'analytic RGB directional HDR, 512x256 RGBA16F',
              'formula': 'L(w)=(2+1.2*wx,1.5+0.8*wy,1+0.7*wz)',
              'limitations': 'Smooth low-frequency HDR, not a high contrast photographic environment; screenshots are SDR, not float readback.'}
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
                'samples': 256, 'lutSize': 128})
            state = fixture.wait_bake(session, accepted['job'], 120)
            if state['state'] != 'complete':
                raise RuntimeError('bake failed: ' + json.dumps(state))
            report['job'] = state
            report['textures'] = inspect(state['outputs'], project)
            scene = tomllib.loads((project / 'assets/scenes/white.scene').read_text())
            env = next(c for n in scene['nodes'] for c in n['components'] if c['type'] == 'environment')
            if not env['ibl_enabled'] or not all(env.get(k) for k in ('ibl_irradiance','ibl_prefilter','ibl_lut')):
                raise RuntimeError('published scene does not reference enabled IBL')
            report['after'] = session.capture(f'verification/{folder.name}/after.png')
            report['center_before'] = base.png_center(Path(report['before']['path']))
            report['center_after'] = base.png_center(Path(report['after']['path']))
            if max(report['center_before']) > 10 or min(report['center_after']) < 60:
                raise RuntimeError('scene receiver did not visibly respond to baked IBL')
            report['close_editor'] = session.call('close_editor', {'delayMs': 500})
            report['editor_exit_code'] = editor.wait(timeout=15)
            if report['editor_exit_code'] != 0:
                raise RuntimeError('editor exited abnormally')
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
    args = parser.parse_args()
    run(args.binary_dir.resolve())
