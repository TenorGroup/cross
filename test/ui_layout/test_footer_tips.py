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
parser.add_argument('--legacy-wrap', action='store_true', help='restore ascender-based small wrapped-label spacing')
parser.add_argument('--source-ref', help='read production functions from this git ref for regression evidence')
args = parser.parse_args()


def read_source(path):
    if args.source_ref:
        return subprocess.check_output(['git', 'show', f'{args.source_ref}:{path}'], cwd=ROOT, text=True)
    return (ROOT / path).read_text()


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


chrome = read_source('src/components/TenorMenuChrome.cpp')
theme = read_source('src/components/themes/TenorTheme.cpp')
base = read_source('src/components/themes/BaseTheme.cpp')
renderer = read_source('lib/GfxRenderer/GfxRenderer.cpp')
header = read_source('src/components/TenorMenuChrome.h')
definitions = []
for name in ('smallFooterSymbolsTopY', 'compactFooterTips', 'tipY'):
    if f'tenorchrome::{name}(' in chrome:
        definitions.append(function(chrome, f'tenorchrome::{name}('))
for signature in ('std::vector<std::string> tipLines(', 'int tipLineY('):
    if signature in chrome:
        definitions.append(function(chrome, signature))
# v1.0.14: the down chevron is the shared "more this way" V.
for constant in re.findall(r'constexpr int MORE_BELOW_SPAN[^;]+;', chrome):
    definitions.append(constant)
if 'void tenorchrome::drawMoreChevron(' in chrome:
    definitions.append(function(chrome, 'void tenorchrome::drawMoreChevron('))
for name in ('tipTopY', 'tipLineCount', 'tipHeight', 'drawTip', 'moreBelowChevronTopY', 'drawMoreBelowChevron'):
    if f'tenorchrome::{name}(' in chrome:
        definitions.append(function(chrome, f'tenorchrome::{name}('))
definitions.append(function(theme, 'TenorTheme::drawButtonHints('))
hint_label = function(base, 'BaseTheme::drawHintLabel(')
if args.legacy_wrap:
    start = hint_label.index('  const auto lines = renderer.wrappedText(')
    end = hint_label.index('  const int block =', start)
    hint_label = hint_label[:start] + '''  constexpr int lineGap = 2;
  const int step = (small ? renderer.getTextHeight(fontId) : renderer.getLineHeight(fontId)) + lineGap;
  const auto lines = renderer.wrappedText(fontId, label, maxTextWidth, 2);
''' + hint_label[end:]
definitions.append(hint_label)
definitions.append(function(renderer, 'GfxRenderer::wrappedText(').replace('GfxRenderer::wrappedText(', 'GfxRenderer::wrappedTextProduction('))
for name in ('getTextInkTop', 'getTextInkBottom'):
    definitions.append(function(renderer, f'GfxRenderer::{name}('))
declarations = '\n'.join(re.findall(r'^(?:bool|int|void) (?:smallFooterSymbolsTopY|compactFooterTips|tipY|tipTopY|tipLineCount|tipHeight|drawTip|moreBelowChevronTopY|drawMoreBelowChevron)\([^;]+;', header, re.M))
declarations += '\n' + '\n'.join(re.findall(r'(?:enum class ChevronDir[^;]+;|constexpr int MORE_CHEVRON_[^;]+;|'
                                             r'constexpr int moreChevronLength[^}]+}|void drawMoreChevron\([^;]+;)', header))
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
                    '-I', str(ROOT / 'lib/Utf8'), '-I', str(ROOT / 'lib/GfxRenderer'),
                    str(source), str(ROOT / 'lib/EpdFont/EpdFont.cpp'), str(ROOT / 'lib/EpdFont/EpdFontFamily.cpp'),
                    str(ROOT / 'lib/Utf8/Utf8.cpp'),
                    '-o', str(program)], check=True)
    subprocess.run([str(program)], check=True)
