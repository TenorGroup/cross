import json
import re
import subprocess
from pathlib import Path


def extend(root, read_source, function, fixture, names):
    sources = {name: read_source(str(next(root.glob(f'src/activities/**/{name}Activity.cpp')).relative_to(root)))
               for name in names}
    activity_states = {}
    for name in names:
        header = read_source(str(next(root.glob(f'src/activities/**/{name}Activity.h')).relative_to(root)))
        enums = re.findall(r'enum(?:\s+class)?\s+(?:State|\w+State)\s*\{([^}]+)\}', header)
        activity_states[name] = sorted(set(re.findall(r'\b[A-Z][A-Z_]+\b', '\n'.join(enums))))
    if any('x3ScreenLayout(renderer' not in text for text in sources.values()):
        adapter = '\n'.join(line for line in (root / 'src/activities/X3ActivityLayout.h').read_text().splitlines()
                            if not line.startswith('#'))
        stub = 'namespace tenorchrome { std::vector<X3ScreenRect> phaseRings;\n' \
            'void drawPillRing(const GfxRenderer&, int x, int y, int width, int height, int, bool) {\n' \
            'phaseRings.push_back({x, y, width, height}); } }\n'
        return '#include <initializer_list>\n' + fixture + stub + adapter, '', ''
    snippets = {}
    margin_snippets = {}
    for name, source in sources.items():
        if name == 'KeyboardEntry':
            snippets[name] = function(source, 'std::string KeyboardEntryActivity::x3TipText(')
            rect = function(source, 'fui::Rect KeyboardEntryActivity::keyboardRect(')
            branch = function(rect, '  if (tenorchrome::x3ScreenLayoutEnabled())')
            margin_snippets[name] = 'const auto& metrics = UITheme::getInstance().getMetrics();\n' + \
                'const int pageWidth = renderer.getScreenWidth();\n' + branch
        else:
            method = 'drawChrome' if name in ('ReadingHistory', 'NetworkModeSelection') else \
                     'drawFooter' if name in ('ReadingHabits', 'BlePageTurner') else 'render'
            render = function(source, f'void {name}Activity::{method}(')
            snippets[name] = function(render, '  if (tenorchrome::x3ScreenLayoutEnabled()')
            if name in ('ReadingHistory', 'ReadingHabits', 'BlePageTurner', 'NetworkModeSelection'):
                screen = function(source, f'void {name}Activity::buildScreen(')
                branch = '  if (tenorchrome::x3ScreenLayoutEnabled())'
                if branch not in screen:
                    branch = '  } else if (tenorchrome::x3ScreenLayoutEnabled())'
                margin_snippets[name] = function(screen, branch).replace('} else if', 'if', 1)
            if name in ('WifiSelection', 'OpdsBookBrowser'):
                method = 'screenHeader' if name == 'OpdsBookBrowser' else 'buildListScreen'
                screen = function(source, f'void {name}Activity::{method}(')
                branch = '  if (tenorchrome::x3ScreenLayoutEnabled())'
                if branch not in screen:
                    branch = '  } else if (tenorchrome::x3ScreenLayoutEnabled())'
                margin_snippets[name] = function(screen, branch).replace('} else if', 'if', 1)
    descriptions = re.search(r'constexpr StrId descriptions\[\]\s*=\s*\{[^}]+\};', sources['ReadingHabits']).group(0)
    habit_count = len(re.findall(r'STR_HABIT_\w+_DESC', descriptions))
    habit_footer = function(sources['ReadingHabits'], 'void ReadingHabitsActivity::drawFooter(')
    habit_status = '\n'.join(re.search(pattern, habit_footer).group(0) for pattern in
                            (r'const bool matched =[^;]+;', r'const char\* status =[^;]+;'))
    snippets['ReadingHabits'] = descriptions + '\n' + habit_status + '\n' + snippets['ReadingHabits']
    # The long-term screen's information line: the page's dates under the grid, the statement as the screen has it.
    history_grid = function(sources['ReadingHistory'], 'void ReadingHistoryActivity::drawGrid(')
    history_info = history_grid[history_grid.index('  tenorchrome::drawInfoInRect(renderer, range,'):]
    history_info = history_info[:history_info.index(';') + 1]
    history_info = history_info.replace('lichdai::SO_TUAN', '4')
    remap_methods = '\n'.join(function(sources['ButtonRemap'], f'const char* ButtonRemapActivity::{method}(')
                              .replace('ButtonRemapActivity::', '') for method in ('getRoleName', 'getHardwareName'))
    used = '\n'.join(snippets.values()) + '\n'.join(margin_snippets.values()) + history_info + remap_methods + ' STR_ALREADY_ASSIGNED'
    keys = sorted(set(re.findall(r'\bSTR_[A-Z0-9_]+\b', used)))
    existing = set(re.findall(r'\bSTR_[A-Z0-9_]+\b', fixture))
    extra = [key for key in keys if key not in existing]
    fixture = fixture.replace('enum class StrId {', 'enum class StrId {\n' + ', '.join(extra) + ',')
    fixture = fixture.replace('const char* tr(StrId id) {', '''std::map<StrId, std::string> phaseWords;
const char* tr(StrId id) {
  const auto found = phaseWords.find(id);
  if (found != phaseWords.end()) return found->second.c_str();''')
    fixture += '\n' + '\n'.join(f'constexpr auto {key} = StrId::{key};' for key in extra) + '\n'
    states = sorted(set(re.findall(r'\b(?:State|BrowserState|WifiSelectionState|CalibreConnectState|WebServerActivityState)::([A-Z_]+)', used)) |
                    set(re.findall(r'\bstate\s*==\s*([A-Z_]+)', used)) |
                    {state for values in activity_states.values() for state in values})
    bounds = Path(__file__).with_name('PhaseTwoHarness.h').read_text()
    bounds = bounds.replace('// PHASE_TWO_STATES', 'enum PhaseState {\n' + ', '.join(states) + '\n};')
    adapter = read_source('src/activities/X3ActivityLayout.h')
    adapter = '\n'.join(line for line in adapter.splitlines() if not line.startswith('#'))
    adapter = adapter.replace('  title = title ? title : "";', '  tenorchrome::phaseBars.push_back(rect);\n  title = title ? title : "";')
    bounds = bounds.replace('// PHASE_TWO_ADAPTER', adapter)
    popup = read_source('src/activities/X3OptionPopup.h')
    bounds = bounds.replace('// PHASE_TWO_POPUP', '\n'.join(
        line for line in popup.splitlines() if not line.startswith('#')))
    classes = []
    invocations = []
    for name, snippet in snippets.items():
        snippet = re.sub(r'\b(?:State|BrowserState|WifiSelectionState|CalibreConnectState|WebServerActivityState)::', '', snippet)
        if name == 'KeyboardEntry':
            classes.append('struct KeyboardEntryActivity : PhaseActivityBoundary {\n'
                           'using PhaseActivityBoundary::PhaseActivityBoundary;\n'
                           'std::string x3TipText() const;\n'
                           'fui::Rect rect() const {' + margin_snippets[name] + 'return {}; }\n};\n' + snippet)
            invocations.append('''{
              for (InputType type : {InputType::Text, InputType::Password, InputType::Url})
              for (bool cursor : {false, true}) for (bool symbols : {false, true})
              for (bool url : {false, true}) for (bool shifted : {false, true}) {
              target.paintedText.clear();
              target.attemptedInkBottom = 0;
              tenorchrome::phaseInk.clear();
              KeyboardEntryActivity page(target);
              page.inputType = type;
              page.cursorMode = cursor;
              page.symbols = symbols;
              page.urlPanel = url;
              page.shifted = shifted;
              const auto keys = page.rect();
              const auto tip = page.x3TipText();
              const auto layout = tenorchrome::x3ScreenLayout(target, tenorchrome::tipHeight(target, tip.c_str(), 8));
              check(keys.x == 8 && keys.width == target.getScreenWidth() - 16, "keyboard spans the screen but for an 8 px margin each side");
              check(keys.y >= layout.bodyRect.y && keys.y + keys.height == layout.bodyRect.bottom(), "keyboard stays in body");
              check((keys.height - 24) / 5 >= 26, "keyboard rows retain legible glyph space");
              tenorchrome::drawTipInRect(target, tip.c_str(), layout.tipRect, 8);
              check(target.attemptedInkBottom <= layout.barRect.y, "keyboard tips above bar");
              std::printf("KEYBOARD_CASE %d %d %d %d %d %d %d %d %d %d %d %d %d %d\\n", keys.x, keys.y, keys.width, keys.height,
                  tier, target.getLineHeight(UI_12_FONT_ID), target.getLineHeight(SMALL_FONT_ID), phaseLocale,
                  static_cast<int>(type), (keys.height - 24) / 5, symbols, shifted, url, cursor);
              }
              ++activityCases;
            }''')
            continue
        if name == 'ReadingHabits':
            snippet = snippet.replace('UiListActivity::drawFooter();', '')
        if name == 'BlePageTurner':
            snippet = snippet.replace('UiListActivity::drawFooter();', '')
        margins = re.sub(r'\b(?:State|BrowserState|WifiSelectionState|CalibreConnectState|WebServerActivityState)::', '', margin_snippets.get(name, ''))
        extra_methods = ''
        if name == 'ButtonRemap':
            extra_methods += remap_methods
        constants = '\n'.join('static ' + match for match in re.findall(
            r'constexpr int QR_CODE_(?:WIDTH|HEIGHT)\s*=\s*\d+;', sources[name]))
        if name == 'ReadingHistory':
            snippet = snippet.replace('ready', 'grid')
            margins = margins.replace('ready', 'grid')
            extra_methods = 'void drawPage() const {\nconst int line = renderer.getLineHeight(SMALL_FONT_ID), ' \
                'step = (renderer.getScreenWidth() - 48) / 7, top = gridTop() + line + 4;\n' \
                'const char* range = "31/08 - 21/09  1/4";\nconst auto rimGap = [] { return 0; };\n' + history_info + '\n}\n'
        if name in ('OtaUpdate', 'BlePageTurner'):
            extra_methods += function(sources[name], f'freeink::ui::Rect {name}Activity::x3PopupFrame(').replace(
                f'{name}Activity::', '')
            assert 'x3Popup.show(' in sources[name]
        if name == 'ReadingHabits':
            loop = function(sources[name], 'void ReadingHabitsActivity::loop(')
            extra_methods += 'void pageInput() {\n' + function(loop, '  if (tenorchrome::x3ScreenLayoutEnabled()') + '\n}\n'
        if name == 'OtaUpdate':
            loop = function(sources[name], 'void OtaUpdateActivity::loop(')
            prefix = loop[loop.index('{') + 1:loop.index('\n  if (state == WAITING_CONFIRMATION) {')]
            extra_methods += 'void pageInput() {\n' + prefix + '\n}\n'
        if name == 'BlePageTurner':
            entry = function(sources[name], f'void {name}Activity::render(')
            extra_methods += 'void renderEntry() {\n' + function(entry, '  if (tenorchrome::x3ScreenLayoutEnabled()') + '\nrenderX3();\n}\n'
            chrome = function(sources[name], 'void BlePageTurnerActivity::drawChrome(')
            extra_methods += 'void drawChrome() {\n' + function(chrome, '  if (tenorchrome::x3ScreenLayoutEnabled()') + '\n}\n'
            loop = function(sources[name], 'void BlePageTurnerActivity::loop(')
            extra_methods += 'void pageInput() {\n' + function(loop, '  if (tenorchrome::x3ScreenLayoutEnabled()') + '\n}\n'
            if 'void BlePageTurnerActivity::pollTilt(' in sources[name]:
                tilt = function(sources[name], 'void BlePageTurnerActivity::pollTilt(')
                extra_methods += tilt.replace('BlePageTurnerActivity::pollTilt', 'tiltInput').replace(
                    'UiListActivity::pollTilt();', '++listTilts;') + '\n'
            else:
                extra_methods += 'void tiltInput() { ++listTilts; }\n'
        classes.append(f'struct {name}Activity : PhaseActivityBoundary {{\n'
                       'using PhaseActivityBoundary::PhaseActivityBoundary;\n'
                       + constants + '\n' +
                       'void renderX3() {\n' + snippet + '\n}\n' +
                       ('void buildX3(UiScreen& screen) {\n' + margins + '\n}\n' if margins else '') + extra_methods + '};\n')
        states_for_class = sorted(set(activity_states[name]) | set(re.findall(r'\bstate\s*==\s*([A-Z_]+)', snippet)) |
                                  set(re.findall(r'\bstate\s*!=\s*([A-Z_]+)', snippet))) or ['0']
        first = '{' + ', '.join(states_for_class) + '}'
        mode_loop = 'for (const auto authMode : {Mode::AUTHENTICATE, Mode::SIGN_UP})' if name == 'KOReaderAuth' else ''
        setup = ''
        if name == 'ReadingHabits':
            mode_loop = f'for (int habit = 0; habit < {habit_count}; ++habit) ' \
                'for (bool saveFailed : {false, true}) for (bool statsReadable : {false, true}) ' \
                'for (bool earned : {false, true})'
            setup = 'page.selected = habit; page.failed = saveFailed; page.readable = statsReadable; page.awarded = earned ? 1 << habit : 0;'
        if name == 'CrossPointWebServer':
            mode_loop = 'for (bool accessPoint : {false, true}) for (int step : {0, 1})'
            setup = 'page.isApMode = accessPoint; page.x3Step = step;'
        if name == 'WifiSelection':
            mode_loop = 'for (bool emptyNetworks : {false, true})'
            setup = 'if (state == NETWORK_LIST && emptyNetworks) page.networks.clear();'
        if name == 'ReadingHistory':
            mode_loop = 'for (bool calendar : {false, true})'
            setup = 'page.grid = calendar;'
        invocation = f'''{{
          {mode_loop}
          for (int state : std::initializer_list<int>{first}) {{
            target.paintedText.clear();
            target.ys.clear();
            target.attemptedInkBottom = 0;
            tenorchrome::phaseInk.clear();
            tenorchrome::phaseRings.clear();
            tenorchrome::phaseBars.clear();
            QrUtils::bounds.clear();
            {name}Activity page(target);
            page.state = state;
            {setup}
''' + ('            page.mode = authMode;\n' if name == 'KOReaderAuth' else '') + f'''
''' + ('            page.errorMessage = tr(STR_ALREADY_ASSIGNED);\n' if name == 'ButtonRemap' else '') + f'''
            page.renderX3();
            const auto home = tenorchrome::x3ScreenLayout(target);
            for (const auto& bar : tenorchrome::phaseBars)
              check(bar.x == home.barRect.x && bar.y == home.barRect.y &&
                        bar.width == home.barRect.width && bar.height == home.barRect.height,
                    "{name}: exact Home barRect");
            check(target.attemptedInkBottom <= home.barRect.bottom(), "{name}: all ink ends above button band");
            for (const auto& ring : tenorchrome::phaseRings)
              check(ring.y >= home.barRect.y && ring.bottom() <= home.barRect.bottom(), "{name}: bar equals Home");
            int bodyEnd = home.bodyRect.bottom();
            for (const auto& ink : tenorchrome::phaseInk)
              if (ink.tip) bodyEnd = std::min(bodyEnd, ink.rect.y - 8);
            for (const auto& ink : tenorchrome::phaseInk)
              check(ink.tip ? ink.rect.bottom() + 4 == home.barRect.y :
                  ink.rect.y >= home.bodyRect.y && ink.rect.bottom() <= bodyEnd,
                  "{name}: info and tip stay in separate rectangles");
            for (const auto& qr : QrUtils::bounds) {{
              check(qr.y >= home.bodyRect.y && qr.y + qr.height <= bodyEnd, "{name}: QR within body and above tip");
              for (const auto& ink : tenorchrome::phaseInk)
                check(ink.tip || ink.rect.bottom() <= qr.y || ink.rect.y >= qr.y + qr.height, "{name}: information never overlaps the QR (the step line stands under it)");
            }}
'''
        if name == 'ButtonRemap':
            invocation += '''check(page.x3Info.pages == 1, "ButtonRemap: all roles and error fit without stealing reset or cancel keys");
'''
        if name == 'ReadingHistory':
            invocation += '''check(page.grid ? !tenorchrome::phaseInk.empty() : tenorchrome::phaseInk.empty(),
                "ReadingHistory: chrome paints chart only in calendar mode");
'''
        if name == 'KOReaderAuth':
            invocation += '''if (state == WIFI_SELECTION || state == CONNECTING || state == AUTHENTICATING) {
              for (const auto& ink : tenorchrome::phaseInk)
                check(ink.text != tr(STR_AUTH_FAILED) && ink.text != tr(STR_SIGNUP_FAILED),
                      "KOReaderAuth: transition never reports failure");
            }
'''
        if margins:
            invocation += '''UiScreen screen;
            page.buildX3(screen);
            check(screen.margins.top >= home.bodyRect.y, "list starts within body");
            check(target.getScreenHeight() - screen.margins.bottom <= bodyEnd, "list ends within body above tip");
            check(target.getScreenHeight() - screen.margins.bottom - screen.margins.top > 100, "list keeps usable viewport");
'''
        if name in ('OtaUpdate', 'BlePageTurner'):
            invocation += '''const auto popup = page.x3PopupFrame();
            check(popup.y >= home.bodyRect.y && popup.y + popup.height <= home.bodyRect.bottom(), "popup stays in body");
'''
        if name == 'BlePageTurner':
            invocation += '''const char* choices[] = {"Connect", "Forget"};
            page.x3Popup.show("Paired devices", choices, 2, 0, nullptr);
            menutilt::pending = {true, false};
            menutilt::polls = 0;
            page.tiltInput();
            check(menutilt::polls == 1 && page.listTilts == 0, "BLE: popup owns tilt before list navigation");
            page.updates = 0;
            tenorchrome::phaseInk.clear();
            page.renderEntry();
            check(std::any_of(tenorchrome::phaseInk.begin(), tenorchrome::phaseInk.end(),
                [](const auto& ink) { return ink.text == "> Forget"; }), "BLE: tilt moves popup choice");
            for (const auto& ink : tenorchrome::phaseInk) {
              check(ink.text != page.statusText_ && ink.text != page.bindNotice_, "BLE: popup render wins over status footer");
              check(ink.rect.y >= home.bodyRect.y && ink.rect.bottom() <= home.bodyRect.bottom(), "BLE: actual popup ink stays in body");
            }
            page.x3Info.pages = 2;
            page.x3Info.page = 0;
            page.mappedInput.released = MappedInputManager::Button::Down;
            page.pageInput();
            check(page.x3Info.page == 0 && page.updates == 0, "BLE: popup input wins over information paging");
            page.mappedInput.released = MappedInputManager::Button::Back;
            page.x3Popup.handleInput(page.mappedInput, [&] { ++page.updates; });
            check(!page.x3Popup.isActive(), "BLE: Back closes popup");
            page.mappedInput.released = MappedInputManager::Button::None;
            tenorchrome::phaseInk.clear();
            page.renderX3();
'''
        if name in ('ReadingHabits', 'BlePageTurner'):
            invocation += '''page.x3Info.pages = 2;
            page.x3Info.page = 0;
            page.updates = 0;
            page.mappedInput.released = MappedInputManager::Button::Up;
            page.pageInput();
            check(page.x3Info.page == 0 && page.updates == 0, "list: information paging preserves front-button navigation");
            page.x3Info.page = 0;
            page.updates = 0;
            page.mappedInput.released = MappedInputManager::Button::''' + ('Right' if name == 'ReadingHabits' else 'Left') + ''';
            page.pageInput();
            check(page.x3Info.page == 1 && page.updates == 1, "list: side button reaches information pages");
            page.mappedInput.released = MappedInputManager::Button::None;
            page.x3Info.page = 0;
            tenorchrome::phaseInk.clear();
            page.renderX3();
'''
        if name == 'OtaUpdate':
            invocation += '''if (state == WAITING_CONFIRMATION) {
              const char* choices[] = {"Cancel", "Update"};
              page.x3Popup.show("Update", choices, 2, 0, nullptr);
              page.idleExpired = true;
              page.pageInput();
              check(page.idleChecks == 1 && page.finished == 1, "OTA: confirmation retains idle timeout");
            }
'''
        invocation += f'''std::vector<std::string> information;
            for (int number = 0; number < page.x3Info.pages; ++number) {{
              if (number) {{
                tenorchrome::phaseInk.clear();
                page.x3Info.page = number;
                page.renderX3();
              }}
              int left = 0;
              for (const auto& ink : tenorchrome::phaseInk) if (!ink.tip) ++left;
              for (const auto& ink : tenorchrome::phaseInk) {{
                if (ink.tip) continue;
                if (page.x3Info.pages > 1 && --left == 0) continue;
                information.push_back(ink.text);
              }}
            }}
            const std::string identity = "{name}" + std::to_string(tier) + "/" + std::to_string(mode) + "/" +
                std::to_string(static_cast<int>(page.mode)) + "/" +
                std::to_string(state) + "/" + std::to_string(phaseLocale) + "/" +
                std::to_string(page.selected) + "/" + std::to_string(page.failed) + "/" +
                std::to_string(page.readable) + "/" + std::to_string(page.awarded) + "/" +
                std::to_string(page.isApMode) + "/" + std::to_string(page.x3Step) + "/" +
                std::to_string(page.networks.empty()) + "/" + std::to_string(page.grid);
            if (!tips) phaseInformation[identity] = information;
            else check(phaseInformation[identity] == information, "{name}: tips Off keeps every information item");
'''
        invocation += '          }\n          ++activityCases;\n        }'
        invocations.append(invocation)
    bounds = bounds.replace('// PHASE_TWO_CLASSES', '\n'.join(classes))
    fixture = fixture.replace('// PHASE_TWO_BOUNDARIES', '') + bounds
    fixture = fixture.replace('  void drawText(int, int x, int y, const char* text) const {',
                              '  void drawText(int, int x, int y, const char* text, bool = true, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {')
    fixture = fixture.replace('enum class Color { LightGray };', 'enum class Color { Black, White, LightGray };')
    theme = read_source('src/components/themes/lyra/LyraTheme.h')
    spacing = re.search(r'\.verticalSpacing\s*=\s*(\d+)', theme).group(1)
    assert not re.search(r'\b(?:m|metrics)\.verticalSpacing\s*[-+*/]?=',
                         read_source('src/components/themes/TenorTheme.h') +
                         read_source('src/components/UIThemeSizing.h'))
    fixture = fixture.replace('struct Metrics { int buttonHintsHeight; };',
                              f'struct Metrics {{ int buttonHintsHeight; int verticalSpacing = {spacing}; }};')
    fixture = fixture.replace('void drawCenteredText(int font, int y, const char* text) const',
                              'void drawCenteredText(int font, int y, const char* text, bool = true) const')
    fixture = fixture.replace('void fillRect(int x, int y, int w, int h, bool black) const',
                              'void fillRect(int x, int y, int w, int h, bool black = true) const')
    fixture = fixture.replace('void drawLine(int, int y, int, int endY) const',
                              'void drawLine(int, int y, int, int endY, bool = true) const')
    fixture = fixture.replace('  int getScreenHeight() const', '''  void clearScreen() const {}
  void displayBuffer() const {}
  template <typename Shade> void fillRectDither(int x, int y, int width, int height, Shade shade) const {
    fillRect(x, y, width, height, static_cast<int>(shade) == 0);
  }
  int getScreenHeight() const''')
    fixture = fixture.replace('namespace tenorchrome {\nstd::vector<X3ScreenRect> phaseRings;',
                              'namespace tenorchrome {\nstd::vector<X3ScreenRect> phaseRings;')
    imports = ['#include <initializer_list>', '#include <cstring>', '#include <cstdio>', '#include <functional>']
    fixture = '\n'.join(imports) + '\n' + fixture
    localized = []
    for locale, language in enumerate(('english', 'vietnamese', 'chinese')):
        translation = (root / f'lib/I18n/translations/{language}.yaml').read_text()
        words = {key: json.loads(value.strip()) for key, value in
                 re.findall(r'^(STR_[A-Z0-9_]+):\s*(.+)$', translation, re.M) if key in keys}
        missing = [key for key in keys if key not in words]
        if missing:
            raise AssertionError(f'{language}: missing strings {missing}')
        entries = ',\n'.join('{StrId::' + key + ', ' + json.dumps(str(words[key]), ensure_ascii=False) + '}' for key in keys)
        localized.append('{\nconst int phaseLocale = ' + str(locale) + ';\nphaseWords = {\n' + entries + '\n};\n' + '\n'.join(invocations) + '\nphaseWords.clear();\n}')
    return fixture, '\n'.join(localized), states


