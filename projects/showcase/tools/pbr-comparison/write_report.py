"""Write a self-contained review page from real captures and pixel measurements."""
import base64
import html
import json
from pathlib import Path
import struct
import sys
import zlib


def compact_png(path):
    # Screenshots use uncompressed PNG IDAT data. Recompress only that stream
    # for the embedded copy; preserve all pixel bytes, filters and metadata.
    # Source images stay untouched, and the decompressed stream is checked.
    original = Path(path).read_bytes()
    if original[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('expected a PNG screenshot')
    chunks = []
    offset = 8
    while offset < len(original):
        length = struct.unpack_from('>I', original, offset)[0]
        chunk = original[offset:offset + length + 12]
        chunks.append((chunk[4:8], chunk[8:-4], chunk))
        offset += length + 12
    pixels = zlib.decompress(b''.join(data for kind, data, _ in chunks if kind == b'IDAT'))
    encoded = zlib.compress(pixels, 9)
    if zlib.decompress(encoded) != pixels:
        raise ValueError('PNG recompression changed image data')
    replacement = struct.pack('>I', len(encoded)) + b'IDAT' + encoded
    replacement += struct.pack('>I', zlib.crc32(b'IDAT' + encoded) & 0xffffffff)
    output = bytearray(original[:8])
    inserted = False
    for kind, _, chunk in chunks:
        if kind == b'IDAT':
            if not inserted:
                output.extend(replacement)
                inserted = True
        else:
            output.extend(chunk)
    return bytes(output)


def picture(path, caption):
    encoded = base64.b64encode(compact_png(path)).decode('ascii')
    return (f'<figure><figcaption>{html.escape(caption)}</figcaption>'
            f'<img src="data:image/png;base64,{encoded}" alt="{html.escape(caption)}" '
            'width="1280" height="720" loading="lazy"></figure>')


def main():
    if len(sys.argv) != 2:
        raise SystemExit('expected: capture-report.json')
    source = Path(sys.argv[1]).resolve()
    manifest = json.loads(source.read_text())
    if not manifest.get('complete'):
        raise SystemExit('capture run is incomplete')
    transfer = manifest.get('display_transfer', 'gamma-2.2')
    if transfer not in ('srgb-iec-61966-2-1', 'gamma-2.2'):
        raise SystemExit(f'unsupported display transfer: {transfer}')
    transfer_label = 'exact sRGB' if transfer == 'srgb-iec-61966-2-1' else 'gamma 2.2'
    comparisons = json.loads((source.parent / 'comparisons.json').read_text())
    parts = ['''<!doctype html><html lang="zh-CN"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PBR measured pixel comparison</title><style>
:root{color-scheme:light dark}body{font:16px/1.6 system-ui,sans-serif;max-width:1200px;margin:24px auto;padding:0 16px}
h1{font-size:28px}h2{margin-top:48px}h3{font-size:18px}p{max-width:85ch}.images{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}
figure{margin:0}figcaption{font-weight:600;margin:8px 0}img{display:block;width:100%;height:auto}
table{border-collapse:collapse;width:100%;font-size:14px}th,td{padding:8px;text-align:left;border-bottom:1px solid GrayText}code{overflow-wrap:anywhere}
details{margin:16px 0}summary{cursor:pointer}nav{display:flex;flex-wrap:wrap;gap:12px}
@media(max-width:650px){.images{grid-template-columns:1fr}body{padding:0 10px}th,td{padding:5px;font-size:12px}}
</style><h1>PBR measured pixel comparison</h1>
<p>2026-11-04 · macOS Metal Release · Engine and Blender native images are both 1280×720.
Original materials, lighting, camera and Blender default bump correction are preserved. Blender linear EXR uses matching exposure 1, Reinhard and {transfer} display encoding.</p>
<p>This page embeds original captures and absolute differences without resampling, automatic alignment or color fitting.
Browser display can resize images, but files retain their original resolution. Each sphere is measured within a 72-pixel center radius; MAE and RMSE use 8-bit RGB values from 0 to 255.
This compares display-encoded values, not linear HDR readback, and does not prove equivalence for all physical materials.</p>
<p>SpecGloss and Blender Principled match only grayscale normal-incidence F0; their diffuse and grazing-angle models are not equivalent.
The earlier specular RGB texture set is a linear-data fixture. Standard sRGB RGB/linear alpha is covered separately by GPU regression tests.
Principled has no equivalent AO input; engine direct/indirect lighting tests cover it separately.</p>'''.replace('{transfer}', transfer_label)]
    parts.append('<nav>' + ''.join(f'<a href="#{html.escape(name)}">{html.escape(name)}</a>'
                                  for name in comparisons) + '</nav>')
    for name, data in comparisons.items():
        parts.append(f'<section id="{html.escape(name)}"><h2>{html.escape(name)}</h2>')
        parts.append('<div class="images">' + picture(data['engine'], 'Engine: current PBR') +
                     picture(data['blender'], 'Blender: matching display encoding and default bump correction') + '</div>')
        parts.append('<details><summary>Absolute pixel difference without amplification</summary>' +
                     picture(source.parent / 'comparison' / name / 'absolute-difference.png',
                             'Absolute per-pixel difference') + '</details>')
        parts.append('<table><thead><tr><th>Parameter</th><th>MAE</th><th>RMSE</th>'
                     '<th>Engine luminance</th><th>Blender luminance</th></tr></thead><tbody>')
        for case in data['cases']:
            parts.append(f'<tr><td>{html.escape(case["label"])}</td>'
                         f'<td>{case["mae_rgb_255"]:.3f}</td><td>{case["rmse_rgb_255"]:.3f}</td>'
                         f'<td>{case["engine"]["mean_luma_255"]:.3f}</td>'
                         f'<td>{case["blender"]["mean_luma_255"]:.3f}</td></tr>')
        parts.append('</tbody></table></section>')
    parts.append('<details><summary>Capture and implementation fingerprints</summary><pre><code>' +
                 html.escape(json.dumps({'run_id': manifest['run_id'],
                                         'shader_sha256': manifest['shader_sha256'],
                                         'editor_exit': manifest['exit']}, indent=2)) +
                 '</code></pre></details></html>')
    destination = source.parent / 'review.html'
    destination.write_text(''.join(parts), encoding='utf-8')
    print(destination)


if __name__ == '__main__':
    main()
