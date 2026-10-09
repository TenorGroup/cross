#!/usr/bin/env python3
"""Build guarded reader +2/+3 variants from a verified existing cpfont release.

Base and old Strong are copied byte-for-byte. New variants replace only glyph
records and bitmap sections; style metrics, coverage, kerning and ligatures
come from the existing Strong pack. Requires freetype-py, fonttools and numpy.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import importlib.util
import json
import struct
import sys
import zipfile
from functools import lru_cache
from pathlib import Path

import freetype
import numpy as np

TOC = struct.Struct('<B3xIIBhhHHBBBIh2x')
GLYPH = struct.Struct('<BBHhhH2xI')
RECIPE = 'reader-outline-guard-v1'
STRENGTHS = {2: 22, 3: 28, 4: 32}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_pack(data):
    magic, version, flags, count, _ = struct.unpack_from('<8sHHB19s', data)
    if magic != b'CPFONT\x00\x00' or version != 4 or flags != 1 or not 1 <= count <= 4:
        raise ValueError('Expected a 2-bit cpfont v4 pack with 1..4 styles')
    styles = []
    for i in range(count):
        toc = TOC.unpack_from(data, 32 + i * TOC.size)
        sid, intervals, glyphs, _, _, _, kl, kr, klc, krc, lig, start, _ = toc
        glyph_start = start + intervals * 12
        bitmap_start = glyph_start + glyphs * GLYPH.size + kl * 3 + kr * 3 + klc * krc + lig * 8
        end = TOC.unpack_from(data, 32 + (i + 1) * TOC.size)[11] if i + 1 < count else len(data)
        if not 32 + count * TOC.size <= start <= glyph_start <= bitmap_start <= end <= len(data):
            raise ValueError('Invalid cpfont section bounds')
        records = []
        for interval in range(intervals):
            lo, hi, first = struct.unpack_from('<III', data, start + interval * 12)
            for cp in range(lo, hi + 1):
                index = first + cp - lo
                if index >= glyphs:
                    raise ValueError('Invalid glyph interval')
                g = GLYPH.unpack_from(data, glyph_start + index * GLYPH.size)
                if g[6] + g[5] > end - bitmap_start or g[5] != (g[0] * g[1] + 3) // 4:
                    raise ValueError('Invalid glyph bitmap bounds')
                records.append((cp, g, data[bitmap_start + g[6]:bitmap_start + g[6] + g[5]]))
        if len(records) != glyphs:
            raise ValueError('Glyph count differs from intervals')
        styles.append({'toc': toc, 'intervals': data[start:glyph_start],
                       'tables': data[glyph_start + glyphs * GLYPH.size:bitmap_start], 'records': records})
    return data[:32], styles


def encode_pack(header, styles, replacements):
    sections = []
    tocs = []
    offset = 32 + len(styles) * TOC.size
    for style in styles:
        toc = list(style['toc'])
        glyph_data, bitmaps = bytearray(), bytearray()
        for cp, old, _ in style['records']:
            g, bitmap = replacements[toc[0], cp]
            if g[2] != old[2]:
                raise ValueError('Reader ink must preserve every glyph advance')
            glyph_data.extend(GLYPH.pack(*g[:5], len(bitmap), len(bitmaps)))
            bitmaps.extend(bitmap)
        section = style['intervals'] + glyph_data + style['tables'] + bitmaps
        toc[11] = offset
        tocs.append(TOC.pack(*toc))
        sections.append(section)
        offset += len(section)
    return header + b''.join(tocs) + b''.join(sections)


def unpack(glyph):
    g, bitmap = glyph
    raw = np.frombuffer(bitmap, np.uint8)
    values = ((raw[:, None] >> np.array([6, 4, 2, 0])) & 3).reshape(-1)
    return values[:g[0] * g[1]].reshape(g[1], g[0])


def pack_pixels(a):
    values = np.pad(a.reshape(-1), (0, (-a.size) % 4))
    return ((values[::4] << 6) | (values[1::4] << 4) | (values[2::4] << 2) | values[3::4]).tobytes()


@lru_cache(maxsize=32768)
def topology(width, height, packed_mask):
    mask = np.unpackbits(np.frombuffer(packed_mask, np.uint8))[:width * height].reshape(height, width).astype(bool)

    def count_regions(values, diagonal):
        ys, xs = np.where(values)
        todo = set(zip(ys.tolist(), xs.tolist()))
        count = 0
        steps = ((-1, 0), (1, 0), (0, -1), (0, 1))
        if diagonal:
            steps += ((-1, -1), (-1, 1), (1, -1), (1, 1))
        while todo:
            pending = [todo.pop()]
            count += 1
            while pending:
                y, x = pending.pop()
                for dy, dx in steps:
                    point = y + dy, x + dx
                    if point in todo:
                        todo.remove(point)
                        pending.append(point)
        return count

    return count_regions(mask, True), count_regions(~np.pad(mask, 1), False) - 1


def glyph_metrics(glyph):
    a = unpack(glyph)
    return {'topology': topology(a.shape[1], a.shape[0], np.packbits(a > 0).tobytes()),
            'ink': int(np.count_nonzero(a)), 'coverage': int(a.sum())}


def accept_candidate(prior, candidate):
    return (candidate['topology'] == prior['topology'] and candidate['ink'] >= prior['ink']
            and candidate['coverage'] >= prior['coverage'])


class RasterSource:
    def __init__(self, source, base, point_size, converter):
        self.faces = []
        for key, hash_key in [('path', 'sha256'), ('fallback_path', 'fallback_sha256')]:
            if key not in source:
                continue
            path = base / source[key]
            if sha(path.read_bytes()) != source[hash_key]:
                raise ValueError(f'Source hash mismatch: {path}')
            face = freetype.Face(str(path))
            face.set_char_size(point_size << 6, point_size << 6, 150, 150)
            ligatures = converter.extract_ligature_glyph_indices_fonttools(str(path)) if key == 'path' else {}
            numerals = converter.pnum_glyph_indices(str(path)) if source.get('pnum', False) else {}
            self.faces.append((face, ligatures, numerals))
        self.fp4 = converter.fp4_from_ft16_16
        self.force_autohint = source.get('force_autohint', False)

    def glyph(self, cp, strength):
        for face, ligatures, numerals in self.faces:
            index = numerals.get(cp, face.get_char_index(cp)) or ligatures.get(cp, 0)
            if not index:
                continue
            flags = freetype.FT_LOAD_NO_BITMAP
            if self.force_autohint:
                flags |= freetype.FT_LOAD_FORCE_AUTOHINT
            face.load_glyph(index, flags)
            outline = ctypes.byref(face.glyph.outline._FT_Outline)
            if freetype.FT_Outline_EmboldenXY(outline, strength, strength):
                raise ValueError(f'FreeType outline failure for U+{cp:04X}')
            freetype.FT_Outline_Translate(outline, -(strength // 2), -(strength // 2))
            face.glyph.render(freetype.FT_RENDER_MODE_NORMAL)
            bitmap = face.glyph.bitmap
            a = np.frombuffer(bytes(bitmap.buffer), np.uint8).reshape(bitmap.rows, abs(bitmap.pitch))[:, :bitmap.width]
            if bitmap.pitch < 0:
                a = a[::-1]
            a = a // 64
            packed = pack_pixels(a)
            g = (bitmap.width, bitmap.rows, self.fp4(face.glyph.linearHoriAdvance),
                 face.glyph.bitmap_left, face.glyph.bitmap_top, len(packed), 0)
            return g, packed
        return (0, 0, 0, 0, 0, 0, 0), b''


def build_file(old_data, family_sources, sources_base, point_size, converter):
    header, styles = read_pack(old_data)
    replacements = {3: {}, 4: {}}
    reports = []
    for style in styles:
        sid = style['toc'][0]
        source = RasterSource(family_sources[str(sid)], sources_base, point_size, converter)
        records = []
        totals = {level: {'ink': 0, 'coverage': 0} for level in [2, 3, 4]}
        for cp, old_g, old_bitmap in style['records']:
            old = old_g, old_bitmap
            recreated = source.glyph(cp, 22)
            if old_g[:6] != recreated[0][:6] or old_bitmap != recreated[1]:
                raise ValueError(f'Old Strong source mismatch at size {point_size}, style {sid}, U+{cp:04X}')
            prior, metrics = old, glyph_metrics(old)
            totals[2]['ink'] += metrics['ink']
            totals[2]['coverage'] += metrics['coverage']
            accepted_strength = 22
            glyph_report = {'cp': cp}
            for level in [3, 4]:
                candidate = source.glyph(cp, STRENGTHS[level])
                if candidate[0][2] != old_g[2]:
                    raise ValueError(f'Advance changed at U+{cp:04X}')
                measured = glyph_metrics(candidate)
                accepted = accept_candidate(metrics, measured)
                if accepted:
                    prior, metrics, accepted_strength = candidate, measured, STRENGTHS[level]
                replacements[level][sid, cp] = prior
                totals[level]['ink'] += metrics['ink']
                totals[level]['coverage'] += metrics['coverage']
                glyph_report[str(level)] = {'strength_26_6': accepted_strength, 'fallback': not accepted}
            records.append(glyph_report)
        for quantity in ['ink', 'coverage']:
            if not totals[2][quantity] < totals[3][quantity] < totals[4][quantity]:
                raise ValueError(f'Levels are not distinct for size {point_size}, style {sid}, {quantity}')
        reports.append({'style': sid, 'glyphs': records, 'totals': totals})
    return {level: encode_pack(header, styles, replacements[level]) for level in [3, 4]}, reports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-zip', type=Path, required=True)
    parser.add_argument('--sources', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--families', help='Optional comma-separated subset for focused validation')
    parser.add_argument('--sizes', help='Optional comma-separated subset for focused validation')
    args = parser.parse_args()
    source_recipe = json.loads(args.sources.read_text())
    if source_recipe['dpi'] != 150:
        raise ValueError('Published reader recipe requires 150 DPI')
    converter_path = Path(__file__).resolve().parents[1] / 'lib/EpdFont/scripts/fontconvert_sdcard.py'
    sys.path.insert(0, str(converter_path.parent))
    spec = importlib.util.spec_from_file_location('fontconvert_sdcard', converter_path)
    converter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(converter)
    archive = zipfile.ZipFile(args.baseline_zip)
    old_manifest = json.loads(archive.read('font-manifest.json'))
    families = set(args.families.split(',')) if args.families else set(source_recipe['families'])
    sizes = {int(s) for s in args.sizes.split(',')} if args.sizes else set(range(12, 27, 2))
    manifest = {'recipe': RECIPE, 'freetype': freetype.version(), 'dpi': 150,
                'baseline_sha256': sha(args.baseline_zip.read_bytes()), 'sources': source_recipe,
                'public_to_physical': [0, 2, 3, 4], 'files': [], 'glyph_guard': []}
    args.output.mkdir(parents=True, exist_ok=True)
    for entry in old_manifest:
        if entry['family'] not in families or entry['size'] not in sizes or entry['weight'] not in [0, 2]:
            continue
        data = archive.read(entry['path'])
        if sha(data) != entry['sha256'] or len(data) != entry['bytes']:
            raise ValueError(f'Published baseline integrity failure: {entry["path"]}')
        path = args.output / entry['path']
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        manifest['files'].append({**entry, 'public_level': 0 if entry['weight'] == 0 else 1})
        if entry['weight'] == 0:
            continue
        family, point_size = entry['family'], entry['size']
        changed, report = build_file(data, source_recipe['families'][family]['styles'],
                                     args.sources.parent, point_size, converter)
        for physical_level, content in changed.items():
            relative = entry['path'].replace('/weight-2/', f'/weight-{physical_level}/')
            path = args.output / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
            manifest['files'].append({'path': relative, 'family': family, 'size': point_size,
                                      'weight': physical_level, 'public_level': physical_level - 1,
                                      'bytes': len(content), 'sha256': sha(content)})
        manifest['glyph_guard'].append({'family': family, 'size': point_size, 'styles': report})
        print(f'{family} {point_size}: verified old Strong, generated guarded +2/+3', flush=True)
    for name in archive.namelist():
        if name.startswith('licenses/') and not name.endswith('/'):
            path = args.output / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(archive.read(name))
    (args.output / 'reader-weight-manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2))
    print(f'Wrote {len(manifest["files"])} font files', flush=True)


if __name__ == '__main__':
    main()