def verify_keyboard(root, work, output, function):
    cases = re.findall(r'^KEYBOARD_CASE ((?:\d+\s*){14})$', output, re.M)
    if not cases:
        return
    keyboard = (root / 'src/activities/util/KeyboardEntryActivity.cpp').read_text()
    render = function(keyboard, 'void KeyboardEntryActivity::render(')
    start = render.index('  if (tenorchrome::x3ScreenLayoutEnabled()) {\n    props.keyStyles')
    styles = function(render[start:], '  if (tenorchrome::x3ScreenLayoutEnabled())')
    assert 'props.keyStyles.selected.borderWidth = 3' in styles
    assert 'props.keyRadius = 8' in styles
    assert 'uint8_t keyboardAligned = 1;' in (root / 'src/CrossPointSettings.h').read_text()
    unit = Path(__file__).with_name('X3KeyboardStyleTest.cpp').read_text()
    unit = unit.replace('// KEYBOARD_CASES', ',\n'.join('{' + ', '.join(case.split()) + '}' for case in cases))
    unit = unit.replace('// KEYBOARD_STYLE', styles)
    url_layers = keyboard[keyboard.index('#define UK('):keyboard.index('// tenor/ugly on the button readers: every key')]
    unit = unit.replace('PRODUCTION_URL_LAYERS', url_layers)
    unit = unit.replace('PRODUCTION_LAYOUT_METHOD', function(keyboard, 'const fui::KeyboardLayout& KeyboardEntryActivity::currentLayout(').replace('KeyboardEntryActivity::', ''))
    path = work / 'keyboard.cpp'
    path.write_text(unit)
    executable = work / 'keyboard'
    sdk = root / 'freeink-sdk/libs/ui/FreeInkUI'
    subprocess.run(['c++', '-std=c++20', '-O0', '-UNDEBUG', '-I', str(root), '-I', str(root / 'src'),
                    '-I', str(sdk / 'include'), str(path), str(sdk / 'src/FreeInkUI.cpp'),
                    '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
