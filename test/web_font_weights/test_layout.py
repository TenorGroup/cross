#!/usr/bin/env python3
"""The fonts page's weight files, run in node with a stand-in canvas (no browser).

Checks what the device relies on: the level table and folders match the firmware and the store packs,
every level file keeps the base file's layout (build_font_pack.layout_signature), more weight means more
ink, and the advances are FreeType's (fontconvert_sdcard.py). test_parity.py compares real Chrome pixels.
"""
import json
import re
import subprocess
import sys
from pathlib import Path

import freetype

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'lib/EpdFont/scripts'))
import build_font_pack as pack  # noqa: E402
import build_reader_weights as weights  # noqa: E402
import fontconvert_sdcard as converter  # noqa: E402

BUILDER = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'src/network/html/js/font-builder.js'
FONT = ROOT / 'lib/EpdFont/builtinFonts/source/NotoSans/NotoSans-Regular.ttf'
SIZE = 14
LEVELS = [0, -3, -1, 1, 3, 5]

# A canvas whose every glyph is a box 0.5 em wide and 0.7 em tall on the pen: a stroke grows or (erasing)
# shrinks it by half the line width, and pixels get the box's area coverage.
RUNNER = r'''
const fs = require('fs');
require(process.argv[1]);
const B = globalThis.TenorFontBuilder;
const [fontPath, size, levels] = [process.argv[2], +process.argv[3], JSON.parse(process.argv[4])];
const canvas = { width: 1000, height: 1000 };
let em = 0, box = null;
const ctx = {
  canvas, lineWidth: 1, globalCompositeOperation: 'source-over',
  set font(f) { em = parseFloat(f); }, get font() { return em + 'px'; },
  measureText: () => ({ width: em / 2, actualBoundingBoxLeft: 0, actualBoundingBoxRight: em / 2,
                        actualBoundingBoxAscent: em * 0.7, actualBoundingBoxDescent: 0 }),
  clearRect: () => { box = null; },
  fillText: (ch, x, y) => { box = [x, y - em * 0.7, x + em / 2, y]; },
  strokeText: () => {
    const e = (ctx.globalCompositeOperation === 'destination-out' ? -1 : 1) * ctx.lineWidth / 2;
    box = [box[0] - e, box[1] - e, box[2] + e, box[3] + e];
  },
  getImageData: (x0, y0, w, h) => {
    const data = new Uint8ClampedArray(w * h * 4);
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      const cx = Math.max(0, Math.min(x0 + x + 1, box[2]) - Math.max(x0 + x, box[0]));
      const cy = Math.max(0, Math.min(y0 + y + 1, box[3]) - Math.max(y0 + y, box[1]));
      data[(y * w + x) * 4 + 3] = Math.round(255 * cx * cy);
    }
    return { data };
  },
};
const b = fs.readFileSync(fontPath);
const font = B.parse(b.buffer.slice(b.byteOffset, b.byteOffset + b.length));
const styles = B.rasterStyle(font, 'f', size, ctx, levels);
console.log(JSON.stringify({
  sizes: B.SIZES, strengths: B.STRENGTHS, defaults: B.DEFAULT_LEVELS,
  dirs: [-3, -2, -1, 0, 1, 2, 3, 4, 5].map(B.weightDir),
  files: styles.map(s => Buffer.from(B.writeCpfont({ 0: s })).toString('base64')),
}));
'''


def main():
    run = subprocess.run(['node', '-e', RUNNER, str(BUILDER), str(FONT), str(SIZE), json.dumps(LEVELS)],
                         capture_output=True, text=True)
    if run.returncode:
        print('RED: the builder did not run: ' + next((l for l in run.stderr.splitlines() if 'Error' in l), run.stderr[-200:]))
        return 1
    result = json.loads(run.stdout)
    bad = []

    header = (ROOT / 'src/ReaderInkWeight.h').read_text()
    table = re.search(r'OUTLINE_STRENGTHS\s*\[\s*LEVEL_COUNT\s*\]\s*=\s*\{([^}]+)\}', header).group(1)
    if result['strengths'] != [int(v) for v in table.split(',')]:
        bad.append(f"strengths {result['strengths']} are not readerInk::OUTLINE_STRENGTHS")
    # The store pack's folder for each level, as the registry reads it.
    want = []
    for level in range(-3, 6):
        n = level if level >= 0 else 5 - level
        name = pack.entry_name('F', SIZE, n)
        want.append(int(name.split('/')[0][7:]) if '/' in name else 0)
    if result['dirs'] != want:
        bad.append(f"weight folders {result['dirs']} want {want}")
    if result['sizes'] != list(range(12, 25, 2)):
        bad.append(f"sizes {result['sizes']}")
    # Stored level = level + 3 (src/ReaderInkWeight.h): the default is stored 3..8 (base, +1..+5), never the thin 0..2,
    # or the reader silently falls back to a lighter level.
    if sorted(d + 3 for d in result['defaults']) != list(range(3, 9)):
        bad.append(f"default levels {result['defaults']} are not stored 3..8")
    # The checkboxes on the page tick the same set.
    page = (ROOT / 'src/network/html/FontsPage.html').read_text()
    tick = re.search(r'box\.checked = ([^;]+);', page)
    if not tick or [v for v in range(-3, 6) if eval(tick.group(1).replace('&&', ' and '), {}, {'level': v})] != result['defaults']:
        bad.append(f"FontsPage.html checkbox default {tick and tick.group(1)!r} differs from DEFAULT_LEVELS")

    files = [bytes(__import__('base64').b64decode(f)) for f in result['files']]
    base = files[0]
    ink = {}
    for level, data in zip(LEVELS, files):
        if pack.layout_signature(data) != pack.layout_signature(base):
            bad.append(f'level {level:+d}: layout differs from the base file')
        ink[level] = pack.ink_coverage(data)
    ordered = [ink[level] for level in sorted(ink)]
    if ordered != sorted(set(ordered)):
        bad.append(f'ink does not rise with the level: {dict(sorted(ink.items()))}')

    face = freetype.Face(str(FONT))
    face.set_char_size(SIZE << 6, SIZE << 6, 150, 150)
    _, styles = pack.read_font(base)
    wrong = []
    for cp, glyph, _ in styles[0]['records']:
        index = face.get_char_index(cp)
        if not index:
            continue
        face.load_glyph(index, freetype.FT_LOAD_NO_BITMAP)
        if glyph[2] != converter.fp4_from_ft16_16(face.glyph.linearHoriAdvance):
            wrong.append(cp)
    if wrong:
        bad.append(f'{len(wrong)} advances are not FreeType\'s, first U+{wrong[0]:04X}')

    for line in bad:
        print('RED: ' + line)
    print(('RED ' if bad else 'GREEN ') + f'{len(LEVELS)} levels, ink {dict(sorted(ink.items()))}')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
