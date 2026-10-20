#!/usr/bin/env python3
"""The fonts page's weight files against the store pack builder, for the same TTF.

Runs src/network/html/js/font-builder.js in headless Chrome (chrome_build.cjs: playwright-core on NODE_PATH,
Chrome at CHROME or the macOS app path) for every level -3..+5, builds the same family with
scripts/build_font_pack.py, and checks at each size, level and style:
  - every browser level file keeps the browser base file's layout (build_font_pack.layout_signature);
  - line metrics and every glyph advance equal the Python file's;
  - the 2-bit coverage (sum over all glyphs) is within --tolerance of the Python file's. Chrome draws the
    outline without FreeType's hinting: NotoSans/NotoSerif/BeVietnamPro/Geist/Mali/OpenDyslexic at 12-24 pt
    differ by 11% at most, the thin levels darker and the heavy ones lighter (dev/phan7/SO-DO.md);
  - coverage rises with the level.
Exits 77 (skipped) when node, playwright-core or Chrome is missing.

  test_parity.py [--sizes 14,24] [--tolerance 0.12] [--out DIR] [font.ttf ...]
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import build_font_pack as pack  # noqa: E402
import build_reader_weights as weights  # noqa: E402

SERIF = ROOT / 'lib/EpdFont/builtinFonts/source/NotoSerif'
LEVELS = range(-3, 6)


def skip(reason):
    print('SKIP: ' + reason)
    sys.exit(77)


def pack_level(level):
    """Reader level -3..+5 to the pack's level index (STRENGTHS order: 0, +1..+5, -1..-3)."""
    return level if level >= 0 else 5 - level


def coverage(style):
    return sum(int(weights.unpack((g, b)).sum()) for _, g, b in style['records'])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--sizes', default='14,24')
    parser.add_argument('--tolerance', type=float, default=0.12)
    parser.add_argument('--out', type=Path)
    parser.add_argument('fonts', nargs='*', type=Path,
                        default=[SERIF / 'NotoSerif-Regular.ttf', SERIF / 'NotoSerif-Bold.ttf'])
    args = parser.parse_args()
    sizes = [int(s) for s in args.sizes.split(',')]
    chrome = os.environ.get('CHROME', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
    if not shutil.which('node'):
        skip('node not found')
    if subprocess.run(['node', '-e', "require('playwright-core')"], capture_output=True).returncode:
        skip('playwright-core not on NODE_PATH')
    if not Path(chrome).exists():
        skip('Chrome not found (set CHROME)')

    work = Path(tempfile.mkdtemp(prefix='web-font-weights-'))
    out = args.out or work / 'browser'
    run = subprocess.run(['node', str(Path(__file__).with_name('chrome_build.cjs')),
                          str(ROOT / 'src/network/html/js/font-builder.js'), str(out),
                          json.dumps([level for level in LEVELS if level]), *map(str, args.fonts)],
                         capture_output=True, text=True)
    if run.returncode:
        print(run.stdout, run.stderr)
        return 1
    browser = json.loads(run.stdout.strip().splitlines()[-1])
    print(f"browser: {browser['files']} files, {browser['bytes']} bytes, {browser['ms']} ms")

    # The browser's style ids come from OS/2: give Python the same ones.
    faces = {}
    for path in args.fonts:
        sid = (1 if 'Bold' in path.stem else 0) + (2 if 'Italic' in path.stem else 0)
        faces[sid] = str(path)
    license_path = args.fonts[0].parent / 'OFL.txt'
    # The store's reading check wants U+FFFD and full Vietnamese from the font or a fallback; a user's TTF
    # may have neither, and the check writes no byte of the files compared here.
    pack.reading_quality = lambda data, line_factor: {}
    pack.build_pack('Ref', faces, sizes, pack.STRENGTHS, pack.builtin_intervals(), license_path,
                    work / 'Ref.cpfontpack')
    reference = zipfile.ZipFile(work / 'Ref.cpfontpack')

    failures, worst = [], 0.0
    for size in sizes:
        name = f"{browser['family']}_{size}.cpfont"
        js_base = (out / name).read_bytes()
        previous = None
        for level in LEVELS:
            n = pack_level(level)
            js = (out / (f'weight-{n + 1}' if n else '') / name).read_bytes()
            py = reference.read(pack.entry_name('Ref', size, n))
            where = f'{size}pt {level:+d}'
            if pack.layout_signature(js) != pack.layout_signature(js_base):
                failures.append(f'{where}: layout differs from the browser base file')
            _, js_styles = pack.read_font(js)
            _, py_styles = pack.read_font(py)
            if [s['toc'][0] for s in js_styles] != [s['toc'][0] for s in py_styles]:
                failures.append(f'{where}: styles differ')
                continue
            total = 0
            row = []
            for a, b in zip(js_styles, py_styles):
                sid = a['toc'][0]
                if a['toc'][3:6] != b['toc'][3:6]:
                    failures.append(f'{where} style {sid}: line metrics {a["toc"][3:6]} != {b["toc"][3:6]}')
                ra = {cp: g for cp, g, _ in a['records']}
                rb = {cp: g for cp, g, _ in b['records']}
                # U+FB00..FB06 come from the font's ligatures in Python; the page makes no ligatures.
                missing = {cp for cp in rb.keys() - ra.keys() if not 0xFB00 <= cp <= 0xFB06}
                if missing:
                    failures.append(f'{where} style {sid}: missing ' + ' '.join(f'U+{cp:04X}' for cp in sorted(missing)))
                advances = [cp for cp in rb.keys() & ra.keys() if ra[cp][2] != rb[cp][2]]
                if advances:
                    failures.append(f'{where} style {sid}: {len(advances)} advances differ, first U+{min(advances):04X}')
                ca, cb = coverage(a), coverage(b)
                ratio = ca / cb
                worst = max(worst, abs(ratio - 1))
                if abs(ratio - 1) > args.tolerance:
                    failures.append(f'{where} style {sid}: coverage {ca} vs {cb} ({ratio:.3f})')
                total += ca
                row.append(f's{sid} {ca}/{cb}={ratio:.3f}')
            step = '' if previous is None else f' step {(total / previous - 1) * 100:+.1f}%'
            if previous is not None and total <= previous:
                failures.append(f'{where}: coverage does not rise')
            previous = total
            print(f'{where}: ' + ', '.join(row) + step)
    print(f'worst coverage difference {worst * 100:.1f}% (tolerance {args.tolerance * 100:.0f}%)')
    for failure in failures:
        print('FAIL ' + failure)
    if not args.out:
        shutil.rmtree(work)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
