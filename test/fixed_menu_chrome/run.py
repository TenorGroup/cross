from pathlib import Path
import argparse
import json
import re
import subprocess
import sys
from shared_path_fixture import bitmap_build_args, extend_path

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--work', type=Path, required=True)
parser.add_argument('--source-ref')
parser.add_argument('--tip-only', action='store_true')
parser.add_argument('--legacy-clear', action='store_true')
parser.add_argument('--phase2', action='store_true')
parser.add_argument('--forms', action='store_true')
parser.add_argument('--mutate-bar', action='store_true')
parser.add_argument('--shell', choices=('buttons', 'touch', 'ugly'), default='buttons')
parser.add_argument('--ugly-contract', action='store_true')
parser.add_argument('--ugly-status-contract', action='store_true')
parser.add_argument('--footer-spacing', action='store_true')
parser.add_argument('--settings-language', choices=('vietnamese', 'english', 'chinese'))
args = parser.parse_args()
if args.ugly_status_contract:
    args.ugly_contract = True
if args.forms:
    command = [sys.executable, str(Path(__file__).with_name('forms_run.py')), '--work', str(args.work),
               '--shell', args.shell]
    if args.source_ref:
        command.extend(['--source-ref', args.source_ref])
    raise SystemExit(subprocess.call(command))

SETTINGS_CLASSES = (
    'AboutActivity', 'BookStatsActivity', 'BookStatsLibraryActivity', 'ClearCacheActivity',
    'ClockSettingsActivity', 'FontDownloadActivity', 'InfoUpdateActivity', 'KeyboardLayoutsActivity',
    'LanguageSelectActivity', 'KOReaderSettingsActivity', 'OpdsServerListActivity', 'OpdsSettingsActivity',
    'SettingsActivity', 'StatusBarSettingsActivity', 'TextSettingsActivity', 'TimezonePickerActivity',
)
PHASE2 = {
    'home': ('FileBrowserActivity', 'QuoteDetailActivity', 'QuoteTrimActivity', 'QuotesActivity'),
    'reader': ('EpubReaderBookmarksActivity', 'EpubReaderChapterSelectionActivity',
               'EpubReaderMenuActivity', 'XtcReaderChapterSelectionActivity'),
}


def read_source(path):
    if args.source_ref:
        source = subprocess.check_output(['git', 'show', f'{args.source_ref}:{path}'], cwd=ROOT, text=True)
    else:
        source = (ROOT / path).read_text()
    if args.mutate_bar and path == 'src/activities/home/FileBrowserActivity.cpp':
        original = function(source, 'FileBrowserActivity::screenLayout(')
        changed = original.replace('return tenorchrome::x3ScreenLayout(', 'auto layout = tenorchrome::x3ScreenLayout(')
        changed = changed[:-1] + '  ++layout.barRect.y;\n  return layout;\n}'
        source = source.replace(original, changed)
    return source


def function(source, name):
    start = source.rfind('\n', 0, source.index(name)) + 1
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


if args.phase2:
    failures = []
    for directory, classes in PHASE2.items():
        for activity in classes:
            source = read_source(f'src/activities/{directory}/{activity}.cpp')
            if f'{activity}::screenLayout(' not in source:
                failures.append(activity)
                print(f'RED: {activity} has no shared body/tip/bar binding (18 cases)')
    if failures:
        raise SystemExit(1)


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
fixture = fixture.replace('mutable std::vector<int> ys;',
                          'mutable std::vector<int> ys, textFontIds, textXs;')
fixture = fixture.replace('void drawText(int, int x, int y, const char* text) const {',
    'void drawText(int fontId, int x, int y, const char* text) const { textFontIds.push_back(fontId);')
fixture = fixture.replace('int getTextWidth(int, const char* text,', 'int getTextWidth(int fontId, const char* text,')
fixture = fixture.replace('EpdFont(&font()).getTextDimensions(text, &width, &height);',
                          'EpdFont(fontId == UI_12_FONT_ID ? &bodyFontData() : &font()).getTextDimensions(text, &width, &height);')
fixture = fixture.replace('const auto* g = glyph(next(text));',
                          'const auto& bitmapFont = fontId == UI_12_FONT_ID ? bodyFontData() : font();\n'
                          '      const auto* g = fontId == UI_12_FONT_ID ? bodyFontObject().getGlyph(next(text)) : glyph(next(text));')
