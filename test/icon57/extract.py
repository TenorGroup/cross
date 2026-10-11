from pathlib import Path
import sys


def function(path, signature):
    text = path.read_text()
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 1
    cursor = opening + 1
    while depth:
        depth += (text[cursor] == '{') - (text[cursor] == '}')
        cursor += 1
    return text[start:cursor]


root = Path(sys.argv[1])
chrome = root / 'src/components/TenorMenuChrome.cpp'
functions = [function(chrome, name) for name in (
    'bool inPill(', 'void tenorchrome::drawPillRing(', 'void tenorchrome::drawBarFrame(',
    'void tenorchrome::drawBarIcon(', 'void tenorchrome::drawBarTab(')]
functions += [function(root / 'src/activities/UiTabListActivity.cpp', 'void UiTabListActivity::veThanhTheTenor('),
              function(root / 'src/activities/home/HomeActivity.cpp', 'freeink::ui::BitmapRef HomeActivity::tabIcon('),
              function(root / 'src/activities/settings/TextSettingsActivity.cpp', 'TextSettingsActivity::Tab TextSettingsActivity::tabForIndex('),
              function(root / 'src/activities/settings/TextSettingsActivity.cpp', 'freeink::ui::BitmapRef TextSettingsActivity::tabIcon('),
              function(root / 'src/activities/reader/EpubReaderMenuActivity.cpp', 'freeink::ui::BitmapRef EpubReaderMenuActivity::tabIcon('),
              function(root / 'src/activities/reader/ReaderToolbarUi.cpp', 'void ReaderToolbarUi::buildToolRow(')]
# The tab centres come from the one production function (TenorMenuChrome.h), inside its namespace.
centre = function(root / 'src/components/TenorMenuChrome.h', 'constexpr int barTabCentre(')
Path(sys.argv[2]).write_text('namespace tenorchrome {\n' + centre + '\n}\n\n' + '\n\n'.join(functions) + '\n')
