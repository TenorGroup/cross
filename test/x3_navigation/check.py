from pathlib import Path
import re
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('--mutate-local', action='store_true')
parser.add_argument('--mutate-screen', action='store_true')
args = parser.parse_args()

root = Path(__file__).resolve().parents[2]
header = root / 'src/components/X3NavigationPath.h'
assert header.exists(), 'RED: no shared stack navigation layout'
manager = (root / 'src/activities/ActivityManager.cpp').read_text()
assert 'ActivityManager::x3NavigationPath(' in manager, 'RED: no stack path builder'
chrome = (root / 'src/components/TenorMenuChrome.cpp').read_text()
assert 'case X3PathIcon::More: return icon_reader_tab_more_40;' in chrome, 'RED: settings Other parent uses the Tools icon'
assert 'drawPillLabel(renderer, UI_12_FONT_ID, pill.text.c_str(), leftEdge, top, pill.width, height)' in chrome, 'RED: navigation path bypasses shared ink centering'
tabs = (root / 'src/activities/UiTabListActivity.cpp').read_text()
assert 'else if (readerTextPath)' in tabs, 'RED: reader text settings still show root tabs'
assert '&& !readerTextPath' in tabs, 'RED: reader text path still draws tab arrows'
reader_menu = (root / 'src/activities/reader/EpubReaderMenuActivity.h').read_text()
assert 'return textOnly ? tenorchrome::X3PathIcon::Reading : icons[activeTab()]' in reader_menu, 'RED: reader parent icon ignores its active tab'
remap = (root / 'src/activities/settings/ButtonRemapActivity.cpp').read_text()
assert 'return shell::uglyParts() ? "-" : "";' in remap, 'RED: unassigned X3 remap footer erases the path'
assert 'stackActivities' in manager.split('ActivityManager::x3NavigationPath(', 1)[1].split('\n}', 1)[0]
for relative in ('X3ActivityLayout.h', 'settings/X3SettingsLayout.h', 'home/X3FileReaderChrome.h'):
    text = (root / 'src/activities' / relative).read_text()
    if args.mutate_local and relative == 'X3ActivityLayout.h':
        text += '\nvoid mutant() { drawPillRing(); }\n'
    assert 'drawX3ActivityPath(renderer,' in text, f'RED: {relative} bypasses shared painter'
    assert 'drawPillRing(' not in text and 'drawPathPill(' not in text, f'RED: local path in {relative}'
classes = []
for path in (root / 'src/activities').rglob('*Activity.cpp'):
    sibling = path.with_suffix('.h')
    text = path.read_text() + (sibling.read_text() if sibling.exists() else '')
    if re.search(r'x3ScreenLayout\(|x3settings::beginScreen\(', text):
        name = path.stem
        # Confirmation centers an overlay in bodyRect and keeps the caller's navigation path.
        if name in ('HomeActivity', 'EpubReaderActivity', 'UiListActivity', 'UiTabListActivity', 'ConfirmationActivity'):
            continue
        if args.mutate_screen and name == 'BlePageTurnerActivity':
            text += '\nvoid mutantScreen() { tenorchrome::drawPillRing(); }\n'
        assert 'drawPillRing(' not in text and 'drawPathPill(' not in text, f'RED: local path painter in {name}'
        assert 'navigationIcon() const override' in sibling.read_text(), f'RED: {name} has no own icon'
        classes.append(name)
print(f'GREEN: shared stack builder, single painter, {len(classes)} activity icon declarations')