fixture = fixture.replace('if (font().bitmap[g->dataOffset', 'if (bitmapFont.bitmap[g->dataOffset')
fixture = fixture.replace('y + font().ascender - g->top + row', 'y + bitmapFont.ascender - g->top + row')
fixture = fixture.replace(function(fixture, 'void drawText('), r'''  void drawText(int fontId, int x, int y, const char* text) const {
    textFontIds.push_back(fontId); ys.push_back(y); textXs.push_back(x); paintedText.emplace_back(text);
    const auto& family = fontMap.at(fontMap.count(fontId) ? fontId : SMALL_FONT_ID);
    const auto* data = family.getData();
    int baseX = x, baseLeft = 0, baseWidth = 0, baseTop = 0;
    int32_t advance = 0;
    uint32_t previous = 0;
    while (uint32_t cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text))) {
      const bool combining = utf8IsCombiningMark(cp);
      if (!combining) {
        cp = family.applyLigatures(cp, text);
        if (previous) baseX += fp4::toPixel(advance + family.getKerning(previous, cp));
      }
      const auto* glyph = family.getGlyph(cp);
      if (!glyph) continue;
      const auto anchor = combiningMark::anchorFor(cp);
      const int glyphX = combining ? combiningMark::anchorOver(anchor, baseX, baseLeft, baseWidth,
                                                                glyph->left, glyph->width) : baseX;
      const int raise = combining ? combiningMark::raiseAboveBase(anchor, glyph->top, glyph->height, baseTop) : 0;
      const auto* bitmap = fixtureGlyphBitmap(data, glyph);
      for (int row = 0; row < glyph->height; ++row)
        for (int col = 0; col < glyph->width; ++col) {
          const int bit = row * glyph->width + col;
          const int ink = data->is2Bit ? (bitmap[bit / 4] >> (6 - (bit % 4) * 2)) & 3
                                       : (bitmap[bit / 8] >> (7 - bit % 8)) & 1;
          if (ink)
            drawPixel(glyphX + glyph->left + col, y + data->ascender - glyph->top - raise + row, true);
        }
      if (!combining) {
        baseLeft = glyph->left; baseWidth = glyph->width; baseTop = glyph->top;
        advance = glyph->advanceX; previous = cp;
      }
    }
  }''')
if args.phase2:
    fixture = '#include <builtinFonts/notoserif_16_regular.h>\n#include <builtinFonts/notoserif_18_regular.h>\n' + fixture
    fixture = fixture.replace("if (!std::strchr(text, '|')) return wrappedTextProduction",
                              'if (true) return wrappedTextProduction')
    fixture = fixture.replace('return id == UI_12_FONT_ID ? bodyFontData().advanceY : font().advanceY;',
        'return id == NOTOSERIF_16_FONT_ID ? notoserif_16_regular.advanceY : '
        'id == NOTOSERIF_18_FONT_ID ? notoserif_18_regular.advanceY : '
        'id == UI_12_FONT_ID ? bodyFontData().advanceY : font().advanceY;')
fixture = fixture.replace('tenorButtonSymbols = true;',
                          'tenorButtonSymbols = true; bool tipsEnabled = true;')
if args.footer_spacing:
    fixture = '#include <InlineSymbolBitmaps.h>\n' + fixture
    fixture = fixture.replace('int getScreenWidth() const { return 528; }',
                              'int screenWidth = 528; int getScreenWidth() const { return screenWidth; }')
    fixture = fixture.replace('StatusCornerBounds statusCornerBounds(const GfxRenderer&, bool) { return {65, 435}; }',
                              'StatusCornerBounds measuredCorners{65, 435};\n'
                              'StatusCornerBounds statusCornerBounds(const GfxRenderer&, bool) { return measuredCorners; }')
    fixture = fixture.replace(function(fixture, 'SymbolBounds horizontalBounds('), '''
      const inlineSymbolBitmaps::Glyph& footerGlyph(const char* label, int net) {
        using namespace inlineSymbolBitmaps;
        const bool large = net == 18;
        if (*label == 'b') return large ? kBackNet18 : kBackNet14;
        if (*label == 'c') return large ? kSelectNet18 : kSelectNet14;
        if (*label == 'u') return large ? kUpNet18 : kUpNet14;
        return large ? kDownNet18 : kDownNet14;
      }
      SymbolBounds horizontalBounds(const char* label, int net) {
        const int width = footerGlyph(label, net).width;
        return {width / 2, width - 1 - width / 2};
      }
    ''')
    fixture = fixture.replace(function(fixture, 'bool drawLabel('), '''
      std::vector<std::array<int, 2>> paintedBounds;
      bool drawLabel(const GfxRenderer& renderer, const char* label, int center, int top, int net) {
        const auto& glyph = footerGlyph(label, net);
        const int left = center - glyph.width / 2;
        paintedBounds.push_back({left, left + glyph.width - 1});
        for (int row = 0; row < glyph.height; ++row)
          for (int column = 0; column < glyph.width; ++column)
            if (glyph.rows[row] & (1u << (glyph.width - column - 1)))
              renderer.drawPixel(left + column, top - glyph.height / 2 + row, true);
        return true;
      }
    ''')
if args.shell == 'ugly' or args.ugly_contract:
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
for name in ('struct X3ScreenRect', 'struct X3ScreenLayout'):
    if name not in header and args.tip_only:
        continue
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
declarations.append('inline int tabTop() { return 32; }')
# Button hints on (the default): the status stays at the foot. The hints-off strip is checked on the simulator
# (test/reading_stats_simulator/test_button_hints_v1059.py).
declarations.append('constexpr int TOUCH_STRIP_HEIGHT = 32;')
declarations.append('inline bool buttonHintsShown() { return !SETTINGS.globalStatusBarHidden(); }')
declarations.append('inline bool statusOnTop() { return false; }')
definitions = []
for name in ('smallFooterSymbolsTopY', 'compactFooterTips', 'tipY', 'x3ScreenLayoutEnabled'):
    if f'tenorchrome::{name}(' not in chrome:
        continue
    definitions.append(function(chrome, f'tenorchrome::{name}('))
for name in ('std::vector<std::string> tipLines(', 'int tipLineY('):
    definitions.append(function(chrome, name))
