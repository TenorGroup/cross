#!/usr/bin/env python3
"""Write src/FontStoreNames.h: the name of every font of the store, drawn in that font's own letters.

    python3 scripts/gen_font_names.py <fonts.json> [--dung <dir of dung.py>] [--check]

The "Add new fonts" list shows each store font's name in its own face. The pictures are 1 bit, made here from the
font files the store packs are built from (dung.py of the store build, by default next to fonts.json), at the three
UI text sizes, so the firmware draws them straight from flash. Same order and names as FontStoreCatalog.h
(scripts/gen_font_store_catalog.py); the header checks that at build time. Needs freetype-py, numpy, fontTools.
--check exits 1 when the header on disk differs from what the sources make.

Letters are cut the way the store packs cut the reader's letters at the default weight (scripts/build_font_pack.py,
recipe reader-outline-v5-even, level 0: lib/EpdFont/scripts/fontconvert_sdcard.py, FreeType FT_LOAD_RENDER at 150 dpi,
proportional numerals, no kerning), then thresholded at 64/255: the pack keeps 2 bits, whose first level starts there
(32 clots thin serifs, 128 breaks thin strokes). A picture is the shape of the letters, not the pixels of a book page.

Each font is then sized so its x-height (the height of the lower-case x, the measure the eye reads a line of names
by) equals the x-height of the UI face (Geist Regular, autohinted as the UI faces are) at the same text size:
the point size is scaled by ui_x / font_x, font_x from the OS/2 sxHeight (the raster 'x' when the table has none),
clamped to 0.85..1.20; then the size is nudged inside that range to the one whose raster 'x' lands on the UI
x-height. A picture never gets taller than a name line of the UI (bodyLineHeight), so no row grows.
A font whose factor hits the clamp ends short of the UI x-height; the test lists those.
"""
import importlib.util
import json
import math
import sys
import tempfile
from pathlib import Path

import freetype
import numpy as np
from fontTools.ttLib import TTFont

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'lib' / 'EpdFont' / 'scripts'))
from font_features import pnum_glyph_indices  # noqa: E402

OUT = Path(__file__).resolve().parents[1] / 'src' / 'FontStoreNames.h'
POINTS = (12, 14, 16)  # body point size of the three UI text sizes (uiTextSizeSpec in src/components/UIScale.h)
THRESHOLD = 64
GEIST = Path(__file__).resolve().parents[1] / 'lib' / 'EpdFont' / 'builtinFonts' / 'source' / 'Geist' / 'Geist-Regular.ttf'
CLAMP = (0.85, 1.20)
LINE_HEIGHTS = (33, 38, 43)  # bodyLineHeight of the three UI text sizes (uiTextSizeSpec): the height of a name line


def display_name(name):
    """The CamelCase split of displayName() in FontDownloadActivity.cpp."""
    out = ''
    for i, c in enumerate(name):
        if i > 0:
            p = name[i - 1]
            n = name[i + 1] if i + 1 < len(name) else ''
            if (c.isupper() and (p.islower() or p.isdigit() or (p.isupper() and n.islower()))) or (c.isdigit() and not p.isdigit()):
                out += ' '
        out += c
    return out


def x_rows(face, size64, flags=freetype.FT_LOAD_RENDER):
    """Rows of ink (>= THRESHOLD) of the lower-case x at `size64` (26.6 points, 150 dpi)."""
    face.set_char_size(size64, size64, 150, 150)
    face.load_glyph(face.get_char_index(ord('x')), flags)
    bm = face.glyph.bitmap
    if not bm.rows:
        return 0
    cov = np.array(bm.buffer, dtype=np.uint8).reshape(bm.rows, bm.pitch)[:, :bm.width]
    return int((cov.max(axis=1) >= THRESHOLD).sum())


def ui_x_height(pt):
    """x-height of the UI face at `pt`, as the firmware draws it (FreeType autohint, fontconvert.py)."""
    return x_rows(freetype.Face(str(GEIST)), pt << 6, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_FORCE_AUTOHINT)


