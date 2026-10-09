#!/usr/bin/env python3
"""Build deterministic STORE packs with six layout-identical cpfont v4 levels."""
from __future__ import annotations

import argparse
import ast
import hashlib
import json
import re
import struct
import sys
import tempfile
import zipfile
from pathlib import Path

import freetype

import build_reader_weights as weights

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'lib/EpdFont/scripts'))
import fontconvert_sdcard as converter

RECIPE = 'reader-outline-v3-ramp'
STRENGTHS = (0, 16, 32, 48, 64, 96)
STYLE_NAMES = ('regular', 'bold', 'italic', 'bolditalic')
ZIP_TIME = (1980, 1, 1, 0, 0, 0)


def builtin_intervals():
    tree = ast.parse((ROOT / 'lib/EpdFont/scripts/fontconvert.py').read_text())
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(isinstance(target, ast.Name) and target.id == 'intervals'
                                                for target in node.targets):
            ranges = ast.literal_eval(node.value)
            return converter.resolve_intervals(','.join(f'(0x{low:X}-0x{high:X})' for low, high in ranges))
    raise ValueError('Missing builtin coverage')


def read_font(data):
    header, styles = weights.read_pack(data)
    identifiers = [style['toc'][0] for style in styles]
    if identifiers != sorted(set(identifiers)) or any(identifier > 3 for identifier in identifiers):
        raise ValueError('Invalid cpfont style identifiers')
    for style in styles:
        codepoints = [record[0] for record in style['records']]
        if codepoints != sorted(set(codepoints)):
            raise ValueError('Invalid cpfont coverage ordering')
    return header, styles


def layout_signature(data):
    header, styles = read_font(data)
    checksum = hashlib.sha256(header)
    for style in styles:
        metrics = list(style['toc'])
        metrics[11] = 0
        checksum.update(weights.TOC.pack(*metrics))
        checksum.update(style['intervals'])
        checksum.update(style['tables'])
        for codepoint, glyph, _ in style['records']:
            checksum.update(struct.pack('<IH', codepoint, glyph[2]))
    return checksum.hexdigest()


def assert_same_layout(base, candidate):
    if layout_signature(base) != layout_signature(candidate):
        raise ValueError('Layout invariant failed: advance, coverage, tables or TOC metrics changed')


def builtin_advances(family, size, style_id):
    directory = ROOT / 'lib/EpdFont/builtinFonts'
    text = (directory / f'{family}_{size}_{STYLE_NAMES[style_id]}.h').read_text()
    glyph_block = text.split('Glyphs[] = {', 1)[1].split('};', 1)[0]
    advances = [int(value) for value in re.findall(r'\{\s*\d+,\s*\d+,\s*(\d+),', glyph_block)]
    shared = re.search(r'Intervals\s*=\s*builtin_font_metadata::(\w+)', text).group(1)
    metadata = (directory / 'builtin_font_metadata.h').read_text()
    block = metadata.split(shared + '[] = {', 1)[1].split('};', 1)[0]
    intervals = [(int(low, 0), int(high, 0), int(index, 0)) for low, high, index in
                 re.findall(r'\{\s*(0x[\da-fA-F]+|\d+),\s*(0x[\da-fA-F]+|\d+),\s*(0x[\da-fA-F]+|\d+)\s*\}', block)]
    result = {codepoint: advances[index + codepoint - low]
              for low, high, index in intervals for codepoint in range(low, high + 1)}
    if len(result) != len(advances):
        raise ValueError('Builtin intervals do not cover every glyph')
    return result


def compare_builtin(data, family, size):
    _, styles = read_font(data)
    rows = []
    for style in styles:
        style_id = style['toc'][0]
        expected = builtin_advances(family, size, style_id)
        actual = {codepoint: glyph[2] for codepoint, glyph, _ in style['records']}
        differences = [{'codepoint': codepoint, 'builtin': expected[codepoint], 'cpfont': actual[codepoint]}
                       for codepoint in sorted(expected.keys() & actual.keys())
                       if expected[codepoint] != actual[codepoint]]
        rows.append({'size': size, 'style': style_id, 'compared': len(expected.keys() & actual.keys()),
                     'mismatches': len(differences), 'examples': differences[:10],
                     'missing': sorted(expected.keys() - actual.keys()),
                     'extra': sorted(actual.keys() - expected.keys())})
    return rows


