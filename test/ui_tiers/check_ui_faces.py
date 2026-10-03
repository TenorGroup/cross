"""The shipped 1-bit UI faces: no Arabic or Hebrew, every Chinese and Vietnamese character, even stems.

Reads lib/EpdFont/builtinFonts. Pass face names to check fewer than the default five.
"""
from itertools import groupby
from pathlib import Path
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'scripts'))
from check_chinese_ui import required
from font_header_tools import read_header
from gen_i18n import parse_yaml_file

FACES = ('geist_8_regular', 'geist_10_regular', 'geist_10_bold', 'geist_12_regular', 'geist_12_bold')
EVEN_STEMS = ('geist_12_regular', 'geist_12_bold')  # the body size; 8 and 10 are measured, not asserted
RTL_BLOCKS = ((0x0590, 0x06FF), (0xFB1D, 0xFEFC))  # Hebrew, Arabic and their presentation forms


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


for name in sys.argv[1:] or FACES:
    glyphs = read_header(ROOT/'lib/EpdFont/builtinFonts'/(name+'.h'))['glyphs']
    shipped = [hex(cp) for cp in glyphs if any(lo <= cp <= hi for lo, hi in RTL_BLOCKS)]
    assert not shipped, (name, 'Arabic or Hebrew glyphs shipped', shipped[:4], len(shipped))
    for locale in ('chinese', 'vietnamese', 'english'):
        wanted = required(parse_yaml_file(ROOT/f'lib/I18n/translations/{locale}.yaml'))
        assert wanted <= glyphs.keys(), (name, locale, 'missing glyphs', sorted(wanted - glyphs.keys())[:8])
    for ch in '界面ệữ':
        assert glyphs[ord(ch)][0][0] * glyphs[ord(ch)][0][1], (name, ch, 'empty glyph')
    widths = stem_widths(glyphs)
    print(name, 'stems', sorted(set(widths)), f'{round(100*widths.count(max(set(widths), key=widths.count))/len(widths))}% even')
    if name in EVEN_STEMS:
        assert len(set(widths)) == 1, (name, 'uneven stems', sorted(set(widths)))
print('PASS: UI faces')