def font_x_height(path, face, pt):
    """x-height in px at `pt`: OS/2 sxHeight, else the raster x."""
    font = TTFont(str(path))
    sx = getattr(font['OS/2'], 'sxHeight', 0)
    return sx * pt * 150 / 72 / font['head'].unitsPerEm if sx else x_rows(face, pt << 6)


def fit(path, text, pt, ui_x, cap):
    """The size (26.6 points) that puts this font's x-height on `ui_x`: the point size scaled by ui_x / font_x within
    CLAMP, then the one in that range whose raster x is nearest ui_x (ties: nearest the first estimate), never so large
    that the picture is taller than `cap` (a name line)."""
    face = freetype.Face(str(path))
    want = min(max(ui_x / font_x_height(path, face, pt), CLAMP[0]), CLAMP[1]) * pt * 64
    sizes = range(math.ceil(CLAMP[0] * pt * 64), int(CLAMP[1] * pt * 64) + 1)
    for size64 in sorted(sizes, key=lambda s: (abs(x_rows(face, s) - ui_x), abs(s - want))):
        if draw(path, text, pt, size64)[2] <= cap:
            return size64, x_rows(face, size64)
    raise SystemExit(f'{path}: no size keeps the picture under {cap} px')


def draw(path, text, pt, size64=None):
    """(rows of bytes, width, height, rows from the top to the baseline) of `text`, cropped to its ink."""
    face = freetype.Face(str(path))
    face.set_char_size(size64 or pt << 6, size64 or pt << 6, 150, 150)
    numerals = pnum_glyph_indices(str(path))
    glyphs, pen = [], 0.0
    for ch in text:
        face.load_glyph(numerals.get(ord(ch), face.get_char_index(ord(ch))), freetype.FT_LOAD_RENDER)
        g = face.glyph
        bm = g.bitmap
        cov = np.array(bm.buffer, dtype=np.uint8).reshape(bm.rows, bm.pitch)[:, :bm.width] if bm.rows else np.zeros((0, 0), np.uint8)
        glyphs.append((int(round(pen)) + g.bitmap_left, g.bitmap_top, cov))
        pen += g.linearHoriAdvance / 65536.0
    top = max(t for _, t, _ in glyphs)
    x0 = min(x for x, _, _ in glyphs)
    page = np.zeros((top + max(a.shape[0] - t for _, t, a in glyphs), max(x + a.shape[1] for x, _, a in glyphs) - x0), np.uint8)
    for x, t, a in glyphs:
        r, c = top - t, x - x0
        page[r:r + a.shape[0], c:c + a.shape[1]] = np.maximum(page[r:r + a.shape[0], c:c + a.shape[1]], a)
    ys, xs = np.where(page >= THRESHOLD)
    ink = page[ys.min():ys.max() + 1, xs.min():xs.max() + 1] >= THRESHOLD
    return np.packbits(ink, axis=1).tobytes(), ink.shape[1], ink.shape[0], top - int(ys.min())


