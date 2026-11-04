"""Capture the original PBR cases through the Release editor and its MCP API.

The actual framebuffer must be 1280x720; no image resize or material adjustment.
Each invocation keeps independent screenshots, receipts, logs and a manifest.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'scripts'))
from editor_mcp_session import EditorMcpSession

CASES = ('basecolor', 'metallic', 'roughness', 'normal-orm', 'emission', 'uv',
         'alpha', 'alpha-map', 'specular', 'specular-map', 'smoothness-source', 'ao')


def main(arguments=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cases', nargs='*', choices=CASES)
    args = parser.parse_args(arguments)
    run_id = str(time.time_ns())
    work = ROOT / 'build/material-comparison/current' / run_id
    work.mkdir(parents=True)
    runtime = work / 'runtime'
    runtime.mkdir()
    project = ROOT / 'projects/showcase'
    release = ROOT / 'build/local-ninja-release'
    report = {'complete': False, 'run_id': run_id, 'cases': {}, 'shader_sha256': {}}
    for path in sorted((ROOT / 'src/engine/runtime/render/shaders').glob('pbr_*.sh')):
        report['shader_sha256'][path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
    log = (work / 'editor.log').open('wb')
    editor = subprocess.Popen([str(release / 'editor_app'), '--project', str(project),
                               '--runtime-dir', str(runtime), '--width', '640', '--height', '360'],
                              stdout=log, stderr=subprocess.STDOUT)
    try:
        with EditorMcpSession(release / 'mcp_app', project, runtime, work / 'receipts.jsonl',
                              timeout=120) as session:
            deadline = time.monotonic() + 120
            while True:
                if editor.poll() is not None:
                    raise RuntimeError(f'editor exited {editor.returncode}')
                try:
                    session.call('get_scene_preview')
                    break
                except RuntimeError:
                    if time.monotonic() > deadline:
                        raise
                    time.sleep(.25)
            for case in args.cases or CASES:
                scene = f'scenes/pbr-test-{case}.scene'
                opened = session.call('open_scene_preview', {'path': scene})
                capture = session.capture(f'material-comparison/current-{run_id}/{case}.png')
                if [capture['width'], capture['height']] != [1280, 720]:
                    raise RuntimeError(f'expected native 1280x720, got {capture}')
                reference = {'normal-orm': 'normal-orm-gltf-same-mesh',
                             'specular': 'specular-ior'}.get(case, case)
                report['cases'][case] = {
                    'scene': scene, 'opened': opened, 'capture': capture,
                    'reference': None if case == 'ao' else str(
                        ROOT / f'build/material-comparison/display-matched/blender-{reference}.png'),
                    'labels': str(ROOT / 'build/material-comparison/expanded' /
                                  f'{"specular-ior" if case == "specular" else case}-labels.json'),
                }
                print(f'CAPTURED {case}: 1280x720', flush=True)
            report['close'] = session.call('close_editor', {'delayMs': 500})
            report['exit'] = editor.wait(timeout=15)
            if report['exit'] != 0:
                raise RuntimeError(f'editor exit {report["exit"]}')
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
        (work / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
        print(json.dumps({'report': str(work / 'report.json'), 'complete': report['complete']},
                         ensure_ascii=False), flush=True)


if __name__ == '__main__':
    main()