for name in ('tipTopY', 'tipLineCount', 'tipHeight', 'uglyTipTop', 'drawTip', 'x3ScreenLayout',
             'drawTipInRect', 'drawInfoInRect'):
    definitions.append(function(chrome, f'tenorchrome::{name}('))
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
    definitions.append(function(chrome, 'bool inPill('))
    for name in ('drawBarIcon', 'drawPillRing'):
        declarations.append(re.search(r'void ' + name + r'\([^;]+;', header).group())
        definitions.append(function(chrome, f'tenorchrome::{name}('))
    declarations.append(function(header, 'struct PathPillLayout') + ';')
    for name in ('layoutPathPill', 'drawPillLabel', 'drawPathPill'):
        if name == 'drawPillLabel' and 'void drawPillLabel(' not in header:
            continue
        declarations.append(re.search(r'(?:PathPillLayout|void) ' + name + r'\([^;]+;', header).group())
        definitions.append(function(chrome, f'tenorchrome::{name}('))
    declarations.append('constexpr int BAR_TAB_INSET = 6;')
    fixture = fixture.replace('  int screenHeight = 800;',
                              '  int screenHeight = 800;\n'
                              '  mutable std::array<int, 4> clip{0, 0, 528, 800};\n'
                              '  auto getClipRect() const { return clip; }\n'
                              '  mutable std::vector<std::array<int, 4>> placedBars;\n'
                              '  void setClipRect(int x, int y, int w, int h) const { clip = {x, y, w, h}; '
                              'if (h <= 72 && w == getScreenWidth()) placedBars.push_back(clip); }')
    fixture = fixture.replace('struct Metrics { int buttonHintsHeight; };',
                              'struct Metrics { int buttonHintsHeight; int contentSidePadding = 24; '
                              'int progressBarHeight = 12; int topPadding = 16; int headerHeight = 40; };')
    fixture = fixture.replace('int clockFormat = 0;', 'int clockFormat = 0; int readerInkWeight = 3;')
    fixture = fixture.replace('  int getScreenHeight() const',
                              '  void displayBuffer() const {}\n'
                              '  void clearScreen() const { paintedText.clear(); ys.clear(); attemptedInkBottom = 0; '
                              'std::fill(pixels.begin(), pixels.end(), 0); }\n'
                              '  int getScreenHeight() const')
    fixture = ('#include <array>\n#include "components/icons/tenorHomeTabIcons.h"\n'
               '#include "components/icons/tenorTextTabIcons.h"\n') + fixture
fixture = '#include "components/UIScale.h"\n#include "shells/ugly/UglyIcons.h"\n' + fixture
fixture = fixture.replace('// CHROME_DECLARATIONS', '\n'.join(declarations))
icon_boundary = '''namespace shell { inline bool isUgly() { return false; } }
namespace ugly {
inline uint32_t iconFrameSeed() { return 0; }
inline bool iconAsset(const GfxRenderer&, const uint8_t*, icons::Rect, uint32_t) { return false; }
void icon(const GfxRenderer&, icons::Id, icons::Rect, uint32_t, bool = true);
}
'''
fixture = fixture.replace('// PRODUCTION_FUNCTIONS', icon_boundary + '\n'.join(definitions))
if args.tip_only:
    fixture = fixture.replace('struct GfxRenderer {', '''struct GfxRenderer {
      mutable std::array<int, 4> clip = {0, 0, 528, 800};
      std::array<int, 4> getClipRect() const { return clip; }
      void setClipRect(int left, int top, int width, int height) const { clip = {left, top, width, height}; }
    ''')
fixture = '#include <array>\n#include <climits>\n' + fixture
fixture = fixture.replace('void drawPixel(int x, int y, bool black)', 'void drawPixel(int x, int y, bool black = true)')
fixture = fixture.replace('if (black) { attemptedInkTop',
                              'if (x < clip[0] || y < clip[1] || x >= clip[0] + clip[2] || y >= clip[1] + clip[3]) return;\n'
                              '    if (black) { attemptedInkTop')
