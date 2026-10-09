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
    run(repo / 'scripts/ugly/gen_quips.py', t)
    for name in ('UglyTables.h', 'UglyArt.h', 'UglySleepData.h', 'UglyQuips.h') if PYTHON else ():
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

# The lines of abuse: each block inflates to the table's lines, in the order the slots count them, and the
# marks point at every 32nd line.
import zlib  # noqa: E402

sys.path.insert(0, str(repo / 'scripts/ugly'))
import gen_quips  # noqa: E402

order, table, blocks, loose = gen_quips.build()
header = (repo / 'src/shells/ugly/UglyQuips.h').read_text()
for lang in ('vi', 'en'):
    name = lang.upper()
    m = re.search(r'inline constexpr uint8_t %s\[\] = \{(.*?)\};' % name, header, re.S)
    packed = bytes(int(b, 16) for b in re.findall(r'0x([0-9a-f]{2})', m.group(1)))
    lines = zlib.decompress(packed).split(b'\0')[:-1]
    check([l.decode() for l in lines] == [r[lang] for r in order], '%s block is not the table in slot order' % name)
    marks = [int(x) for x in re.search(r'%s_AT\[\] = \{(.*?)\}' % name, header).group(1).split(',')]
    raw = zlib.decompress(packed)
    for k, at in enumerate(marks[:-1]):
        check(at == 0 or raw[at - 1] == 0, '%s mark %d is not at a line start' % (name, k))
    check(marks[-1] == len(raw), '%s last mark is not the block size' % name)
for r in order:
    for lang in ('vi', 'en'):
        check(not any(c in r[lang] for c in ('—', '–', '·')), 'quip %s uses a dash or a middle dot' % r['key'])
check(len(table) > 200, 'the quip slots look empty')
said = set()
for path in (repo / 'src').rglob('*.cpp'):
    said |= set(re.findall(r'\bQuip::(\w+)', path.read_text(errors='ignore')))
check(said == gen_quips.SAID, 'the events src says (%s) are not gen_quips.SAID' % sorted(said ^ gen_quips.SAID))

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
        if lang in ('english', 'chinese') and key.startswith('STR_UGLY_START_'):
            check(not any(char in value for char in 'qjJ'), '%s %s uses a missing UI glyph' % (lang, key))

# The voice of the shell stays behind the shell: since every screen is drawn in tenor/ugly (founder 06/10/2026), a
# screen outside src/shells/ugly may name a STR_UGLY_ string, but only a file that asks the shell first.
for path in list((repo / 'src').rglob('*.cpp')) + list((repo / 'src').rglob('*.h')) + list((repo / 'lib').rglob('*.cpp')):
    rel = path.relative_to(repo)
    if str(rel).startswith('src/shells/ugly/') or str(rel).startswith('lib/I18n/'):
        continue
    text = path.read_text(errors='ignore')
    if str(rel) == 'src/SettingsList.h':
        allowed = {'STR_UGLY_START_SCREEN', 'STR_UGLY_START_BOOK', 'STR_UGLY_START_DIARY',
                   'STR_UGLY_START_RECENT', 'STR_UGLY_START_DESK'}
        check(set(re.findall(r'\bSTR_UGLY_\w+', text)) == allowed,
              'SettingsList.h may only name the shell-filtered start-screen strings')
        check('!ugly && setting.valuePtr == &CrossPointSettings::uglyStartScreen' in text,
              'the cross shell must hide the ugly start row')
        check('settingHiddenOnThisBoard(setting) || settingHiddenInShell(setting)' in text and
              'v.end(), settingHiddenInShell)' in text,
              'device and web catalogs must both apply the shell filter')
        continue
    if 'STR_UGLY_' in text and not re.search(r'shell::(isUgly|uglyParts)\(\)', text):
        check(False, '%s names a STR_UGLY_ string without asking the shell' % rel)



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
letter = body(ink, 'Letter letterOf(')
check(letter.count('getGlyph(') == 1 and letter.count('logic::warpOf(') == 1,
      'a letter must resolve its glyph and warp once')
for signature in ('int stepOf(', 'void drawWarped('):
    check('getGlyph(' not in body(ink, signature) and 'logic::warpOf(' not in body(ink, signature),
          signature + ' must reuse the prepared letter')
text_run = body(ink, 'int run(')
check(text_run.count('letterOf(') == 1 and 'drawWarped(r, s, letter,' in text_run and
      'stepOf(r, s, af, pos, cp, letter)' in text_run,
      'drawing and measuring must share the prepared letter')
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
