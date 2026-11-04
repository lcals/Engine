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
    comparisons = json.loads((source.parent / 'comparisons.json').read_text())
    parts = ['''<!doctype html><html lang="zh-CN"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PBR 实测像素对照</title><style>
:root{color-scheme:light dark}body{font:16px/1.6 system-ui,sans-serif;max-width:1200px;margin:24px auto;padding:0 16px}
h1{font-size:28px}h2{margin-top:48px}h3{font-size:18px}p{max-width:85ch}.images{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}
figure{margin:0}figcaption{font-weight:600;margin:8px 0}img{display:block;width:100%;height:auto}
table{border-collapse:collapse;width:100%;font-size:14px}th,td{padding:8px;text-align:left;border-bottom:1px solid GrayText}code{overflow-wrap:anywhere}
details{margin:16px 0}summary{cursor:pointer}nav{display:flex;flex-wrap:wrap;gap:12px}
@media(max-width:650px){.images{grid-template-columns:1fr}body{padding:0 10px}th,td{padding:5px;font-size:12px}}
</style><h1>PBR 实测像素对照</h1>
<p>2026-11-04 · macOS Metal Release · 引擎与 Blender 原生图像均为 1280×720。
保留原材质、光照、相机和 Blender 默认凹凸校正；Blender 线性 EXR 使用与引擎一致的曝光 1、Reinhard、gamma 2.2 显示编码。</p>
<p>本页嵌入原始截图及绝对差图，没有缩放重采样、自动对齐或色彩拟合。
浏览器显示时可自适应大小，图片文件仍为原分辨率。每球测量中心半径 72 像素区域；MAE、RMSE 单位均为 8 位 RGB 的 0–255。
这是显示编码对照，不是线性 HDR 缓冲读回，也不是“全部物理材质完全一致”的证明。</p>
<p>SpecGloss 与 Blender Principled 只匹配灰度正入射 F0，其漫反射和掠射模型不完全等价。
旧镜面 RGB 贴图组是线性数据夹具；标准 sRGB RGB／线性 alpha 已由独立 GPU 回归覆盖，不用此组代替。
AO 没有 Principled 同义输入，另由引擎直接／间接光回归验证。</p>''']
    parts.append('<nav>' + ''.join(f'<a href="#{html.escape(name)}">{html.escape(name)}</a>'
                                  for name in comparisons) + '</nav>')
    for name, data in comparisons.items():
        parts.append(f'<section id="{html.escape(name)}"><h2>{html.escape(name)}</h2>')
        parts.append('<div class="images">' + picture(data['engine'], '引擎：当前 PBR') +
                     picture(data['blender'], 'Blender：统一显示编码，默认凹凸校正') + '</div>')
        parts.append('<details><summary>查看绝对像素差图（不放大差值）</summary>' +
                     picture(source.parent / 'comparison' / name / 'absolute-difference.png',
                             '逐像素绝对差') + '</details>')
        parts.append('<table><thead><tr><th>参数</th><th>MAE</th><th>RMSE</th>'
                     '<th>引擎亮度</th><th>Blender 亮度</th></tr></thead><tbody>')
        for case in data['cases']:
            parts.append(f'<tr><td>{html.escape(case["label"])}</td>'
                         f'<td>{case["mae_rgb_255"]:.3f}</td><td>{case["rmse_rgb_255"]:.3f}</td>'
                         f'<td>{case["engine"]["mean_luma_255"]:.3f}</td>'
                         f'<td>{case["blender"]["mean_luma_255"]:.3f}</td></tr>')
        parts.append('</tbody></table></section>')
    parts.append('<details><summary>本次采集与实现指纹</summary><pre><code>' +
                 html.escape(json.dumps({'run_id': manifest['run_id'],
                                         'shader_sha256': manifest['shader_sha256'],
                                         'editor_exit': manifest['exit']}, indent=2)) +
                 '</code></pre></details></html>')
    destination = source.parent / 'review.html'
    destination.write_text(''.join(parts), encoding='utf-8')
    print(destination)


if __name__ == '__main__':
    main()
