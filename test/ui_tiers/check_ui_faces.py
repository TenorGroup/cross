"""The shipped 1-bit UI faces: no Arabic or Hebrew, every Chinese and Vietnamese character, even stems,
and marks above a letter that sit on the plain letter without crushing it or being crushed.

Reads lib/EpdFont/builtinFonts. Pass face names to check fewer than the default five.
"""
from itertools import groupby
from pathlib import Path
import sys
import unicodedata
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'scripts'))
from check_chinese_ui import required
from font_header_tools import read_header
from gen_i18n import parse_yaml_file

FACES = ('geist_8_regular', 'geist_10_regular', 'geist_10_bold', 'geist_12_regular', 'geist_12_bold')
EVEN_STEMS = ('geist_12_regular', 'geist_12_bold')  # the body size; 8 and 10 are measured, not asserted
RTL_BLOCKS = ((0x0590, 0x06FF), (0xFB1D, 0xFEFC))  # Hebrew, Arabic and their presentation forms
MARKS_ABOVE = {0x0300, 0x0301, 0x0302, 0x0303, 0x0306, 0x0309}  # grave acute circumflex tilde breve hook
HOLLOW_BREVE = ('geist_10_regular', 'geist_10_bold', 'geist_12_regular', 'geist_12_bold')  # 8 is measured
BREVES = 'ĂăẮắẰằẲẳẴẵẶặ'


def stem_widths(glyphs):
    """Run lengths across the middle of the x-height, for letters that have vertical stems."""
    x_height = glyphs[ord('x')][0][4]
    widths = []
    for letter in 'bdhilmnpqru':
        (width, height, _, _, top), packed = glyphs[ord(letter)]
        row = top - x_height // 2
        bits = [bool(packed[i//8] & (128 >> (i % 8))) for i in range(row*width, (row+1)*width)]
        widths += [len(list(g)) for ink, g in groupby(bits) if ink]
    return widths


def ink(glyph):
    (width, height, _, left, top), packed = glyph
    return {(left + i % width, top - i // width) for i in range(width*height) if packed[i//8] & (128 >> (i % 8))}


def marks(glyphs):
    """Letters with marks above: (char, the plain letter's ink, the ink the marks add)."""
    for cp in glyphs:
        parts = unicodedata.normalize('NFD', chr(cp))
        if cp >= 0x2000 or len(parts) < 2 or not MARKS_ABOVE & set(map(ord, parts)):
            continue
        plain = unicodedata.normalize('NFC', ''.join(c for c in parts if ord(c) not in MARKS_ABOVE))
        plain = {'i': '\u0131', 'j': '\u0237'}.get(plain, plain)
        if len(plain) == 1 and ord(plain) in glyphs:
            body, marked = ink(glyphs[ord(plain)]), ink(glyphs[cp])
            yield chr(cp), body, marked - body, marked


def hollow(points):
    """White pixels held between two inked pixels of the same row: the bowl of a breve."""
    rows = {}
    for x, y in points:
        rows.setdefault(y, []).append(x)
    return sum(max(xs) - min(xs) + 1 - len(xs) for xs in rows.values())


for name in sys.argv[1:] or FACES:
    glyphs = read_header(ROOT/'lib/EpdFont/builtinFonts'/(name+'.h'))['glyphs']
    shipped = [hex(cp) for cp in glyphs if any(lo <= cp <= hi for lo, hi in RTL_BLOCKS)]
    assert not shipped, (name, 'Arabic or Hebrew glyphs shipped', shipped[:4], len(shipped))
    for locale in ('chinese', 'vietnamese', 'english'):
        wanted = required(parse_yaml_file(ROOT/f'lib/I18n/translations/{locale}.yaml'))
        assert wanted <= glyphs.keys(), (name, locale, 'missing glyphs', sorted(wanted - glyphs.keys())[:8])
    for ch in '界面ệữ':
        assert glyphs[ord(ch)][0][0] * glyphs[ord(ch)][0][1], (name, ch, 'empty glyph')
    for ch, body, added, marked in marks(glyphs):
        # The autohinter reshapes a letter under its marks: the A of "TẮT" stopped matching T's A.
        assert body <= marked, (name, ch, 'letter under the marks differs from the plain letter')
        tops = {}
        for x, y in body:
            tops[x] = max(y, tops.get(x, y))
        assert all(y > tops.get(x, y - 1) for x, y in added), (name, ch, 'mark ink inside the letter')
        if name in HOLLOW_BREVE and ch in BREVES:
            breve = {(x, y) for x, y in added if y > max(tops.values())}
            assert hollow(breve) >= 2, (name, ch, 'breve filled in', hollow(breve))
    widths = stem_widths(glyphs)
    print(name, 'stems', sorted(set(widths)), f'{round(100*widths.count(max(set(widths), key=widths.count))/len(widths))}% even')
    if name in EVEN_STEMS:
        assert len(set(widths)) == 1, (name, 'uneven stems', sorted(set(widths)))
print('PASS: UI faces')
