"""Run Home's production Settings-page row methods: one list, one order and the same titles on every device."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
home = (ROOT / 'src/activities/home/HomeActivity.cpp').read_text()
methods = []
for name in ('settingsRow', 'rowStartsGroup', 'focusFavorite'):
    method = re.search(r'(?:void|bool|int) HomeActivity::' + name + r'\([^\n]*\) (?:const )?\{.*?\n\}', home, re.S)
    assert method, name
    methods.append(method.group())
# The X3 and X4 Classic once drew this page through their own layout; one path now serves every device.
assert 'buildSettingsGroups' not in home and 'homesettings::order' not in home and 'touchOrder' not in home
for method in methods:
    assert 'kTouchShell' not in method and 'infoupdate' not in method, method

HARNESS = r'''
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include "components/HomeSettingsLayout.h"
namespace freeink::ui { struct ListItem { const char* label = nullptr; const char* sectionHeading = nullptr; int actionValue = 0; }; }
namespace fui = freeink::ui;
I18n& I18n::getInstance() { static I18n i18n; return i18n; }
const char* I18n::get(StrId id) const {
  switch (id) {
    case StrId::STR_READER_TAB_READING: return "Reading";
    case StrId::STR_CAT_SYSTEM: return "System";
    case StrId::STR_CAT_OTHER: return "Other";
    default: return "?";
  }
}
struct RenderLock { template<class T> explicit RenderLock(T&) {} };
struct Nav { int selected = 0; bool followOnBuild = false; };
struct UiListActivity { int focusFavorite(const std::string&) { return -2; } };
struct HomeActivity : UiListActivity {
  enum class Tab { CAI_DAT, OTHER };
  Tab activeTabId = Tab::CAI_DAT;
  bool pinned = false;
  std::string settingsTransferLabel;
  std::vector<fui::ListItem> rowItems;
  std::vector<int> settingsGroups;
  Nav nav;
  Nav& activeNav() { return nav; }
  bool rowIsPinned(int row) const { return pinned && row == 0; }
  static void settingsRow(void*, uint16_t, fui::ListItem&);
  bool rowStartsGroup(int row) const;
  int focusFavorite(const std::string&);
};
'''
CASES = r'''
int main() {
  for (const bool motion : {true, false}) {
    HomeActivity home;
    for (const auto id : settingstabs::DEFAULT_ORDER)
      if (id < (motion ? 9 : 8)) home.settingsGroups.push_back(id);
    const int rows = static_cast<int>(home.settingsGroups.size()) + 2;  // Connections, groups, About & updates
    std::vector<std::string> labels(rows, "row");
    home.rowItems.resize(rows);
    for (int i = 0; i < rows; ++i) home.rowItems[i].label = labels[i].c_str(), home.rowItems[i].actionValue = i;
    std::vector<std::string> titles;
    for (int i = 0; i < rows; ++i) {
      fui::ListItem item;
      HomeActivity::settingsRow(&home, i, item);
      assert(item.label == home.rowItems[i].label && item.actionValue == i);
      titles.emplace_back(item.sectionHeading ? item.sectionHeading : "");
      assert(home.rowStartsGroup(i) == (item.sectionHeading != nullptr));
    }
    std::vector<std::string> want(rows);
    want[0] = "Reading", want[4] = "System", want[motion ? 8 : 7] = "Other";
    assert(titles == want);
    home.pinned = true;
    fui::ListItem pinned;
    HomeActivity::settingsRow(&home, 0, pinned);
    assert(std::string(pinned.label) == "\xEE\x84\x8A" + labels[0] && std::string(pinned.sectionHeading) == "Reading");
    assert(home.focusFavorite("action/15") == 0 && home.nav.selected == 1 && home.nav.followOnBuild);
    assert(home.focusFavorite("other") == -2);
    home.activeTabId = HomeActivity::Tab::OTHER;
    assert(!home.rowStartsGroup(0));
  }
  // An order the user chose: the same rows, untitled.
  HomeActivity chosen;
  chosen.settingsGroups = {0, 7, 1, 2, 3, 4, 5, 6};
  chosen.rowItems.resize(10);
  for (int i = 0; i < 10; ++i) {
    fui::ListItem item;
    HomeActivity::settingsRow(&chosen, i, item);
    assert(!item.sectionHeading && !chosen.rowStartsGroup(i));
  }
  std::printf("PASS: one Settings page, titled at Connections, System and Sleep, untitled when the user chose\n");
}
'''

with tempfile.TemporaryDirectory(prefix='home-settings-binding-') as temporary:
    folder = Path(temporary)
    (folder / 'test.cpp').write_text(HARNESS + '\n'.join(methods) + CASES)
    build = subprocess.run(['c++', '-std=c++20', '-UNDEBUG', '-I' + str(ROOT / 'src'), '-I' + str(ROOT / 'lib/I18n'),
                            str(folder / 'test.cpp'), '-o', str(folder / 'test')], capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run([str(folder / 'test')], capture_output=True, text=True)
    if artifact := os.environ.get('CROSSPOINT_TEST_ARTIFACTS'):
        output = Path(artifact); output.mkdir(parents=True, exist_ok=True)
        (output / 'binding.cpp').write_text('\n'.join(methods))
        (output / 'result.log').write_text(run.stdout + run.stderr)
    assert run.returncode == 0, run.stdout + run.stderr
    print(run.stdout, end='')
