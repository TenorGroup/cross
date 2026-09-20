"""Compile the actual footer functions against shipped 1-bit caption fonts.

No simulator, firmware build or device access is needed. The small renderer
records actual font pixels and the footer's clear rectangles in painting order.
"""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--legacy-clear', action='store_true', help='restore the old clear rectangle in the test unit')
args = parser.parse_args()


def function(source, name):
    start = source.index(name)
    start = source.rfind('\n', 0, start) + 1
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


chrome = (ROOT / 'src/components/TenorMenuChrome.cpp').read_text()
theme = (ROOT / 'src/components/themes/TenorTheme.cpp').read_text()
base = (ROOT / 'src/components/themes/BaseTheme.cpp').read_text()
header = (ROOT / 'src/components/TenorMenuChrome.h').read_text()
definitions = []
for name in ('smallFooterSymbolsTopY', 'compactFooterTips', 'tipY', 'drawTip'):
    if f'tenorchrome::{name}(' in chrome:
        definitions.append(function(chrome, f'tenorchrome::{name}('))
definitions.append(function(theme, 'TenorTheme::drawButtonHints('))
definitions.append(function(base, 'BaseTheme::drawHintLabel('))
declarations = '\n'.join(re.findall(r'^(?:bool|int|void) (?:smallFooterSymbolsTopY|compactFooterTips|tipY|drawTip)\([^;]+;', header, re.M))
fixture = Path(__file__).with_name('footer_tips.cpp').read_text()
fixture = fixture.replace('// CHROME_DECLARATIONS', declarations)
fixture = fixture.replace('// PRODUCTION_FUNCTIONS', '\n'.join(definitions))
if args.legacy_clear:
    fixture = fixture.replace('compactSymbol ? tenorchrome::smallFooterSymbolsTopY(renderer) : pageHeight - buttonY',
                              'pageHeight - buttonY')
# Before the new optional argument exists, still run the old implementation:
# the RED evidence must be a geometry failure, not a compiler failure.
if 'hasTextHints' not in declarations:
    fixture = fixture.replace(', 0, 4, true)', ', 0, 4)')
with tempfile.TemporaryDirectory(prefix='footer-tips-') as folder:
    source = Path(folder) / 'test.cpp'
    program = Path(folder) / 'test'
    source.write_text(fixture)
    subprocess.run(['c++', '-std=c++17', '-O0', '-UNDEBUG', '-I', str(ROOT / 'lib/EpdFont'),
                    str(source), '-o', str(program)], check=True)
    subprocess.run([str(program)], check=True)
