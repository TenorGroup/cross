"""Exercise the production stat painter and measured cover placement with real glyph metrics."""
from pathlib import Path
import argparse
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--baseline', action='store_true')
parser.add_argument('--mutation', action='store_true')
a = parser.parse_args()
a.output = a.output.resolve()
root = Path(__file__).resolve().parents[2]
a.output.mkdir(parents=True, exist_ok=True)
source = subprocess.check_output(['git','show','bcb678d:src/activities/home/HomeActivity.cpp'],cwd=root,text=True) if a.baseline else (root / 'src/activities/home/HomeActivity.cpp').read_text()
start = source.index('static int cardStatRunWidth(')
end = source.index('\nvoid HomeActivity::drawRecentCard()', start)
methods = source[start:end]
new = 'HomeStatsBounds* bounds' in methods
placement = source[source.index('  const auto size = tenorchrome::kTouchShell ? cardCoverSize(shown)'):source.index('  heldCover =', source.index('  const auto size = tenorchrome::kTouchShell ? cardCoverSize(shown)'))] if new else 'auto frame = homeCardLayout(in);'
fixture = Path(__file__).with_name('home_cover_alignment_fixture.cpp').read_text()
fixture = fixture.replace('@@METHODS@@', methods)
fixture = fixture.replace('@@PLACE@@', placement)
if not new:
    fixture = fixture.replace('cardStatInkBounds(h.renderer,UI_10_FONT_ID,LABELS[0],EpdFontFamily::REGULAR)', 'HomeStatsBounds{h.renderer.getTextInkTop(UI_10_FONT_ID,LABELS[0],EpdFontFamily::REGULAR),0}')
fixture = fixture.replace('@@MEASURE@@', 'h.drawCardStats(card, &bounds);' if new else 'h.drawCardStats(card); bounds={h.renderer.inkTop, h.renderer.inkBottom};')
fixture = fixture.replace('@@ALIGN@@', '')
fixture = fixture.replace('int drawCardStats(const HomeCardLayout& card, HomeStatsBounds* bounds=nullptr);', 'int drawCardStats(const HomeCardLayout& card);') if not new else fixture
fixture = fixture.replace('@@BOUNDS@@', '' if new else 'struct HomeStatsBounds { int top=0, bottom=0; };')
include = a.output / 'include/components'
include.mkdir(parents=True, exist_ok=True)
header = subprocess.check_output(['git','show','bcb678d:src/components/HomeExcerptStyle.h'],cwd=root,text=True) if a.baseline else (root / 'src/components/HomeExcerptStyle.h').read_text()
if a.mutation: header = header.replace('card.coverY = ink.top + (height - card.coverH) / 2;', 'card.coverY = 77;')
(include / 'HomeExcerptStyle.h').write_text(header)
cpp = a.output / 'fixture.cpp'
cpp.write_text(fixture)
exe = a.output / 'fixture'
subprocess.run(['c++', '-std=c++20', '-O1', '-g', '-UNDEBUG', '-fsanitize=address,undefined',
                '-I' + str(a.output / 'include'), '-I' + str(root / 'src'), '-I' + str(root / 'lib/EpdFont'), '-I' + str(root / 'lib/Utf8'),
                str(cpp), str(root / 'lib/EpdFont/EpdFont.cpp'), str(root / 'lib/Utf8/Utf8.cpp'), '-o', str(exe)], check=True)
result = subprocess.run([str(exe)], cwd=a.output)
if a.mutation:
    assert result.returncode == 1, 'fixed-y mutation survived'
    print('GREEN: fixed-y=77 mutation rejected')
    raise SystemExit(0)
raise SystemExit(result.returncode)
