#!/usr/bin/env python3
"""Regenerate every built-in UI face: Geist, FreeType autohint, Chinese UI subset of Noto Sans SC.

Sizes 8, 10 and 12 are 1-bit and uncompressed. Sizes 14 and 16 are 2-bit mono coverage,
DEFLATE-grouped (2048 B), each next to an uncompressed `-reference.h` for the decoder test.
Latin and Vietnamese come from Geist, Chinese from the pinned Noto Sans SC subset. No Arabic or
Hebrew: book text gets those from SD-card fonts.

  python scripts/build_ui_fonts.py --cjk-source 'test/fonts-cjk/NotoSansSC[wght].ttf' --output /tmp/ui-fonts --install
"""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'scripts'))
from check_chinese_ui import fingerprint, required
from gen_i18n import parse_yaml_file

SOURCE_SHA = 'a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da'
GEIST_SHA = {
    'Regular': '85a1c6b18a6b0a06dfe9fd4f6d6a5d4979f74ec861eaef4bc7868b5492b8a117',
    'Bold': '3f21f02b827228c3f0c5fb230d027b5a1a263013618e8052bc1b3c5692812c88',
}
# (size, styles, compressed): the pixel size is shared by all faces of a size.
FACES = [(8, ('Regular',), False), (10, ('Regular', 'Bold'), False), (12, ('Regular', 'Bold'), False),
         (14, ('Regular', 'Bold'), True), (16, ('Regular', 'Bold'), True)]
BUILTIN = ROOT/'lib/EpdFont/builtinFonts'


def convert(out, name, size, style, cjk_cps, charset, compressed, suffix=''):
    geist = f'lib/EpdFont/builtinFonts/source/Geist/Geist-{style}.ttf'
    cjk = out/f'NotoSansSC-UI-{500 if style == "Bold" else 400}.ttf'
    command = [sys.executable, 'lib/EpdFont/scripts/fontconvert.py', name, str(size), geist, str(cjk), '--force-autohint']
    if compressed:
        command += ['--2bit', '--mono-coverage', '--compress', '--max-group-bytes', '2048']
    for cp in cjk_cps:
        command += ['--additional-intervals', f'{cp},{cp}']
    run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    assert run.returncode == 0, run.stderr
    header = (f'// Chinese UI charset SHA256: {charset}\n// Noto Sans SC source SHA256: {SOURCE_SHA}\n'
              + run.stdout.replace(str(out), '<generated>'))
    (out/(name+suffix+'.h')).write_text(header)
    return name+suffix


def main():
    from fontTools import subset
    from fontTools.ttLib import TTFont
    from fontTools.varLib.instancer import instantiateVariableFont
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--cjk-source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--install', action='store_true', help='Copy the headers into builtinFonts and re-pool the metadata.')
    args = parser.parse_args()
    assert hashlib.sha256(args.cjk_source.read_bytes()).hexdigest() == SOURCE_SHA, 'SHA256 của font nguồn không đúng'
    for style, digest in GEIST_SHA.items():
        assert hashlib.sha256((BUILTIN/f'source/Geist/Geist-{style}.ttf').read_bytes()).hexdigest() == digest
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cps = required(parse_yaml_file(ROOT/'lib/I18n/translations/chinese.yaml'))
    wanted = cps | set(map(ord, '界面字号小中大'))
    cjk_cps = sorted(cp for cp in wanted if cp >= 0x2e80)
    font = TTFont(args.cjk_source, recalcTimestamp=False)
    options = subset.Options(); options.layout_features = []
    subsetter = subset.Subsetter(options=options); subsetter.populate(unicodes=cjk_cps); subsetter.subset(font)
    variable = out/'NotoSansSC-UI-variable.ttf'; font.save(variable); font.close()
    for weight in (400, 500):
        font = TTFont(variable, recalcTimestamp=False)
        instantiateVariableFont(font, {'wght': weight}, inplace=True)
        font.save(out/f'NotoSansSC-UI-{weight}.ttf'); font.close()
    charset = fingerprint(cps)
    with ThreadPoolExecutor(4) as pool:
        jobs = []
        for size, styles, compressed in FACES:
            for style in styles:
                name = f'geist_{size}_{style.lower()}'
                jobs.append(pool.submit(convert, out, name, size, style, cjk_cps, charset, compressed))
                if compressed:  # uncompressed twin: the ground truth for the decoder test
                    jobs.append(pool.submit(convert, out, name, size, style, cjk_cps, charset, False, '-reference'))
        for job in jobs:
            print(job.result(), 'generated', flush=True)
    if args.install:
        for size, styles, _ in FACES:
            for style in styles:
                name = f'geist_{size}_{style.lower()}.h'
                shutil.copyfile(out/name, BUILTIN/name)
        subprocess.run([sys.executable, 'scripts/deduplicate_font_metadata.py', str(BUILTIN)], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
