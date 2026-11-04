#!/usr/bin/env python3
"""检查全景 HDR 小亮斑重投影的球面积分（正式编辑器 GPU 烘焙）。"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import struct
import subprocess
import time

import ibl_gpu_test as base
import scene_lighting_gpu_test as fixture

WIDTH, HEIGHT = 1024, 512
BACKGROUND, PEAK = 0.25, 2048.0
SPOT_SIZE = 4
WORK = fixture.ROOT / 'build' / 'ibl-panorama-footprint'
CASES = {'equator': (517, 256), 'seam': (1022, 256), 'near-pole': (517, 6)}


def make_panorama(path, spot):
    sx, sy = spot
    payload = bytearray()
    for y in range(HEIGHT):
        for x in range(WIDTH):
            lit = ((x - sx) % WIDTH < SPOT_SIZE and sy <= y < sy + SPOT_SIZE)
            value = PEAK if lit else BACKGROUND
            payload.extend(struct.pack('<eeee', value, value, value, 1.0))
    header = b'\xabKTX 11\xbb\r\n\x1a\n' + struct.pack(
        '<13I', 0x04030201, 0x140B, 2, 0x1908, 0x881A, 0x1908,
        WIDTH, HEIGHT, 0, 0, 1, 1, 0)
    path.write_bytes(header + struct.pack('<I', len(payload)) + payload)


def rectangle_solid_angle(x, y, size):
    def area(u, v):
        return math.atan2(u * v, math.sqrt(u*u + v*v + 1))
    x0, x1 = 2*x/size-1, 2*(x+1)/size-1
    y0, y1 = 2*y/size-1, 2*(y+1)/size-1
    return area(x1, y1) - area(x0, y1) - area(x1, y0) + area(x0, y0)


def inspect(outputs, project, spot):
    path = next(project / 'assets' / item['path'] for item in outputs if 'prefilter' in item['path'])
    header, levels = base.read_ktx(path)
    size = header[6]
    if header[10] != 6:
        raise RuntimeError('prefilter output is not a cubemap')
    actual = 0.0
    for y in range(size):
        for x in range(size):
            w = rectangle_solid_angle(x, y, size)
            for face in range(6):
                actual += (levels[0][face * size * size + y * size + x][0] - BACKGROUND) * w
    # Exact solid angle of each latitude-longitude source cell, independent of
    # cubemap projection or the implementation's sampling pattern.
    y0, y1 = spot[1], spot[1] + SPOT_SIZE
    solid_angle = (SPOT_SIZE * 2 * math.pi / WIDTH *
                   (math.cos(math.pi * y0 / HEIGHT) - math.cos(math.pi * y1 / HEIGHT)))
    expected = (PEAK - BACKGROUND) * solid_angle
    return {'radiant_integral_excess_actual': actual, 'radiant_integral_excess_reference': expected,
            'absolute_error': abs(actual - expected),
            'relative_error': abs(actual - expected) / expected}


def run_case(binary_dir, name, spot):
    base.WORK = WORK / f'{time.time_ns()}-{name}'
    project = base.prepare()
    make_panorama(project / 'assets/textures/white.ktx', spot)
    fixture.RUNTIME.mkdir(parents=True, exist_ok=True)
    folder = project / 'cache/verification' / str(time.time_ns())
    folder.mkdir(parents=True)
    report = {'complete': False, 'case': name, 'input_pixels': [WIDTH, HEIGHT], 'spot_xy': spot,
              'spot_size': SPOT_SIZE, 'input_base': BACKGROUND, 'input_peak': PEAK,
              'reference': 'exact input equirectangular pixel solid-angle sum',
              'limitations': 'finite footprint samples, cube downsampling and RGBA16F rounding'}
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
            accepted = session.call('bake_scene', {'scene': 'scenes/white.scene',
                'environment': 'textures/white.ktx', 'effects': ['ibl'], 'resolution': 128,
                'samples': 256, 'lutSize': 128})
            state = fixture.wait_bake(session, accepted['job'], 180)
            if state['state'] != 'complete':
                raise RuntimeError('bake failed: ' + json.dumps(state))
            report['measurement'] = inspect(state['outputs'], project, spot)
            # This regression threshold applies only to these 4x4 emitter
            # fixtures and their integrated excess radiance; it is not an
            # error bound for arbitrary HDR panoramas or subpixel emitters.
            report['acceptance'] = {'scope': 'selected 4x4 HDR emitter integral',
                                    'max_relative_error': 0.1}
            if report['measurement']['relative_error'] > report['acceptance']['max_relative_error']:
                raise RuntimeError('selected HDR emitter integral differs by more than 10%')
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
    parser.add_argument('--case', choices=CASES, action='append')
    args = parser.parse_args()
    for case in args.case or CASES:
        run_case(args.binary_dir.resolve(), case, CASES[case])