if args.ugly_contract:
    fixture = fixture.replace('void drawLine(int, int y, int, int endY)',
                              'void drawLine(int, int y, int, int endY, bool = true)')
    start = fixture.index('namespace ugly {')
    end = fixture.index('\n}', start) + 2
    fixture = fixture[:start] + Path(__file__).with_name('UglyInkHarness.h').read_text() + fixture[end:]
    status_stub = ('void' if args.ugly_status_contract else 'inline void') + ' statusBar(const GfxRenderer&, const char*, const char*, const char*, const char*)'
    fixture = fixture.replace('enum class Size { S22 };', 'enum class Size { S22 };\n' + status_stub + (';' if args.ugly_status_contract else ' {}'))
    ink = read_source('src/shells/ugly/UglyInk.cpp')
    fixture += '\nnamespace ugly {\n'
    for signature in ('void dot(', 'void stroke(const GfxRenderer& r, int x0, int y0, const int x1',
                      'void penStroke(', 'void penTick(', 'int sixteenths(', 'void mark(', 'void circle('):
        if signature in ink:
            extracted = function(ink, signature)
            if signature == 'void mark(':
                extracted = extracted.replace('{', '{\nif (traceStatus) statusMarks.push_back({static_cast<int>(m), cx, cy});', 1)
            fixture += extracted + '\n'
    fixture += function(read_source('src/shells/ugly/UglyIconInk.cpp'), 'void icon(') + '\n'
    fixture += '}\n'
    fixture += '\nnamespace ugly {\n' + function(ink, 'void pageHints(') + '\n}\n'
    if 'void pathHints(' in ink:
        fixture += '\nnamespace ugly {\n' + function(ink, 'void pathHints(').replace('const bool wholeBar)', 'const bool wholeBar = true)') + '\n}\n'
    else:
        fixture += '\nnamespace ugly { void pathHints(const GfxRenderer&, const char*, const char*) {} }\n'
    fixture += '''
      struct NotebookLayoutHarness {
        GfxRenderer renderer;
        const char* tip() const { return "Giu OK: Ghim"; }
        int firstBaseline() const { return 210; }
        int rowsPerPage(int first = -1, int bottom = -1) const;
      };
    '''
    notebook_source = read_source('src/shells/ugly/UglyNotebook.cpp')
    fixture += 'constexpr int ROW_HEIGHT = 52;\n'
    notebook_rows = function(notebook_source, 'int Notebook::rowsPerPage(').replace('Notebook::', 'NotebookLayoutHarness::')
    if 'x3ScreenLayout(renderer, tenorchrome::tipHeight(renderer, tip())).bodyRect' in notebook_source:
        fixture += notebook_rows
    else:
        fixture += notebook_rows.replace('rowsPerPage() const', 'rowsPerPage(int first, int) const').replace(
            'firstBaseline()', '(first >= 0 ? first : firstBaseline())')
    notebook = read_source('src/shells/ugly/UglyNotebook.cpp')
    fixture += '''
      int id(int page) { return page; }
      int SUBTITLES[] = {0};
      struct { const char* get(int) const { return "open-page jab"; } } I18N;
      struct JabHarness { std::string jab; int page = 0; const char* subtitle() const; };
    '''
    fixture += function(notebook, 'const char* Notebook::subtitle()').replace('Notebook::', 'JabHarness::')
    fixture += '''
      const int STR_UGLY_BLE_TIP = 0;
      const char* tr(int) { return "Hold OK: Bind"; }
      struct UiListActivity { void drawFooter() {} };
      struct BlePageTurnerActivity : UiListActivity {
        GfxRenderer renderer;
        struct InfoBoundary {
          void draw(const GfxRenderer&, tenorchrome::X3ScreenRect, std::initializer_list<const char*>) { assert(false); }
        } x3Info;
        std::string bindNotice_, statusText_, statusValue_;
        bool statusInTip_ = false;
        const char* uglyTip() const;
        void drawFooter();
      };
    '''
    ble = read_source('src/activities/settings/BlePageTurnerActivity.cpp')
    for signature in ('const char* BlePageTurnerActivity::uglyTip()', 'void BlePageTurnerActivity::drawFooter()'):
        fixture += function(ble, signature) + '\n'
    if args.ugly_status_contract:
        fixture += r"""
namespace inlineSymbols {
enum class Shape { Select };
void drawShape(const GfxRenderer& r, Shape, int x, int y, int size, bool) {
  buttonSymbols::drawLabel(r, "\xEE\x84\x80", x, y, size);
  if (ugly::traceStatus) ugly::statusMarks.push_back({static_cast<int>(ugly::Mark::Tick), x, y});
}
}
"""
        fixture = fixture.replace('int clockFormat = 0;', '''int clockFormat = 0, clockShowInHeader = 0, statusBarClock = 1;
          bool percent = true;
          bool batteryPercentShown(bool) const { return percent; }
        ''')
        fixture = fixture.replace('inline bool hasValidTime() { return false; }',
                                  'inline bool valid = true; inline bool hasValidTime() { return valid; }')
        fixture = fixture.replace('struct { bool formatTime(char*, int, bool) const { return false; } } halClock;', '''
          struct {
            std::string twelve = "12:59 PM";
            bool formatTime(char* out, size_t count, bool twelve) const {
              std::snprintf(out, count, "%s", twelve ? this->twelve.c_str() : "09:28"); return true;
            }
            bool getDateTime(uint16_t& year, uint8_t& month, uint8_t& day, uint8_t&, uint8_t&) const {
              year = 2026; month = 10; day = 9; return true;
            }
          } halClock;
          struct { int level = 100; int getDisplayedBatteryPercentage() const { return level; } } powerManager;
          struct CrossPointSettings { enum { CLOCK_HEADER_HIDE, CLOCK_HEADER_TIME, CLOCK_HEADER_TIME_DATE };
            enum { STATUS_BAR_CLOCK_RIGHT = 1, STATUS_BAR_CLOCK_LEFT = 2 }; };
          struct MappedInputManager {
            bool reverse = false;
            struct Labels { const char *btn1, *btn2, *btn3, *btn4; };
            Labels mapLabels(const char* back, const char* confirm, const char* up, const char* down) const {
              return reverse ? Labels{down, up, confirm, back} : Labels{back, confirm, up, down};
            }
          };
        ''')
        fixture = fixture.replace('int labelId(const char*) { return -1; }', '''
          int labelId(const char* word) {
            const auto* bytes = reinterpret_cast<const unsigned char*>(word);
            if (std::strlen(word) == 3 && bytes[0] == 0xEE && bytes[1] == 0x84 && bytes[2] >= 0x80 && bytes[2] <= 0x85)
              return bytes[2] - 0x80;
            for (int index = 0; index < 6; ++index)
              if (std::strcmp(word, std::vector<const char*>{"c", "b", "u", "d", "l", "r"}[index]) == 0) return index;
            return -1;
          }
        ''')
        status_header = function(read_source('src/shells/ugly/UglyInk.h'), 'struct Hints')
        fixture += '\nnamespace ugly {\n' + status_header + ';\n'
        fixture += '''
          const EpdFont FONT22(&ugly_22);
          struct Letter { const EpdGlyph* glyph; logic::Warp warp; };
          void drawWarped(const GfxRenderer& renderer, Size, const Letter& letter, int left, int baseline, bool black) {
            const auto* glyph = letter.glyph;
            logic::warpGlyph(FONT22.data->bitmap + glyph->dataOffset, glyph->width, glyph->height, glyph->left,
                             glyph->top, glyph->advanceX, letter.warp, logic::NO_CLIP,
                             [&](int px, int py) { renderer.drawPixel(left + px, baseline + py, black); });
          }
        '''
        for signature in ('void polyline(', 'void battery(', 'bool clockText('):
            fixture += function(ink, signature) + '\n'
        for match in re.finditer(r'\nvoid statusBar\(', ink):
            fixture += function(ink[match.start():], 'void statusBar(') + '\n'
        fixture += '}\n'
        desk = read_source('src/shells/ugly/UglyDesk.cpp')
        places = function(desk, 'struct Place') + ';\n'
        places += re.search(r'constexpr Place PLACES\[6\] = .*?;', desk, re.S).group()
        places = re.sub(r'StrId::[A-Z_]+', '0', places).replace('StrId label;', 'int label;')
        fixture += '\nnamespace ugly { constexpr int READING = 2;\n' + places + '\n'
        if 'Box deskLabelBox(' in desk:
            fixture += function(desk, 'Box deskLabelBox(') + '\n'
        else:
            render = desk[desk.index('#else\nvoid Desk::render'):]
            labels = render[render.index('const int lw = width'):render.index('text(renderer, Size::S22, lx, base, label);')]
            fixture += 'Box deskLabelBox(const GfxRenderer& renderer, Box object, const char* label, int) {\nstruct { Box box; } p{object};\n' + labels
            fixture += '\nreturn {lx, base - ascent(Size::S22), lx + lw, base + 8}; }\n'
        fixture += '}\n'
        for name, file in (('Diary', 'UglyDiary'), ('Desk', 'UglyDesk'), ('Notebook', 'UglyNotebook'), ('QuestionSheet', 'UglyQuestionSheet')):
            activity = read_source(f'src/shells/ugly/{file}.cpp')
            calls = re.findall(r'(?<![:\w])statusBar\([^;]+;', activity)
            assert calls, file
            fixture += f'void paint{name}(GfxRenderer& renderer, const MappedInputManager& mappedInput) {{\n'
            fixture += 'const bool hasBook = true; const int count = 1; struct { int count = 1; } view_;\n'
            fixture += 'const auto& r = renderer; const auto& input = mappedInput; using ugly::statusBar;\n' + calls[-1] + '\n}\n'
        browser = read_source('src/activities/home/FileBrowserActivity.cpp')
        browser_call = re.search(r'GUI\.drawButtonHints\([^;]+;', browser).group()
        fixture += '''void paintFileBrowser(GfxRenderer& renderer, const MappedInputManager& input) {
          const auto labels = input.mapLabels("b", "c", "u", "d"); TenorTheme theme;
        ''' + browser_call.replace('GUI.', 'theme.') + '\n}\n'
        fixture += Path(__file__).with_name('UglyStatusBarTest.cpp').read_text()
    else:
        fixture += Path(__file__).with_name('UglyScreenLayoutTest.cpp').read_text()
