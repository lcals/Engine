#!/usr/bin/env python3
"""Validate direction and energy after production IBL baking of odd-sized HDR cubemaps."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import struct
import subprocess
import time

import ibl_directional_hdr_gpu_test as directional
import ibl_gpu_test as base
import scene_lighting_gpu_test as fixture

SIZE = 5
BACKGROUND = 0.25
PEAK = 8.0
SUBDIVISIONS = 24
WORK = fixture.ROOT / 'build' / 'ibl-npot-cube'


def check_odd_mip_reference():
    """The 5-to-2 center-column intersection with a destination half-face has an analytic solid-angle integral."""
    def area_element(x, y):
        return math.atan2(x * y, math.sqrt(x*x + y*y + 1.0))

    def area(x0, y0, x1, y1):
        return (area_element(x1, y1) - area_element(x0, y1) -
                area_element(x1, y0) + area_element(x0, y0))

    exact = area(-0.2, -1.0, 0.0, 0.0) / area(-1.0, -1.0, 0.0, 0.0)
    legacy = []
    for dest in (0, 1):
        weighted = total = 0.0
        for y in range(2):
            for x in range(dest * SIZE // 2, (dest + 1) * SIZE // 2):
                w = area(2*x/SIZE-1, 2*y/SIZE-1, 2*(x+1)/SIZE-1, 2*(y+1)/SIZE-1)
                weighted += w * (x == SIZE // 2)
                total += w
        legacy.append(weighted / total)
    if not (abs(legacy[0] - exact) > 0.2 and abs(legacy[1] - exact) > 0.1):
        raise RuntimeError('odd-dimension analytical fixture no longer detects legacy partition error')
    return {'exact_left_and_right': exact, 'legacy_left_and_right': legacy}


def make_cube(path):
    faces = []
    for face in range(6):
        pixels = bytearray()
        for y in range(SIZE):
            for x in range(SIZE):
                value = PEAK if face == 4 and x == SIZE // 2 else BACKGROUND
                pixels.extend(struct.pack('<eeee', value, value, value, 1.0))
        faces.append(bytes(pixels))
    header = b'\xabKTX 11\xbb\r\n\x1a\n' + struct.pack(
        '<13I', 0x04030201, 0x140B, 2, 0x1908, 0x881A, 0x1908,
        SIZE, SIZE, 0, 0, 6, 1, 0)
    path.write_bytes(header + struct.pack('<I', len(faces[0])) + b''.join(faces))


def reference(normal, roughness):
    """Independently integrate discrete input through cube subdivision without calling bake or mip-generation code."""
    n = SIZE * SUBDIVISIONS
    alpha2 = roughness ** 4
    weighted = total = 0.0
    for face in range(6):
        for y in range(n):
            v = 1.0 - 2.0 * (y + 0.5) / n
            for x in range(n):
                u = 2.0 * (x + 0.5) / n - 1.0
                direction = directional.direction(face, x, y, n)
                ndotl = sum(normal[i] * direction[i] for i in range(3))
                if ndotl <= 0.0:
                    continue
                h2 = (1.0 + ndotl) * 0.5
                ggx = alpha2 / (1.0 + (alpha2 - 1.0) * h2) ** 2
                weight = ggx * ndotl / (1.0 + u*u + v*v) ** 1.5
                value = PEAK if face == 4 and x // SUBDIVISIONS == SIZE // 2 else BACKGROUND
                weighted += value * weight
                total += weight
    return weighted / total


def inspect(outputs, project):
    path = next(project / 'assets' / item['path'] for item in outputs if 'prefilter' in item['path'])
    header, levels = base.read_ktx(path)
    rows = []
    for mip in (4, 5, 6):
        size = max(header[6] >> mip, 1)
        for x in sorted({(size - 1) // 2, size // 2}):
            y = size // 2
            normal = directional.direction(4, x, y, size)
            expected = reference(normal, mip / (len(levels) - 1))
            actual = levels[mip][4 * size * size + y * size + x][0]
            rows.append({'mip': mip, 'pixel': [x, y], 'normal': normal,
                         'actual': actual, 'reference': expected,
                         'absolute_error': abs(actual - expected)})
    return rows


def run(binary_dir):
    base.WORK = WORK / str(time.time_ns())
    project = base.prepare()
    make_cube(project / 'assets/textures/white.ktx')
    fixture.RUNTIME.mkdir(parents=True, exist_ok=True)
    folder = project / 'cache/verification' / str(time.time_ns())
    folder.mkdir(parents=True)
    report = {'complete': False, 'source': '5x5 linear RGBA16F cube, bright center column on +Z',
              'background': BACKGROUND, 'peak': PEAK, 'samples': 1024,
              'reference': 'independent GGX/solid-angle integration over 24x24 subpixels per input texel',
              'limitations': 'finite input resolution, filtered importance sampling, cubemap filtering and half precision',
              'odd_mip_analytical_fixture': check_odd_mip_reference()}
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
                'environment': 'textures/white.ktx', 'effects': ['ibl'], 'resolution': 64,
                'samples': 1024, 'lutSize': 128})
            state = fixture.wait_bake(session, accepted['job'], 180)
            if state['state'] != 'complete':
                raise RuntimeError('bake failed: ' + json.dumps(state))
            report['prefilter'] = inspect(state['outputs'], project)
            # As in the high-contrast IBL test, 2% of source contrast is an
            # absolute bound, not a relative claim about the filtered value.
            # Symmetry has a tighter half-percent bound because the input and
            # integration are exactly mirror-symmetric; sampling is finite.
            integral_tolerance = 0.02 * (PEAK - BACKGROUND)
            symmetry_tolerance = 0.005 * (PEAK - BACKGROUND)
            rows = report['prefilter']
            symmetry = [abs(rows[0]['actual'] - rows[1]['actual']),
                        abs(rows[2]['actual'] - rows[3]['actual'])]
            report['acceptance'] = {'integral_absolute_tolerance': integral_tolerance,
                                    'symmetry_absolute_tolerance': symmetry_tolerance,
                                    'symmetry_errors': symmetry}
            if (any(row['absolute_error'] > integral_tolerance for row in rows) or
                    any(error > symmetry_tolerance for error in symmetry)):
                raise RuntimeError('NPOT prefilter integral or mirror symmetry exceeded tolerance')
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
    run(parser.parse_args().binary_dir.resolve())
