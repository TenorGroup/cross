from pathlib import Path
import argparse
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--work', required=True, type=Path)
parser.add_argument('--mutate', action='store_true')
args = parser.parse_args()
runner = Path(__file__).with_name('run.py')
settings = {
    'About', 'BookStats', 'BookStatsLibrary', 'ClearCache', 'ClockSettings', 'FontDownload',
    'InfoUpdate', 'KeyboardLayouts', 'LanguageSelect', 'KOReaderSettings', 'OpdsServerList',
    'HomeButtonSettings', 'SettingsChoice',
    'OpdsSettings', 'Settings', 'StatusBarSettings', 'TextSettings', 'TimezonePicker',
}
lists = {
    'FileBrowser', 'QuoteDetail', 'QuoteTrim', 'Quotes', 'EpubReaderBookmarks',
    'EpubReaderChapterSelection', 'EpubReaderMenu', 'XtcReaderChapterSelection',
}
forms = {
    'ButtonRemap', 'ChapterNumberEntry', 'Crash', 'KeyboardEntry', 'QrDisplay', 'ReadingHabits',
    'ReadingHistory', 'BlePageTurner', 'CalibreConnect', 'ClockSync', 'CrossPointWebServer',
    'KOReaderAuth', 'KOReaderSync', 'NetworkModeSelection', 'OpdsBookBrowser', 'OtaUpdate',
    'SdFirmwareUpdate', 'WifiSelection',
}
activities = settings | lists | forms | {'Home'}
covered = activities | {'EpubReader', 'ReaderToolbarUi'}
bound = set()
for path in (root / 'src/activities').rglob('*Activity.cpp'):
    header = path.with_suffix('.h')
    source = path.read_text() + (header.read_text() if header.exists() else '')
    if re.search(r'x3ScreenLayout\(|x3settings::beginScreen\(', source):
        bound.add(path.stem.removesuffix('Activity'))
# Confirmation uses the body bounds to center its overlay and preserves the caller's bar.
assert bound <= covered | {'UiList', 'UiTabList', 'Confirmation'}, sorted(bound - covered)
assert len(activities) == 45
assert len(covered) == 47
toolbar = (root / 'src/activities/reader/ReaderToolbarUi.cpp').read_text()
assert 'ReaderToolbarUi::screenLayout(' in toolbar, 'RED: toolbar lacks the Home body/tip/bar binding'
subprocess.run([sys.executable, str(Path(__file__).with_name('toolbar_bars.py')),
                '--work', str(args.work / 'toolbar'), *(['--mutate'] if args.mutate else [])], check=True)
ugly_sources = {name: root / f'src/shells/ugly/Ugly{name}.cpp'
                for name in ('Diary', 'Desk', 'Notebook', 'QuestionSheet')}
assert all(re.search(r'x3ScreenLayout\(|(?:pathHints|pageHints)\(', source.read_text())
           for source in ugly_sources.values())
assert 'x3ScreenLayout(r' in (root / 'src/shells/ugly/UglyInk.cpp').read_text()
command = [sys.executable, str(runner), '--phase2', '--work', str(args.work / 'lists-settings')]
if args.mutate:
    command.append('--mutate-bar')
subprocess.run(command, check=True)
subprocess.run([sys.executable, str(runner), '--forms', '--work', str(args.work / 'forms')], check=True)
subprocess.run([sys.executable, str(runner), '--ugly-contract', '--work', str(args.work / 'ugly')], check=True)
subprocess.run([sys.executable, str(runner), '--ugly-status-contract', '--work', str(args.work / 'ugly-status')], check=True)
subprocess.run([sys.executable, str(runner), '--footer-spacing', '--work', str(args.work / 'footer-spacing')], check=True)
print('GREEN: 45 X3 activity classes, 2 reader toolbar bindings and 4 handwritten screens, 3 sizes x 3 status modes x 2 tips modes, exact Home barRect')
print('COVERED:', ', '.join(sorted(covered)))
