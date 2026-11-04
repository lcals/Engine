"""Compare a capture_cases manifest with unchanged, display-matched references.

Run with Blender Python, passing the capture report after --.
"""
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'projects/showcase/tests'))
import pbr_compare_pixels


def main():
    arguments = sys.argv[sys.argv.index('--') + 1:]
    if len(arguments) != 1:
        raise SystemExit('expected: capture-report.json')
    report_path = Path(arguments[0]).resolve()
    capture = json.loads(report_path.read_text())
    if not capture.get('complete'):
        raise SystemExit('capture run did not complete')
    results = {}
    for name, case in capture['cases'].items():
        if case['reference'] is None:
            continue  # AO has no equivalent Blender Principled input.
        output = report_path.parent / 'comparison' / name
        sys.argv = ['pbr_compare_pixels.py', '--', case['capture']['path'], case['reference'],
                    str(output), case['labels']]
        pbr_compare_pixels.main()
        results[name] = json.loads((output / 'comparison.json').read_text())
    (report_path.parent / 'comparisons.json').write_text(
        json.dumps(results, indent=2, ensure_ascii=False) + '\n')
    print(json.dumps({name: [round(item['mae_rgb_255'], 4) for item in result['cases']]
                      for name, result in results.items()}, ensure_ascii=False), flush=True)


if __name__ == '__main__':
    main()
