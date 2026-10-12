from pathlib import Path
import argparse
import re
import subprocess
from phase_two import extend, verify_keyboard
from shared_path_fixture import bitmap_build_args, extend_path

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--work', type=Path, required=True)
parser.add_argument('--source-ref')
parser.add_argument('--tip-only', action='store_true')
parser.add_argument('--legacy-clear', action='store_true')
parser.add_argument('--shell', choices=('buttons', 'touch', 'ugly'), default='buttons')
args = parser.parse_args()

PHASE_TWO = (
    'ButtonRemap', 'ChapterNumberEntry', 'Crash', 'KeyboardEntry', 'QrDisplay',
    'ReadingHabits', 'ReadingHistory', 'BlePageTurner', 'CalibreConnect', 'ClockSync',
    'CrossPointWebServer', 'KOReaderAuth', 'KOReaderSync', 'NetworkModeSelection',
    'OpdsBookBrowser', 'OtaUpdate', 'SdFirmwareUpdate', 'WifiSelection',
)


def read_source(path):
    if args.source_ref:
        return subprocess.check_output(['git', 'show', f'{args.source_ref}:{path}'], cwd=ROOT, text=True)
    return (ROOT / path).read_text()


def function(source, name):
    start = source.rfind('\n', 0, source.index(name)) + 1
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


header = read_source('src/components/TenorMenuChrome.h')
chrome = read_source('src/components/TenorMenuChrome.cpp')
required = ('X3ScreenLayout', 'x3ScreenLayout(', 'drawTipInRect(', 'drawInfoInRect(')
missing = [name for name in required if name not in header]
if missing and not args.tip_only:
    raise SystemExit('RED: base lacks layout contract: ' + ', '.join(missing))

fixture = (ROOT / 'test/ui_layout/footer_tips.cpp').read_text().split('int main(')[0]
fixture = fixture.replace('constexpr int SMALL_FONT_ID = 1;', '')
fixture = fixture.replace('int normalizedUiTextSize(int value) { return value; }', '')
fixture = fixture.replace('480 * 800', '528 * 800').replace('return 480;', 'return 528;')
fixture = fixture.replace('x < 480', 'x < 528').replace('y * 480 + x', 'y * 528 + x')
fixture = fixture.replace('#include <builtinFonts/geist_12_regular.h>',
                          '#include <builtinFonts/geist_12_regular.h>\n'
                          '#include <builtinFonts/geist_14_regular.h>\n'
                          '#include <builtinFonts/geist_16_regular.h>')
fixture = fixture.replace('  const std::map<int, EpdFontFamily> fontMap =', '''  const EpdFont& bodyFontObject() const {
    static const EpdFont fonts[] = {EpdFont(&geist_12_regular), EpdFont(&geist_14_regular), EpdFont(&geist_16_regular)};
    return fonts[SETTINGS.uiTextSize];
  }
  const EpdFontData& bodyFontData() const {
    return SETTINGS.uiTextSize == 0 ? geist_12_regular : SETTINGS.uiTextSize == 1 ? geist_14_regular : geist_16_regular;
  }
  const std::map<int, EpdFontFamily> fontMap =''')
fixture = fixture.replace('int getFontAscenderSize(int) const { return font().ascender; }',
                          'int getFontAscenderSize(int id) const { return id == UI_12_FONT_ID ? bodyFontData().ascender : font().ascender; }')
fixture = fixture.replace('int getLineHeight(int) const { return font().advanceY; }',
                          'int getLineHeight(int id) const { return id == UI_12_FONT_ID ? bodyFontData().advanceY : font().advanceY; }')
fixture = fixture.replace('{{SMALL_FONT_ID, EpdFontFamily(&fontObject())}}',
                          '{{SMALL_FONT_ID, EpdFontFamily(&fontObject())}, '
                          '{UI_12_FONT_ID, EpdFontFamily(&bodyFontObject())}}')
fixture = fixture.replace('tenorButtonSymbols = true;',
                          'tenorButtonSymbols = true; bool tipsEnabled = true;')
if args.shell == 'ugly':
    fixture = fixture.replace('bool uglyParts() { return false; }', 'bool uglyParts() { return true; }')
fixture = fixture.replace('inline bool enabled() { return true; }',
                          'inline bool enabled() { return true; }\nconstexpr bool kTouchShell = ' +
                          ('true' if args.shell == 'touch' else 'false') + ';')
start = fixture.index('int statusTextGrowth(')
end = fixture.index('struct StatusCornerBounds', start)
status = '\n'.join(re.findall(r'constexpr int STATUS_[A-Z_]+[^;]+;', header)) + '\n'
for name in ('inline int statusTextGrowth(', 'inline int statusTextY(', 'inline int statusIconTopY('):
    status += function(header, name) + '\n'
fixture = fixture[:start] + status + fixture[end:]
declarations = ['bool x3ScreenLayoutEnabled();']
for name in (() if args.tip_only else ('struct X3ScreenRect', 'struct X3ScreenLayout')):
    declarations.append(function(header, name) + ';')