def ink_bounds(data):
    _, styles = read_font(data)
    rows = []
    for style in styles:
        inked = [glyph for _, glyph, _ in style['records'] if glyph[0] and glyph[1]]
        rows.append({'style': style['toc'][0], 'metric_max_ink_top': style['toc'][12],
                     'actual_max_ink_top': max((glyph[4] for glyph in inked), default=0),
                     'actual_min_ink_bottom': min((glyph[4] - glyph[1] for glyph in inked), default=0)})
    return rows


def entry_name(family, size, level):
    filename = f'{family}_{size}.cpfont'
    return f'weight-{level + 1}/{filename}' if level else filename


def build_pack(family, style_fonts, sizes, strengths, intervals, license_path, output,
               pnum=False, force_autohint=False, builtin_family=None, base_dir=None,
               fallback_style_fonts=None):
    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_-]{0,63}', family):
        raise ValueError('Family must be a safe ASCII filename')
    sizes = sorted(sizes)
    if not sizes or len(set(sizes)) != len(sizes) or any(not 1 <= size <= 255 for size in sizes):
        raise ValueError('Sizes must be distinct integers in 1..255')
    if tuple(strengths) != STRENGTHS:
        raise ValueError(f'{RECIPE} requires strength 26.6 {STRENGTHS}')
    if not style_fonts or any(style not in range(4) for style in style_fonts):
        raise ValueError('Expected 1..4 styles with identifiers 0..3')
    if builtin_family and not pnum:
        raise ValueError('Builtin reader parity requires pnum')
    license_data = Path(license_path).read_bytes()
    if not license_data.strip():
        raise ValueError('Source license must not be empty')
    sources = {style: {'path': str(Path(filename).resolve()),
                       'sha256': weights.sha(Path(filename).read_bytes()),
                       'pnum': pnum, 'force_autohint': force_autohint}
               for style, filename in sorted(style_fonts.items())}
    for style, filename in (fallback_style_fonts or {}).items():
        sources[style].update(fallback_path=str(Path(filename).resolve()),
                              fallback_sha256=weights.sha(Path(filename).read_bytes()))
    metadata = {'format': 1, 'cpfont_version': 4, 'family': family, 'sizes': sizes,
                'recipe': RECIPE, 'dpi': 150, 'freetype': list(freetype.version()),
                'pnum': pnum, 'force_autohint': force_autohint,
                'levels': [{'level': level, 'directory': f'weight-{level + 1}' if level else '',
                            'strength_26_6': strength, 'translation_26_6': -(strength // 2)}
                           for level, strength in enumerate(strengths)],
                'legacy_compatible_directory': 'weight-1',
                'layout_signature_format': 'sha256-header-toc-offset0-intervals-tables-codepoint-advance12.4',
                'layout_signatures': {}, 'builtin_parity': [],
                'sources': {str(style): {'name': Path(source['path']).name, 'sha256': source['sha256'],
                                        'size': Path(source['path']).stat().st_size}
                            for style, source in sources.items()}, 'entries': []}
    payloads = {'OFL.txt': license_data}
    if fallback_style_fonts:
        fallback_license = Path(next(iter(fallback_style_fonts.values()))).parent / 'OFL.txt'
        payloads['OFL.txt'] += b'\n\nFallback font license:\n\n' + fallback_license.read_bytes()
        metadata['fallback_sources'] = {
            str(style): {'name': Path(filename).name, 'sha256': weights.sha(Path(filename).read_bytes())}
            for style, filename in sorted(fallback_style_fonts.items())}
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.font-pack-', dir=output.parent) as temporary:
        for size in sizes:
            base_path = Path(temporary) / f'{family}_{size}.cpfont'
            if base_dir:
                base_data = (Path(base_dir) / base_path.name).read_bytes()
            else:
                converter.generate_cpfont_multistyle(style_fonts, size, intervals, str(base_path),
                                                    pnum=pnum, force_autohint=force_autohint,
                                                    fallback_style_fonts=fallback_style_fonts)
                base_data = base_path.read_bytes()
            header, styles = read_font(base_data)
            signature = layout_signature(base_data)
            metadata['layout_signatures'][str(size)] = signature
            if builtin_family:
                parity = compare_builtin(base_data, builtin_family, size)
                metadata['builtin_parity'].extend(parity)
                if any(row['mismatches'] or row['missing'] or row['extra'] for row in parity):
                    raise ValueError('Builtin parity failed: ' + json.dumps(parity, sort_keys=True))
            rasters = {style: weights.RasterSource(source, Path('/'), size, converter)
                       for style, source in sources.items()}
            if base_dir:
                for style in styles:
                    for codepoint, glyph, bitmap in style['records']:
                        raster, pixels = rasters[style['toc'][0]].glyph(codepoint, 0)
                        if raster[:5] != glyph[:5] or pixels != bitmap:
                            raise ValueError(f'Source raster differs from published base: {size}/{style["toc"][0]}/U+{codepoint:04X}')
            for level, strength in enumerate(strengths):
                data = base_data
                if level:
                    replacements = {(style['toc'][0], codepoint): rasters[style['toc'][0]].glyph(codepoint, strength)
                                    for style in styles for codepoint, _, _ in style['records']}
                    data = weights.encode_pack(header, styles, replacements)
                assert_same_layout(base_data, data)
                name = entry_name(family, size, level)
                payloads[name] = data
                metadata['entries'].append({'path': name, 'point_size': size, 'level': level,
                                            'layout_signature': signature, 'ink_bounds': ink_bounds(data)})
        metadata['entries'].append({'path': 'OFL.txt'})
        for entry in metadata['entries']:
            data = payloads[entry['path']]
            entry.update({'sha256': weights.sha(data), 'size': len(data)})
        metadata['entries'].sort(key=lambda entry: entry['path'])
        payloads['pack.json'] = (json.dumps(metadata, indent=2, sort_keys=True) + '\n').encode('utf-8')
        archive_path = Path(temporary) / 'pack.zip'
        with zipfile.ZipFile(archive_path, 'w', compression=zipfile.ZIP_STORED, allowZip64=False) as archive:
            for name in sorted(payloads):
                entry = zipfile.ZipInfo(name, ZIP_TIME)
                entry.compress_type = zipfile.ZIP_STORED
                entry.create_system = 3
                entry.external_attr = 0o100644 << 16
                archive.writestr(entry, payloads[name])
        archive_path.replace(output)
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--family', required=True)
    for style in STYLE_NAMES:
        parser.add_argument('--' + style, type=Path)
        parser.add_argument('--fallback-' + style, type=Path)
    parser.add_argument('--base-dir', type=Path)
    parser.add_argument('--sizes', default='12,14,16,18')
    parser.add_argument('--strengths', default=','.join(map(str, STRENGTHS)))
    parser.add_argument('--intervals', default='builtin')
    parser.add_argument('--license', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--pnum', action='store_true')
    parser.add_argument('--force-autohint', action='store_true')
    parser.add_argument('--builtin-family', choices=('notoserif', 'notosans'))
    args = parser.parse_args()
    fonts = {style: str(getattr(args, name)) for style, name in enumerate(STYLE_NAMES) if getattr(args, name)}
    fallbacks = {style: str(getattr(args, 'fallback_' + name)) for style, name in enumerate(STYLE_NAMES)
                 if getattr(args, 'fallback_' + name)}
    intervals = builtin_intervals() if args.intervals == 'builtin' else converter.resolve_intervals(args.intervals)
    metadata = build_pack(args.family, fonts, [int(size) for size in args.sizes.split(',')],
                          [int(strength) for strength in args.strengths.split(',')], intervals,
                          args.license, args.output, args.pnum, args.force_autohint, args.builtin_family,
                          args.base_dir, fallbacks)
    print(json.dumps({'output': str(args.output), 'bytes': args.output.stat().st_size,
                      'sha256': weights.sha(args.output.read_bytes()),
                      'entries': len(metadata['entries']) + 1,
                      'builtin_compared': sum(row['compared'] for row in metadata['builtin_parity']),
                      'builtin_mismatches': sum(row['mismatches'] for row in metadata['builtin_parity'])}, sort_keys=True))


if __name__ == '__main__':
    main()
