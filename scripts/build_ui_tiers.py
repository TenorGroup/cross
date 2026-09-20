#!/usr/bin/env python3
"""Generate bounded compressed UI faces from verified font sources."""
import argparse
import hashlib
import json
import re
import zlib
from pathlib import Path
import shlex
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'scripts'))
from check_chinese_ui import required
from gen_i18n import parse_yaml_file
from build_chinese_ui import SOURCE_SHA

PRIMARY_SHA = {
    'Geist-Regular.ttf': '85a1c6b18a6b0a06dfe9fd4f6d6a5d4979f74ec861eaef4bc7868b5492b8a117',
    'Geist-Bold.ttf': '3f21f02b827228c3f0c5fb230d027b5a1a263013618e8052bc1b3c5692812c88',
}

def recolor_compressed(text, reference):
    """Keep converter metadata and group bounds; replace binary pixels losslessly."""
    from font_header_tools import read_header
    ref = read_header(reference)
    glyphs = [value for _, value in sorted(ref['glyphs'].items())]
    group_match = re.search(r'(\w+Groups\[.*?\]\s*=\s*\{)(.*?)(\n\};)', text, re.S)
    groups = [[int(x) for x in re.findall(r'\d+', row)] for row in re.findall(r'\{([^{}]+)\}', group_match.group(2))]
    bitmap = bytearray(); rewritten = []
    for _, _, original_size, count, first in groups:
        aligned = bytearray()
        for metrics, bits in glyphs[first:first+count]:
            width, height = metrics[:2]
            stride = (width+3)//4
            rows = bytearray(stride*height)
            for y in range(height):
                for x in range(width):
                    pixel = y*width+x
                    ink = 3 if bits[pixel//8] & (128 >> (pixel%8)) else 0
                    rows[y*stride+x//4] |= ink << (6-2*(x%4))
            aligned.extend(rows)
        assert len(aligned) == original_size and original_size <= 2048
        compressor = zlib.compressobj(9, zlib.DEFLATED, -15)
        compressed = compressor.compress(aligned)+compressor.flush()
        rewritten.append((len(bitmap),len(compressed),len(aligned),count,first));bitmap.extend(compressed)
    body = '\n'+'\n'.join('    { '+', '.join(map(str,row))+' },' for row in rewritten)
    text = text[:group_match.start(2)]+body+text[group_match.end(2):]
    bm = re.search(r'(\w+Bitmaps)\[.*?\](\s*=\s*\{)(.*?)(\n\};)', text, re.S)
    body = '\n'+'\n'.join('    '+' '.join(f'0x{x:02X},' for x in bitmap[i:i+16]) for i in range(0,len(bitmap),16))
    return text[:bm.start()]+bm.group(1)+f'[{len(bitmap)}]'+bm.group(2)+body+bm.group(4)+text[bm.end():]


def main():
    from fontTools.ttLib import TTFont
    from fontTools import subset
    from fontTools.varLib.instancer import instantiateVariableFont
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cjk-source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--ink', action='store_true', help='Apply guarded native-weight tuning to each new face.')
    args = parser.parse_args()
    assert hashlib.sha256(args.cjk_source.read_bytes()).hexdigest() == SOURCE_SHA
    args.output.mkdir(parents=True, exist_ok=True)
    source = ROOT/'lib/EpdFont/builtinFonts/source'
    for name, digest in PRIMARY_SHA.items():
        assert hashlib.sha256((source/'Geist'/name).read_bytes()).hexdigest() == digest
    cps = required(parse_yaml_file(ROOT/'lib/I18n/translations/chinese.yaml')) | set(map(ord, '界面字号小中大'))
    cjk_cps = sorted(cp for cp in cps if cp >= 0x2e80)
    font = TTFont(args.cjk_source, recalcTimestamp=False)
    options = subset.Options(); options.layout_features = []
    subsetter = subset.Subsetter(options=options); subsetter.populate(unicodes=cjk_cps); subsetter.subset(font)
    variable = args.output/'NotoSansSC-UI-variable.ttf'; font.save(variable); font.close()
    for weight in (400, 500):
        font = TTFont(variable, recalcTimestamp=False)
        instantiateVariableFont(font, {'wght': weight}, inplace=True)
        font.save(args.output/f'NotoSansSC-UI-{weight}.ttf'); font.close()
    recipe = json.loads((ROOT/'test/chinese_ui/font-baseline.json').read_text())
    manifest = {'source_sha256': SOURCE_SHA, 'primary_sha256': PRIMARY_SHA, 'max_group_bytes': 2048, 'fonts': {}}
    for size in (14, 16):
        for style in ('regular', 'bold'):
            name = f'geist_{size}_{style}'
            command = shlex.split(recipe[f'geist_12_{style}']['command'])
            command[0] = str(ROOT/'lib/EpdFont/scripts/fontconvert.py')
            command[1:3] = [name, str(size)]
            for index, arg in enumerate(command):
                if arg.endswith('.ttf'):
                    command[index] = str((ROOT/'lib/EpdFont/scripts'/arg).resolve())
            insert = next(i for i, arg in enumerate(command) if arg.startswith('--'))
            command.insert(insert, str(args.output/f'NotoSansSC-UI-{500 if style == "bold" else 400}.ttf'))
            for cp in cjk_cps:
                command.extend(['--additional-intervals', f'{cp},{cp}'])
            records = {}
            for compressed in (False, True):
                suffix = '' if compressed else '-reference'
                flags = ['--2bit', '--mono-coverage', '--compress', '--max-group-bytes', '2048'] if compressed else []
                result = subprocess.run([sys.executable]+command+flags, capture_output=True, text=True)
                assert result.returncode == 0, result.stderr
                header = args.output/(name+suffix+'.h')
                # Input paths are recorded in the manifest; generated headers stay portable.
                text = result.stdout
                command_line = next(line for line in text.splitlines() if 'fontconvert.py' in line)
                text = text.replace(command_line, ' * Generated by scripts/build_ui_tiers.py; see ui-tier-fonts.json.')
                header.write_text(text)
                if args.ink:
                    if compressed:
                        header.write_text(recolor_compressed(text, args.output/(name+'-reference.h')))
                    else:
                        from build_ui_ink import tune, write_tuned_header
                        from font_header_tools import read_header
                        glyphs, ink_stats, _ = tune(name, read_header(header), source, args.cjk_source)
                        temporary = args.output/(name+'-untuned.h')
                        header.rename(temporary)
                        write_tuned_header(temporary, header, glyphs)
                        manifest.setdefault('ink', {})[name] = ink_stats
                (args.output/(name+suffix+'.log')).write_text(result.stderr)
                records['compressed' if compressed else 'reference'] = hashlib.sha256(header.read_bytes()).hexdigest()
            manifest['fonts'][name] = records
            print(name, 'generated', flush=True)
    (args.output/'ui-tier-fonts.json').write_text(json.dumps(manifest, indent=2)+'\n')

if __name__ == '__main__':
    main()