for name in ('inline bool tipShown(', 'inline int tabHeight('):
    declarations.append(function(header, name))
declarations.append('constexpr int TAB_HEIGHT = 60;')
declarations[-2], declarations[-1] = declarations[-1], declarations[-2]
declarations.extend(re.findall(r'^(?:bool|int|void|X3ScreenLayout) '
                              r'(?:x3ScreenLayoutEnabled|x3ScreenLayout|smallFooterSymbolsTopY|compactFooterTips|'
                              r'tipY|tipTopY|tipLineCount|tipHeight|uglyTipTop|drawTip|drawTipInRect|drawInfoInRect)'
                              r'\([^;]+;', header, re.M))
declarations.append('inline int touchBarTop(int height) { return height; }')
# Button hints on: the status stays at the foot (the hints-off strip: test_button_hints_v1059.py).
declarations.append('constexpr int TOUCH_STRIP_HEIGHT = 32;')
declarations.append('inline bool buttonHintsShown() { return !SETTINGS.globalStatusBarHidden(); }')
declarations.append('inline bool statusOnTop() { return false; }')
if not args.tip_only:
    declarations.append('''struct PhaseInk { X3ScreenRect rect; std::string text; bool tip; };
std::vector<PhaseInk> phaseInk;
bool phaseTipDrawing = false;''')
definitions = []
for name in ('smallFooterSymbolsTopY', 'compactFooterTips', 'tipY', 'x3ScreenLayoutEnabled'):
    if f'tenorchrome::{name}(' not in chrome:
        continue
    definitions.append(function(chrome, f'tenorchrome::{name}('))
for name in ('std::vector<std::string> tipLines(', 'int tipLineY('):
    definitions.append(function(chrome, name))
for name in ('tipTopY', 'tipLineCount', 'tipHeight', 'uglyTipTop', 'drawTip', 'x3ScreenLayout',
             'drawTipInRect', 'drawInfoInRect'):
    if args.tip_only and name in ('x3ScreenLayout', 'drawTipInRect', 'drawInfoInRect'):
        continue
    definition = function(chrome, f'tenorchrome::{name}(')
    if name == 'drawInfoInRect':
        definition = re.sub(r'(  if \(rect.height <= 0[^;]+return;)',
            r'\1\n  phaseInk.push_back({rect, text ? text : "", phaseTipDrawing});', definition, count=1)
    if name == 'drawTipInRect':
        definition = definition.replace('  drawInfoInRect(renderer, text, rect, maxLines);',
            '  phaseTipDrawing = true;\n  drawInfoInRect(renderer, text, rect, maxLines);\n  phaseTipDrawing = false;')
    definitions.append(definition)
fixture = fixture.replace('int getTextInkTop(int, const char*, EpdFontFamily::Style) const;', '''std::array<int, 4> getTextInkBounds(int, const char*, EpdFontFamily::Style) const;
  void ensureSdGlyphsResident(int, const char*, EpdFontFamily::Style, bool) const {}
  const uint8_t* getGlyphBitmap(const EpdFontData* data, const EpdGlyph* glyph) const {
    return fixtureGlyphBitmap(data, glyph);
  }
  int getTextInkTop(int, const char*, EpdFontFamily::Style) const;''')
renderer = (ROOT / 'lib/GfxRenderer/GfxRenderer.cpp').read_text()
definitions.append(function(renderer, 'GfxRenderer::wrappedText(').replace(
    'GfxRenderer::wrappedText(', 'GfxRenderer::wrappedTextProduction('))
for name in ('getTextInkTop', 'getTextInkBottom', 'getTextInkBounds'):
    definitions.append(function(renderer, f'GfxRenderer::{name}(').replace(' || BidiUtils::isTransparentMark(cp)', ''))
if not args.tip_only:
    theme = read_source('src/components/themes/TenorTheme.cpp')
    base = read_source('src/components/themes/BaseTheme.cpp')
    footer = function(theme, 'TenorTheme::drawButtonHints(')
    if args.legacy_clear:
        footer = re.sub(r'const int clearTop = .*?;',
                        'const int clearTop = compactSymbol ? tenorchrome::smallFooterSymbolsTopY(renderer) : pageHeight - buttonY;',
                        footer, count=1, flags=re.S)
    definitions.append(footer)
    definitions.append(function(base, 'BaseTheme::drawHintLabel('))
fixture = fixture.replace('struct GfxRenderer {', '''struct GfxRenderer {
  mutable std::array<int, 4> clip{0, 0, 528, 800};
  auto getClipRect() const { return clip; }
  void setClipRect(int left, int top, int width, int height) const { clip = {left, top, width, height}; }
''')
fixture = '#include <array>\n#include <climits>\n#include "components/UIScale.h"\n' + fixture
fixture = fixture.replace('// CHROME_DECLARATIONS', '\n'.join(declarations))
fixture = fixture.replace('// PRODUCTION_FUNCTIONS', '\n'.join(definitions))
fixture = extend_path(fixture, chrome, function, False, ROOT)
if args.shell != 'buttons':
    fixture = fixture.replace(
        'void drawPillRing(const GfxRenderer&, int, int, int, int, int, bool);',
        'inline void drawPillRing(const GfxRenderer&, int, int, int, int, int, bool) {}')