elif args.shell == 'ugly':
    fixture += '''int main() {
      SETTINGS.tipsEnabled = false;
      assert(tenorchrome::x3ScreenLayoutEnabled());
      assert(!tenorchrome::tipShown("regular tip"));
      SETTINGS.tipsEnabled = true;
      assert(tenorchrome::tipShown("regular tip"));
      std::puts("GREEN: ugly uses the shared layout and tips switch");
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
    harness = Path(__file__).with_name('HomeFooterHarness.h').read_text()
    harness = harness.replace('namespace shell { bool isUgly() { return false; } }', '')
    if args.settings_language:
        import yaml
        translations = yaml.safe_load((ROOT / 'lib/I18n/translations' / (args.settings_language + '.yaml')).read_text())
        enum = function(harness, 'enum class StrId')
        home_keys = {'STR_FILE_NOT_FOUND', 'STR_STATS_RESET_FAILED', 'STR_STATS_RESET_PENDING',
                     'STR_HOME_FAVORITES_HINT', 'STR_FOLDER_HOLD'}
        original = function(harness, 'const char* tr(')
        cases = []
        for key in re.findall(r'\bSTR_[A-Z0-9_]+\b', enum):
            if key in home_keys:
                cases.append(re.search(r'case StrId::' + key + r': return [^;]+;', original).group())
            else:
                cases.append('case StrId::' + key + ': return ' + json.dumps(translations[key], ensure_ascii=False) + ';')
        translated = 'const char* tr(StrId id) { switch (id) {\n' + '\n'.join(cases) + '\ndefault: return "key";\n} }'
        harness = harness.replace(original, translated)
    if args.phase2:
        ids = sorted(set(re.findall(r'\bSTR_[A-Z0-9_]+\b', function(harness, 'enum class StrId'))))
        languages = []
        for language in ('vietnamese', 'english', 'chinese'):
            translated = {}
            for line in read_source(f'lib/I18n/translations/{language}.yaml').splitlines():
                name, separator, value = line.partition(':')
                if separator and name in ids:
                    translated[name] = json.JSONDecoder().raw_decode(value.lstrip())[0]
            assert set(ids) <= translated.keys(), f'{language}: missing translations'
            languages.append(translated)
        lookup = 'int phase2Language = -1;\nconst char* phase2Translation(StrId id) {\n switch (id) {\n'
        for name in ids:
            values = ', '.join(json.dumps(language.get(name, name), ensure_ascii=False) for language in languages)
            lookup += f'case StrId::{name}: {{ static const char* values[] = {{{values}}}; return values[phase2Language]; }}\n'
        lookup += '} return ""; }\n'
        lookup = '\n'.join(f'constexpr auto {name} = StrId::{name};' for name in ids
                           if f'constexpr auto {name} =' not in harness and f'FIXTURE_STRING({name})' not in harness) + '\n' + lookup
        harness = harness.replace('// PHASE2_TRANSLATIONS', lookup)
        harness = harness.replace('// PHASE2_LOOKUP',
            'if (phase2Language >= 0) return phase2Translation(id);')
    fixture += harness
    for name in ('drawFooter', 'homeTipText', 'missingFileText', 'homeInfoText', 'homeScreenLayout'):
        fixture += '\n' + function(home_source, f'HomeActivity::{name}(')
    fixture = extend_path(fixture, chrome, function, True, ROOT)
    adapter_path = 'src/activities/settings/X3SettingsLayout.h'
    if (ROOT / adapter_path).exists():
        adapter = (ROOT / adapter_path).read_text()
        for name in ('beginScreen', 'drawPath', 'infoRect', 'nextInfoLine', 'infoPageCount', 'drawInfoPage'):
            fixture += '\n' + function(adapter, f'{name}(')
        fixture += ('\nnamespace x3settings { using ::beginScreen; using ::drawPath; '
                    'using ::drawInfoPage; using ::infoPageCount; }\n')
        fixture += '\n' + function(read_source('src/activities/UiListActivity.cpp'), 'void UiListActivity::reserveFavoriteHint(')
        fixture += '\nint settingsMatrixCases = 0;\n'
        for name in SETTINGS_CLASSES:
            if name in ('ClearCacheActivity', 'TextSettingsActivity'):
                continue
            folder = 'home' if name.startswith('BookStats') else 'settings'
            body = function(read_source(f'src/activities/{folder}/{name}.cpp'), f'void {name}::buildScreen(')
            marker = 'if (x3settings::beginScreen('
            if marker in body:
                prefix = body[body.index(marker):]
                prefix = function(prefix, marker)
                prefix = prefix.replace('UiListActivity::favoriteHintText()', 'page.UiListActivity::favoriteHintText()')
            else:
                prefix = 'screen.content = {0, 60, 528, 700};'
            class_header = read_source(f'src/activities/{folder}/{name}.h')
            supported = 'supportsFavorites()' in class_header
            fixture += f'\nvoid prepare_{name}(SettingsScreenBoundary& page, UiScreen& screen) {{\n'
            fixture += 'auto& renderer = page.renderer; auto& tabBandDrawn = page.tabBandDrawn;\n'
            fixture += 'auto& saveFailed = page.saveFailed; auto& saveFailed_ = page.saveFailed_;\n'
            fixture += 'bool showSaveError = page.showSaveError, favoriteSaveFailed = page.favoriteSaveFailed;\n'
            fixture += 'auto favoriteHintText = [&] { return page.favoriteHintText(); };\n'
            fixture += prefix + '\npage.reserveFavoriteHint(screen);\n}\n'
            fixture += f'\nvoid check_{name}(GfxRenderer& renderer) {{\n'
            fixture += 'for (int state = 0; state < 4; ++state) {\nSettingsScreenBoundary page(renderer);\n'
            fixture += f'page.supported = {str(supported).lower()};\n'
            fixture += 'page.saveFailed.value = page.saveFailed_.value = page.showSaveError = state == 1 || state == 3;\n'
            fixture += 'page.favoriteSaveFailed = state >= 2; UiScreen screen;\n'
            fixture += f'prepare_{name}(page, screen);\n'
            fixture += 'const char* tip = state >= 2 ? nullptr : page.favoriteHintText();\n'
            if name == 'OpdsSettingsActivity':
                fixture += 'tip = tr(STR_CALIBRE_URL_HINT);\n'
            fixture += 'const auto layout = tenorchrome::x3ScreenLayout(renderer, tip ? tenorchrome::tipHeight(renderer, tip) : 0);\n'
            fixture += f'assert(screen.body().y == 16 && "{name}: no header");\n'
            fixture += f'assert(screen.body().bottom() <= layout.bodyRect.bottom() && "{name}: no tip/bar overlap");\n'
            fixture += 'renderer.placedBars.clear(); x3settings::drawPath(renderer, "Settings");\n'
            fixture += 'assert(!renderer.placedBars.empty()); const auto bar = renderer.placedBars.back();\n'
            fixture += f'assert(bar[0] == layout.barRect.x && bar[1] == layout.barRect.y && bar[2] == layout.barRect.width && bar[3] == layout.barRect.height && "{name}: exact Home barRect");\n'
            if name in ('SettingsActivity', 'StatusBarSettingsActivity'):
                fixture += 'if (state == 3) {\n'
                fixture += 'const int errorHeight = tenorchrome::tipHeight(renderer, tr(STR_HABIT_SAVE_FAILED));\n'
                fixture += 'const int menuHeight = tenorchrome::tipHeight(renderer, tr(STR_MENU_SAVE_FAILED));\n'
                fixture += f'assert(screen.body().bottom() <= layout.bodyRect.bottom() - errorHeight - menuHeight - 16 && "{name}: both errors need separate body slots");\n}}\n'
            fixture += 'const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);\n'
            fixture += 'const int pageRows = std::max(1, screen.body().height / (lineHeight + 8));\n'
            fixture += 'int reachable = 0; for (int first = 0; first < 53; first += pageRows)\n'
            fixture += 'for (int row = first; row < std::min(53, first + pageRows); ++row) {\n'
            fixture += 'assert(screen.body().y + (row - first) * (lineHeight + 8) + lineHeight <= screen.body().bottom()); ++reachable; }\n'
            fixture += 'assert(reachable == 53); ++settingsMatrixCases;\n}\n}\n'
        fixture += '\nvoid checkSettingsScreenMatrix(GfxRenderer& renderer) {\n'
        for name in SETTINGS_CLASSES:
            if name not in ('ClearCacheActivity', 'TextSettingsActivity'):
                fixture += f'check_{name}(renderer);\n'
        fixture += '}\n'
        text_source = read_source('src/activities/settings/TextSettingsActivity.cpp')
        if 'TextSettingsActivity::displayIndex(' in text_source:
            for name in ('displayIndex', 'tabForIndex'):
                fixture += '\n' + function(text_source, f'TextSettingsActivity::{name}(').replace(
                    'TextSettingsActivity::', 'TextTabsBoundary::')
            fixture += '\n#define X3_TEXT_TABS_AVAILABLE 1\n'
            for name in ('displayIndex', 'tabForIndex', 'tabIcon', 'listCount', 'favoriteKey', 'rebuildRowItems',
                         'refreshFavoriteRows', 'favoriteHintText', 'x3TipText', 'x3InfoText', 'focusedRowHasNoPreview',
                         'onTabAction', 'focusFavorite', 'activateRow'):
                fixture += '\n' + function(text_source, f'TextSettingsActivity::{name}(')
            geometry = function(text_source, 'void TextSettingsActivity::updatePreviewGeometry(')
            fixture += '\n' + geometry[:geometry.index('  const int fontId =')] + '\n}\n'
            build = function(text_source, 'void TextSettingsActivity::buildScreen(')
            fixture += '\n' + build[:build.index('  // rowItems_')].replace('::buildScreen(', '::prepareBody(') + '\n}\n'
            render = function(text_source, 'void TextSettingsActivity::render(')
            footer = function(render, '  if (tenorchrome::x3ScreenLayoutEnabled()) {\n    const char* tip =')
            fixture += '\nvoid TextSettingsActivity::drawX3Info() {\n' + footer + '\n}\n'
        for name, functions in (('ClearCacheActivity', ('x3InfoText', 'renderX3')),
                                ('FontDownloadActivity', ('x3InfoText', 'drawChrome', 'renderX3State'))):
            state_source = read_source(f'src/activities/settings/{name}.cpp')
            if f'{name}::{functions[-1]}(' in state_source:
                for method in functions:
                    fixture += '\n' + function(state_source, f'{name}::{method}(')
                fixture += f'\n#define X3_{name.upper()}_AVAILABLE 1\n'
        fixture += '\n#define X3_SETTINGS_ADAPTER_AVAILABLE 1\n'
    if args.phase2:
        adapters = read_source('src/activities/home/X3FileReaderChrome.h')
        adapters = adapters[adapters.index('template <class Visitor'):adapters.rfind('}')]
        reader_icons = function(read_source('src/activities/reader/ReaderMenuLayout.h'), 'enum class Tab') + ';\n'
        if 'x3TabIcon(' in read_source('src/activities/reader/EpubReaderMenuActivity.cpp'):
            reader_icons += function(read_source('src/components/icons/X3ReaderTabIcons.h'),
                                     'inline const freeink::Icon& x3TabIcon(')
        fixture += '\n' + Path(__file__).with_name('Phase2ChromeHarness.h').read_text().replace(
            '// PHASE2_PATH_FUNCTIONS', adapters).replace('// PHASE2_READER_ICONS', reader_icons)
        extra_methods = {
            'FileBrowserActivity': ('screenTip',),
            'QuoteTrimActivity': ('screenTip', 'screenInfo', 'areaTop', 'linesPerArea'),
            'EpubReaderMenuActivity': ('progressText', 'tabIcon', 'infoOverBar'),
            'QuoteDetailActivity': ('metrics', 'metaStep', 'positionLabel', 'visitX3Meta', 'layoutX3Pages'),
            'QuotesActivity': ('metrics', 'footer'),
        }
        for directory, classes in PHASE2.items():
            for activity in classes:
                source = read_source(f'src/activities/{directory}/{activity}.cpp')
                fixture += '\n' + function(source, f'{activity}::screenLayout(')
                for name in (*extra_methods.get(activity, ()), 'drawX3Content'):
                    fixture += '\n' + function(source, f'{activity}::{name}(')
                if activity == 'EpubReaderMenuActivity':
                    declaration = function(read_source(f'src/activities/{directory}/{activity}.h'), 'int tabCount()')
                    fixture += '\n' + declaration.replace('int tabCount()', 'int EpubReaderMenuActivity::tabCount()').replace(' override', '')
        fixture += Path(__file__).with_name('Phase2ChromeTest.cpp').read_text()
    if not args.footer_spacing:
        fixture += Path(__file__).with_name('PathGeometryTest.h').read_text()
    fixture += Path(__file__).with_name('FooterSpacingTest.cpp' if args.footer_spacing else 'X3ScreenLayoutTest.cpp').read_text()
args.work.mkdir(parents=True, exist_ok=True)
unit = args.work / 'layout.cpp'
unit.write_text('#include <cstdlib>\n#include "BitmapFixture.h"\n' + fixture)
program = args.work / 'layout'
units = [str(ROOT / 'src/components/QuoteDetailLayout.cpp'), str(ROOT / 'src/components/QuoteListLayout.cpp')] if args.phase2 else []
subprocess.run(['c++', '-std=c++17', '-O0', '-UNDEBUG',
                *bitmap_build_args(ROOT, args.work), '-I', str(ROOT / 'src'),
                '-I', str(ROOT / 'lib/EpdFont'), '-I', str(ROOT / 'lib/Utf8'),
                '-I', str(ROOT / 'lib/GfxRenderer'), str(unit),
                '-I', str(ROOT / 'freeink-sdk/libs/assets/Icons/include'),
                str(ROOT / 'lib/Utf8/Utf8.cpp'), str(ROOT / 'lib/EpdFont/EpdFont.cpp'),
                str(ROOT / 'lib/EpdFont/EpdFontFamily.cpp'),
                *units, '-o', str(program)], check=True)
runtime = subprocess.run([str(program)])
if args.tip_only or args.shell != 'buttons' or args.ugly_contract:
    raise SystemExit(runtime.returncode)

tabs = (ROOT / 'src/activities/UiTabListActivity.cpp').read_text()
home = (ROOT / 'src/activities/home/HomeActivity.cpp').read_text()
assert 'x3ScreenLayout(renderer)' in function(tabs, 'void UiTabListActivity::buildTabBar(')
assert 'homeScreenLayout()' in function(home, 'void HomeActivity::buildScreen(')
assert 'homeScreenLayout()' in function(home, 'void HomeActivity::drawRecentCard(')
footer = function(home, 'void HomeActivity::drawFooter(')
assert 'drawInfoInRect' in footer and 'missingFileText()' in footer and 'statsResetTip' in footer
assert 'drawTipInRect' in footer
print('GREEN: shared tab placement and Home body/tip/info bindings')

failed_classes = []
for name in SETTINGS_CLASSES:
    folder = 'home' if name.startswith('BookStats') else 'settings'
    source = read_source(f'src/activities/{folder}/{name}.cpp')
    class_header = read_source(f'src/activities/{folder}/{name}.h')
    path_bound = 'x3settings::drawPath(' in source + class_header
    body_bound = 'x3settings::beginScreen(' in source
    if name == 'ClearCacheActivity':
        body_bound = 'x3settings::drawInfoPage(' in source and 'x3ScreenLayout(renderer)' in source
    if name == 'TextSettingsActivity':
        body_bound = ('x3ScreenLayout(renderer' in source and 'drawInfoInRect(' in source and
                      'tabIcon(' in source and 'Tab::Favorites' in source)
        path_bound = 'buildTabBar(screen)' in source
    if not body_bound or not path_bound:
        failed_classes.append(name)
    print(f'{"GREEN" if body_bound and path_bound else "RED"}: {name}: '
          f'18 bindings (3 sizes x tips On/Off x status Small/Large/Off)')
if failed_classes:
    raise SystemExit('RED: missing settings body/bar bindings: ' + ', '.join(failed_classes))
print('GREEN: 288 settings body/bar bindings')
if args.phase2:
    for directory, classes in PHASE2.items():
        for activity in classes:
            source = read_source(f'src/activities/{directory}/{activity}.cpp')
            header = read_source(f'src/activities/{directory}/{activity}.h')
            assert re.search(r'x3ScreenLayout\(\s*renderer', function(source, f'{activity}::screenLayout(')), activity
            assert source.count('screenLayout()') >= 3, activity
            assert 'drawX3Content();' in source, activity
            if 'buildScreen(UiScreen&' in header:
                body = function(source, f'{activity}::buildScreen(')
                assert 'setContentMarginFromScreen' in body and 'screenLayout()' in body, activity
                assert 'ownsX3Chrome() const override { return true; }' in header, activity
    base_list = read_source('src/activities/UiListActivity.cpp')
    for name in ('reserveFixedMenuContent', 'reserveFavoriteHint'):
        assert 'x3ScreenLayoutEnabled() && ownsX3Chrome()' in function(base_list, f'UiListActivity::{name}(')
    menu = read_source('src/activities/reader/EpubReaderMenuActivity.cpp')
    assert 'x3ScreenLayoutEnabled()' in menu[menu.index('readermenu::buildItems'):menu.index('napGhim();')]
    assert 'readermenu::separateTextTab(menuItems)' in menu
    assert 'library/LibraryListActivity.h' not in read_source('src/activities/ActivityManager.cpp')
    assert 'goToLibrary' not in read_source('src/activities/ActivityManager.h')
    enum = read_source('src/activities/ActivityManager.h').split('enum class HomeMenuItem {')[1].split('};')[0]
    members = re.findall(r'^\s*([A-Z_]+)\s*(?:,|//)', enum, re.M)
    assert members[9] == 'LIBRARY' and members[10] == 'DESK'
    assert not (ROOT / 'src/activities/library/LibraryListActivity.cpp').exists()
    assert not (ROOT / 'test/library_nav_capture/run.py').exists()
    print('GREEN: 8 caller bindings, X3-only fifth tab, inherited reservation bypass, Library removal and RTC slot')

raise SystemExit(runtime.returncode)
