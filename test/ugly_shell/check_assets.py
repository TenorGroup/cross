#!/usr/bin/env python3
"""tenor/ugly: generated files are the generators' output, the strings keep the writing rules, and the
shell's strings are used by the shell alone."""
import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument('--repo', type=Path, required=True)
repo = ap.parse_args().repo
sys.path.insert(0, str(repo / 'scripts'))
from gen_i18n import parse_yaml_file  # noqa: E402

failures = []


def check(ok, message):
    if not ok:
        failures.append(message)


def run(*args):
    subprocess.run([sys.executable, *map(str, args)], check=True, capture_output=True)


with tempfile.TemporaryDirectory() as t:
    t = Path(t)
    # Strokes and pictures are pure arithmetic: the committed files are exactly what the scripts write.
    run(repo / 'scripts/ugly/gen_tables.py', t)
    run(repo / 'scripts/ugly/gen_art.py', t)
    for name in ('UglyTables.h', 'UglyArt.h'):
        check((t / name).read_bytes() == (repo / 'src/shells/ugly' / name).read_bytes(),
              name + ' is not what scripts/ugly writes: run the script and commit the result')
    # The font draws with FreeType, so the same script run twice must agree with itself.
    for n in (1, 2):
        (t / ('f%d' % n)).mkdir()
        run(repo / 'scripts/ugly/gen_font.py', t / ('f%d' % n))
    for px in (22, 30, 38, 52):
        a = (t / 'f1' / ('ugly_%d.h' % px)).read_bytes()
        b = (t / 'f2' / ('ugly_%d.h' % px)).read_bytes()
        check(a == b, 'ugly_%d.h is not the same twice' % px)
        check(len(a) > 10000, 'ugly_%d.h looks empty' % px)

# The writing rules, on the Vietnamese and English lines of the shell.
FORBIDDEN = ('—', '–', '·')
NUMBER_WORDS = ('một', 'hai', 'ba', 'bốn', 'sáu', 'bảy', 'tám', 'chín', 'mười')
for lang in ('vietnamese', 'english', 'chinese'):
    data = parse_yaml_file(repo / ('lib/I18n/translations/%s.yaml' % lang))
    for key, value in data.items():
        if not key.startswith('STR_UGLY_'):
            continue
        check(not any(c in value for c in FORBIDDEN), '%s %s uses a dash or a middle dot' % (lang, key))
        check('{' not in value, '%s %s keeps a placeholder' % (lang, key))
        if lang == 'vietnamese':
            check(not re.search(r'không phải .{0,40} mà', value, re.I), '%s: "không phải X mà là Y"' % key)
            check(not re.search(r'\b(%s)\b' % '|'.join(NUMBER_WORDS), value, re.I), '%s: a count in words' % key)

# The voice of the shell stays in the shell: no other source file names a STR_UGLY_ string.
for path in list((repo / 'src').rglob('*.cpp')) + list((repo / 'src').rglob('*.h')) + list((repo / 'lib').rglob('*.cpp')):
    rel = path.relative_to(repo)
    if str(rel).startswith('src/shells/ugly/') or str(rel).startswith('lib/I18n/'):
        continue
    if 'STR_UGLY_' in path.read_text(errors='ignore'):
        check(False, '%s names a STR_UGLY_ string outside src/shells/ugly' % rel)

if failures:
    print('\n'.join('FAIL: ' + f for f in failures))
    sys.exit(1)
print('PASS: generated files match their scripts, strings keep the rules, the voice stays in the shell')