if args.shell == 'ugly':
    fixture += '''int main() {
      SETTINGS.tipsEnabled = false;
      assert(tenorchrome::x3ScreenLayoutEnabled());
      assert(!tenorchrome::tipShown("regular tip"));
      SETTINGS.tipsEnabled = true;
      assert(tenorchrome::tipShown("regular tip"));
      std::puts("GREEN: ugly uses shared layout and tips switch");
    }'''
elif args.shell != 'buttons':
    fixture += '''int main() {
      SETTINGS.tipsEnabled = false;
      assert(!tenorchrome::x3ScreenLayoutEnabled());
      assert(tenorchrome::tipShown("regular tip"));
      assert(tenorchrome::tipShown("\\xEE\\x84\\x80") == !tenorchrome::kTouchShell);
      std::puts("GREEN: touch/ugly keep legacy tip and layout gates");
    }'''
elif args.tip_only:
    fixture += '''int main() {
      GfxRenderer renderer;
      SETTINGS.hidden = true;
      tenorchrome::drawTip(renderer, "Hold OK: Pin");
      const bool statusIndependent = !renderer.paintedText.empty();
      renderer.paintedText.clear();
      SETTINGS.hidden = false;
      SETTINGS.tipsEnabled = false;
      tenorchrome::drawTip(renderer, "Hold OK: Pin");
      const bool tipsOff = renderer.paintedText.empty();
      std::printf("statusIndependent=%d tipsOff=%d\\n", statusIndependent, tipsOff);
      return statusIndependent && tipsOff ? 0 : 1;
    }'''
else:
    home_source = read_source('src/activities/home/HomeActivity.cpp')
    fixture += Path(__file__).with_name('FormsHomeFooterHarness.h').read_text()
    for name in ('drawFooter', 'homeTipText', 'missingFileText', 'homeInfoText', 'homeScreenLayout'):
        fixture += '\n' + function(home_source, f'HomeActivity::{name}(')
    fixture, phase_two_cases, phase_two_states = extend(ROOT, read_source, function, fixture, PHASE_TWO)
    tests = Path(__file__).with_name('FormsScreenLayoutTest.cpp').read_text()
    tests = tests.replace('// PHASE_TWO_MATRIX', phase_two_cases)
    fixture += tests
args.work.mkdir(parents=True, exist_ok=True)
unit = args.work / 'layout.cpp'
unit.write_text('#include <cstdlib>\n#include "BitmapFixture.h"\n' + fixture)
program = args.work / 'layout'
subprocess.run(['c++', '-std=c++17', '-O0', '-UNDEBUG',
                *bitmap_build_args(ROOT, args.work), '-I', str(ROOT / 'src'),
                '-I', str(ROOT / 'lib/EpdFont'), '-I', str(ROOT / 'lib/Utf8'),
                '-I', str(ROOT / 'lib/GfxRenderer'), str(unit),
                '-I', str(ROOT / 'freeink-sdk/libs/assets/Icons/include'),
                str(ROOT / 'lib/Utf8/Utf8.cpp'), str(ROOT / 'lib/EpdFont/EpdFont.cpp'),
                str(ROOT / 'lib/EpdFont/EpdFontFamily.cpp'),
                '-o', str(program)], check=True)
execution = subprocess.run([str(program)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
print(execution.stdout, end='')
execution.check_returncode()
if args.tip_only or args.shell != 'buttons':
    raise SystemExit(0)

phase_two_failures = []
for name in PHASE_TWO:
    path = next(ROOT.glob(f'src/activities/**/{name}Activity.cpp'))
    source = read_source(str(path.relative_to(ROOT)))
    if 'x3ScreenLayout(renderer' not in source or 'x3activity::' not in source:
        phase_two_failures.append(name)
        print(f'RED: {name}: missing shared body/tip/bar binding')
if phase_two_failures:
    raise SystemExit(f'RED: {len(phase_two_failures)}/18 phase-two activities')
verify_keyboard(ROOT, args.work, execution.stdout, function)

tabs = (ROOT / 'src/activities/UiTabListActivity.cpp').read_text()
home = (ROOT / 'src/activities/home/HomeActivity.cpp').read_text()
assert 'x3ScreenLayout(renderer)' in function(tabs, 'void UiTabListActivity::buildTabBar(')
assert 'homeScreenLayout()' in function(home, 'void HomeActivity::buildScreen(')
assert 'homeScreenLayout()' in function(home, 'void HomeActivity::drawRecentCard(')
footer = function(home, 'void HomeActivity::drawFooter(')
assert 'drawInfoInRect' in footer and 'missingFileText()' in footer and 'statsResetTip' in footer
assert 'drawTipInRect' in footer
print('GREEN: shared tab placement and Home body/tip/info bindings')