def render(manifest, dung_dir):
    spec = importlib.util.spec_from_file_location('dung', Path(dung_dir) / 'dung.py')
    dung = importlib.util.module_from_spec(spec)
    sys.modules['dung'] = dung
    spec.loader.exec_module(dung)
    families = sorted(manifest['families'], key=lambda f: f['name'])
    names = [f['name'] for f in families]
    tiers = [[] for _ in POINTS]
    xheights = [[] for _ in POINTS]  # raster x-height of each picture's font
    scales = [[] for _ in POINTS]  # size of each picture / the UI point size, per mille
    ui_x = [ui_x_height(pt) for pt in POINTS]
    with tempfile.TemporaryDirectory() as tmp:
        regulars = []
        for name in names:
            fam = dung.FAMILIES[name]
            if 'files' in fam:
                regulars.append(fam['files'][0])
            else:  # a variable font: the Regular instance, as the pack is built from
                regulars.append(dung.instance(fam['var'][0], 400, Path(tmp) / f'{name}.ttf', fam.get('patch')))
        for t, pt in enumerate(POINTS):
            cap = LINE_HEIGHTS[t]
            for name, regular in zip(names, regulars):
                size64, xh = fit(regular, display_name(name), pt, ui_x[t], cap)
                tiers[t].append(draw(regular, display_name(name), pt, size64))
                xheights[t].append(xh)
                scales[t].append(round(1000 * size64 / (pt * 64)))
    lines = ['#pragma once', '', '#include <cstdint>', '', '#include "FontStoreCatalog.h"', '#include "components/lists/list.h"', '',
             '// Generated by scripts/gen_font_names.py from the font store manifest of this version. Do not edit.',
             '// The name of each pack of FontStoreCatalog.h in its own letters, 1 bit (rows of ceil(width / 8) bytes, MSB',
             '// first, 1 = ink), at the three UI text sizes. Read straight from flash. NAME_XHEIGHT_UI: the x-height of the UI',
             '// face per size; NAME_XHEIGHT_n / NAME_SCALE_n (per mille): x-height of each picture and its size / UI size.',
             'namespace fontstore {', 'namespace fui = freeink::ui;',
             f'inline constexpr uint8_t NAME_POINTS[{len(POINTS)}] = {{{", ".join(map(str, POINTS))}}};']
    for t, tier in enumerate(tiers):
        data = b''.join(row[0] for row in tier)
        lines.append(f'inline constexpr uint8_t NAME_BITS_{t}[{len(data)}] = {{')
        lines += ['    ' + ','.join(f'0x{b:02x}' for b in data[i:i + 24]) + ',' for i in range(0, len(data), 24)]
        lines.append('};')
    lines.append(f'inline constexpr uint8_t NAME_XHEIGHT_UI[{len(POINTS)}] = {{{", ".join(map(str, ui_x))}}};')
    for t in range(len(POINTS)):
        lines.append(f'inline constexpr uint8_t NAME_XHEIGHT_{t}[] = {{{", ".join(map(str, xheights[t]))}}};')
        lines.append(f'inline constexpr uint16_t NAME_SCALE_{t}[] = {{{", ".join(map(str, scales[t]))}}};')
    lines.append(f'inline constexpr const char* NAME_KEYS[] = {{{", ".join(json.dumps(n) for n in names)}}};')
    lines.append(f'inline constexpr uint32_t NAME_CRC32[] = {{{", ".join(str(f["files"][0]["crc32"]) + "u" for f in families)}}};')
    lines.append('inline constexpr fui::LabelImage NAME_IMAGES[][PACK_COUNT] = {')
    for t, tier in enumerate(tiers):
        lines.append('    {')
        offset = 0
        for name, (raw, w, h, base) in zip(names, tier):
            lines.append(f'        {{{{NAME_BITS_{t} + {offset}, {w}, {h}, fui::BitmapFormat::BW1, true}}, {base}}},  // {name}')
            offset += len(raw)
        lines.append('    },')
    lines += ['};',
              'constexpr bool sameName(const char* a, const char* b) {',
              '  while (*a && *a == *b) ++a, ++b;',
              '  return *a == *b;',
              '}',
              'constexpr bool namesMatchCatalog() {',
              '  for (size_t i = 0; i < PACK_COUNT; ++i)',
              '    if (!sameName(NAME_KEYS[i], PACKS[i].name) || NAME_CRC32[i] != PACKS[i].crc32) return false;',
              '  return true;',
              '}',
              f'static_assert(PACK_COUNT == {len(names)} && namesMatchCatalog(), "FontStoreNames.h and FontStoreCatalog.h come from different manifests (a name or a pack crc32 differs)");',
              'static_assert(NAME_IMAGES[2][PACK_COUNT - 1].bitmap.data != nullptr, "a picture is missing");',
              '}  // namespace fontstore', '']
    return '\n'.join(lines)


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    dung_dir = sys.argv[sys.argv.index('--dung') + 1] if '--dung' in sys.argv else str(Path(args[0]).resolve().parent)
    if '--dung' in sys.argv:
        args.remove(dung_dir)
    text = render(json.load(open(args[0])), dung_dir)
    if '--check' in sys.argv:
        sys.exit(0 if OUT.read_text() == text else 1)
    OUT.write_text(text)
    print('wrote', OUT)
