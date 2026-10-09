import pathlib
import re
import subprocess
import sys

root, output = map(pathlib.Path, sys.argv[1:3])
case, compiler = sys.argv[3:5]
reader = (root / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
if case == 'T5':
    settings = (root / 'src/CrossPointSettings.h').read_text()
    names = ['KOSYNC', 'DISABLED', 'BOOKMARK', 'DICTIONARY', 'READER_MENU',
             'FILE_TRANSFER', 'TILT_PAGE_TURN', 'SAVE_QUOTE', 'CONNECT_REMOTE']
    for number, name in enumerate(names):
        assert re.search(r'LP_MENU_' + name + r'\s*=\s*' + str(number) + r'\b', settings), name
    threshold = reader.split('unsigned long EpubReaderActivity::confirmLongPressThreshold() const {')[1]
    threshold = threshold.split('void EpubReaderActivity::toggleTiltFromReader')[0]
    assert 'case CrossPointSettings::LP_MENU_CONNECT_REMOTE:' in threshold
    assert threshold.index('LP_MENU_CONNECT_REMOTE') < threshold.index('return ReaderUtils::BOOKMARK_HOLD_MS')
    utils = (root / 'src/activities/reader/ReaderUtils.h').read_text()
    assert re.search(r'BOOKMARK_HOLD_MS\s*=\s*400\b', utils)
    source = subprocess.check_output(['git', '-C', str(root), 'show', 'v1.0.55:src/activities/reader/EpubReaderActivity.cpp'], text=True)
    def old_cases(text):
        switch = text.split('switch (SETTINGS.longPressMenuFunction) {')[1].split('\n  // Home-key boards')[0]
        switch = re.sub(r'      case CrossPointSettings::LP_MENU_CONNECT_REMOTE:.*?(?=      case |      default:)', '', switch, flags=re.S)
        return switch
    assert old_cases(reader) == old_cases(source), 'legacy handler changed'
    print('T5 stored numbers 0-7 and legacy handler unchanged; new hold 400 ms: GREEN')
    sys.exit(0)

switch = reader.split('switch (SETTINGS.longPressMenuFunction) {')[1]
match = re.search(r'case CrossPointSettings::LP_MENU_CONNECT_REMOTE:(.*?)(?=      case |      default:)', switch, re.S)
action = match.group(1) if match else ''
output.mkdir(parents=True, exist_ok=True)
projection = output / 'action.cpp'
note_source = (root / 'src/activities/reader/ReaderActivity.cpp').read_text()
note_function = 'bool ReaderActivity::linkNoteTitle' + note_source.split('bool ReaderActivity::linkNoteTitle')[1].split('\n}\n')[0] + '\n}\n'
popup_function = 'void EpubReaderActivity::redrawLinkNote' + reader.split('void EpubReaderActivity::redrawLinkNote')[1].split('\n}\n')[0] + '\n}\n'
projection.write_text('''#include <cassert>
#include <cstring>
#include <string>
#include "Fakes.h"
#define tr(id) (#id)
struct ReaderActivity {
  mutable bleturner::LinkNote linkNoteDrawn = bleturner::LinkNote::None;
  mutable bool linkNoteInTitle = false;
  bool linkNoteTitle(std::string& title) const;
};
unsigned long millis() { return fake::radio().now; }
struct EpubReaderActivity : ReaderActivity {
  bool statusBarStale = false, bleConnectMessage = true, showBookmarkMessage = false;
  unsigned long bookmarkMessageTime = 0;
  unsigned updates = 0;
  void requestUpdate() { ++updates; }
  void redrawLinkNote();
};
''' + note_function + popup_function + '''
struct Settings {
  bleturner::Config ble;
  unsigned saves = 0;
  void saveToFile() { ++saves; }
} SETTINGS;
void holdSelect() {
  static bool bleConnectMessage = false;
  static bool linkNoteInTitle = false;
  static bleturner::LinkNote linkNoteDrawn = bleturner::LinkNote::None;
''' + action + '''
}
int main() {
  fake::reset();
  SETTINGS.ble.pick = 1;
  std::strcpy(SETTINGS.ble.peerAddr, "11:22:33:44:55:66");
  bleturner::begin(fake::hostFns(), SETTINGS.ble);
  fake::radio().queueStarts = true;
  bleturner::tick(fake::reading());
  holdSelect();
  assert(SETTINGS.ble.enabled == 1);
  assert(SETTINGS.saves == 1);
  assert(fake::radio().creates == 0);
  bleturner::tick(fake::reading());
  assert(fake::radio().creates == 1);
  holdSelect();
  assert(SETTINGS.saves == 1);
  bleturner::tick(fake::reading());
  assert(fake::radio().creates == 1);
  fake::radio().runStart();
  bleturner::tick(fake::reading());
  assert(fake::radio().armCalls == 1);
  assert(fake::radio().armedPick == 1);
  assert(fake::radio().armedAddr == SETTINGS.ble.peerAddr);
  fake::radio().connected = true;
  fake::radio().name = "Remote A";
  bleturner::requestConnect();
  bleturner::tick(fake::reading());
  EpubReaderActivity reader;
  std::string title;
  assert(reader.linkNoteTitle(title));
  assert(title == "STR_CONNECTED Remote A");
  reader.redrawLinkNote();
  assert(reader.showBookmarkMessage && reader.updates == 1);
  assert(reader.bookmarkMessageTime == millis());
  fake::reset();
  bleturner::begin(fake::hostFns(), SETTINGS.ble);
  fake::host().heap = {81919, 32768};
  bleturner::requestConnect();
  bleturner::tick(fake::reading());
  bleturner::tick(fake::reading());
  assert(reader.linkNoteTitle(title));
  assert(title == "STR_BLE_READER_LOW_RAM");
  reader.showBookmarkMessage = false;
  reader.redrawLinkNote();
  assert(reader.showBookmarkMessage && reader.updates == 2);
}
''')
module = root / 'lib/BlePageTurner'
binary = output / 'action'
subprocess.run([compiler, '-std=c++20', '-DBLETURNER_TESTING', '-DFREEINK_CAP_BLE_HID_HOST=1',
                '-I' + str(module / 'include'), '-I' + str(module / 'src'), '-I' + str(module / 'test'),
                str(projection), str(module / 'src/Runtime.cpp'), str(module / 'test/Fakes.cpp'),
                '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
print('T1 production action enables, saves once, requests once, arms saved pick: GREEN')
