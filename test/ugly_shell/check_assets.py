#!/usr/bin/env python3
"""tenor/ugly: generated files are the generators' output, the strings keep the writing rules, and the
shell's strings are used by the shell alone."""
import argparse
import os
import re
import shutil
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


def pick_python():
    """The first interpreter that has numpy and PIL, the two the generators draw with."""
    env = {k: v for k, v in os.environ.items() if k != 'PYTHONPATH'}
    for candidate in (sys.executable, '/usr/bin/python3', shutil.which('python3') or ''):
        if candidate and subprocess.run([candidate, '-c', 'import numpy, PIL'], env=env, capture_output=True).returncode == 0:
            return candidate, env
    failures.append('no interpreter with numpy and PIL: the generators cannot run')
    return None, env


PYTHON, ENV = pick_python()


def run(*args):
    if PYTHON:
        subprocess.run([PYTHON, *map(str, args)], check=True, capture_output=True, env=ENV)


with tempfile.TemporaryDirectory() as t:
    t = Path(t)
    # Strokes and pictures are pure arithmetic: the committed files are exactly what the scripts write.
    run(repo / 'scripts/ugly/gen_tables.py', t)
    run(repo / 'scripts/ugly/gen_art.py', t)
    run(repo / 'scripts/ugly/gen_sleep_set.py', t)
    for name in ('UglyTables.h', 'UglyArt.h', 'UglySleepData.h') if PYTHON else ():
        check((t / name).read_bytes() == (repo / 'src/shells/ugly' / name).read_bytes(),
              name + ' is not what scripts/ugly writes: run the script and commit the result')
    # The font draws with FreeType, so the same script run twice must agree with itself.
    for n in (1, 2):
        (t / ('f%d' % n)).mkdir()
        run(repo / 'scripts/ugly/gen_font.py', t / ('f%d' % n))
    for px in (22, 30, 38, 52) if PYTHON else ():
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



def body(text, signature):
    """The braces of the function that starts at `signature`, or '' when it is not there."""
    start = text.find(signature)
    if start < 0:
        return ''
    level, i = 0, text.index('{', start)
    first = i
    while True:
        level += text[i] == '{'
        level -= text[i] == '}'
        i += 1
        if level == 0:
            return text[first:i]


# The pen's fonts go in at boot, never through the lock of a draw: a lock taken inside a path that already
# holds it would hang the device for good.
ink = (repo / 'src/shells/ugly/UglyInk.cpp').read_text()
check(body(ink, 'void ensureFonts(') != '', 'ensureFonts is gone from UglyInk.cpp')
check('RenderLock' not in body(ink, 'void ensureFonts('), 'ensureFonts takes the render lock')
boot = body((repo / 'src/main.cpp').read_text(), 'void setupDisplayAndFonts(')
check('ugly::ensureFonts' in boot, 'boot does not register the tenor/ugly fonts when the shell is in use')

# A desk whose picture cannot be unpacked (the heap is fragmented) says so in the log, then draws the labels.
desk = (repo / 'src/shells/ugly/UglyDesk.cpp').read_text()
check(re.search(r'if \(!decodeX3BrandPlane\([^;]*\)\) \{[^}]*LOG_ERR[^}]*clearScreen', desk) is not None,
      'the desk does not log the failed picture before it draws the bare table')

# The notebook reads the card outside the render lock and keeps the Folder names once.
book = (repo / 'src/shells/ugly/UglyNotebook.cpp').read_text()
check(re.search(r'\b(load|reload|read|turn)\(', body(book, 'bool Notebook::onKey(')) is None,
      'Notebook::onKey reads the card while the key queue holds the render lock')
folder = book[book.find('case homerows::Page::Folder: {'):book.find('case homerows::Page::Stats:')]
check('labels' not in folder, 'the Folder page keeps a second copy of the names')
check('folderCap' in folder, 'the Folder page has no ceiling')

if failures:
    print('\n'.join('FAIL: ' + f for f in failures))
    sys.exit(1)
print('PASS: generated files match their scripts, strings keep the rules, the voice stays in the shell')
