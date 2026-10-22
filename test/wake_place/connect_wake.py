#!/usr/bin/env python3
"""The Connections wake, built from the production sources.

Where a sleep taken in Connections (the list, a session, the font store) is kept, and where the wake puts it back:
the answers of the screens (wakePlace), the capture that walks the stack, goToWakePlace / goToFileTransfer, and the
Settings action rows. Each production function is cut out of its file and compiled against host stand-ins for the
screens around it, once for each board (X3: 4 rows; X4 Classic / X4 Pro: 6 rows; a board with WebDAV but no USB: 5).
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("--repo", required=True, type=Path)
p.add_argument("--output", required=True, type=Path)
p.add_argument("--cxx", default="c++")
args = p.parse_args()
repo = args.repo.resolve()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
src = repo / "src"


def read(path):
    return (repo / path).read_text()


def cut(text, signature, what=None):
    """A function (or block) from its signature through the brace that closes it."""
    start = text.find(signature)
    if start < 0:
        sys.exit(f"RED missing in production: {what or signature}")
    brace = text.index("{", start)
    depth = 0
    for i in range(brace, len(text)):
        depth += (text[i] == "{") - (text[i] == "}")
        if depth == 0:
            return text[start:i + 1]
    sys.exit(f"unterminated: {signature}")


manager = read("src/activities/ActivityManager.cpp")
cpws_h = read("src/activities/network/CrossPointWebServerActivity.h")
cpws = read("src/activities/network/CrossPointWebServerActivity.cpp")
nms_h = read("src/activities/network/NetworkModeSelectionActivity.h")
nms = read("src/activities/network/NetworkModeSelectionActivity.cpp")
webdav = read("src/activities/network/WebDavPullActivity.cpp")
kosync_h = read("src/activities/reader/KOReaderSyncActivity.h")
bmp = read("src/activities/util/BmpViewerActivity.cpp")
fshelpers = read("lib/FsHelpers/FsHelpers.cpp")
listact = read("src/activities/UiListActivity.cpp")
settings = read("src/activities/settings/SettingsActivity.cpp")
tabs = read("src/activities/settings/SettingsTabs.h")
main = read("src/main.cpp")
fontdl_h = read("src/activities/settings/FontDownloadActivity.h")

# --- cuts ------------------------------------------------------------------------------------------------------
enum_mode = cut(nms_h, "enum class NetworkMode", "NetworkMode enum") + ";"
count_block = nms_h[nms_h.index("#if FREEINK_CAP_USB_MSC\n  static constexpr int MENU_ITEM_COUNT"):]
count_block = count_block[:count_block.index("#endif") + len("#endif")]
rows_block = nms[nms.index("namespace {\n// One row a mode"):]
rows_block = rows_block[:rows_block.index("}  // namespace") + len("}  // namespace")]
row_of = cut(nms, "int NetworkModeSelectionActivity::rowOf(", "NetworkModeSelectionActivity::rowOf")
nms_wake = cut(nms, "bool NetworkModeSelectionActivity::wakePlace(", "NetworkModeSelectionActivity::wakePlace")
kep = cut(listact, "int UiListActivity::kepConTro(").replace("UiListActivity::", "NetworkModeSelectionActivity::")
cpws_wake = cut(cpws_h, "bool wakePlace(WakePlace& place) const override", "CrossPointWebServerActivity::wakePlace")
webdav_wake = cut(webdav, "bool WebDavPullActivity::wakePlace(", "WebDavPullActivity::wakePlace")
kosync_wake = cut(kosync_h, "bool wakePlace(WakePlace& place) const override", "KOReaderSyncActivity::wakePlace")
bmp_wake = cut(bmp, "bool BmpViewerActivity::wakePlace(", "BmpViewerActivity::wakePlace")
folder_of = cut(fshelpers, "std::string extractFolderPath(")
capture = cut(manager, "WakePlace ActivityManager::captureWakePlace() const", "captureWakePlace")
to_place = cut(manager, "void ActivityManager::goToWakePlace(", "goToWakePlace")
to_transfer = cut(manager, "void ActivityManager::goToFileTransfer(", "goToFileTransfer")
settings_wake = cut(settings, "bool SettingsActivity::wakePlace(", "SettingsActivity::wakePlace")
settings_key = cut(settings, "std::string wakeKey(", "SettingsActivity wakeKey")
settings_enter = cut(settings, "void SettingsActivity::onEnter()")
action_enum = cut(tabs, "enum class Action : uint8_t") + ";"
selected = cut(cpws, "void CrossPointWebServerActivity::onNetworkModeSelected(", "onNetworkModeSelected")

# StrId / UIIcon names used by the rows table become two enums of that many names.
strs = sorted(set(re.findall(r"StrId::(\w+)", rows_block)))
icons = sorted(set(re.findall(r"UIIcon::(\w+)", rows_block)))

harness = r'''
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "activities/MenuNavigationMemory.h"
#include "util/WakePlace.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("RED %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

struct { uint8_t uiShell = 0; } SETTINGS;
struct { std::string openEpubPath; } APP_STATE;

struct Activity {
  std::string name;
  bool prevent = false;  // what preventAutoSleep says
  explicit Activity(std::string n = "") : name(std::move(n)) {}
  virtual ~Activity() = default;
  virtual bool wakePlace(WakePlace&) const { return false; }
  virtual bool preventAutoSleep() { return prevent; }
};

enum class StrId { @@STRS@@ };
enum class UIIcon { @@ICONS@@ };
@@ENUM_MODE@@
@@ACTION_ENUM@@
using SettingAction = Action;

// --- the Connections list --------------------------------------------------------------------------------------
struct NavStub { int selected = 0; };
struct NetworkModeSelectionActivity : Activity {
  NavStub nav;
  std::string returnBook_;
  NetworkModeSelectionActivity() : Activity("NetworkModeSelection") {}
@@COUNT_BLOCK@@
  static int kepConTro(int chon, int soDong);
  static int rowOf(NetworkMode mode);
  bool wakePlace(WakePlace& place) const override;
};
@@ROWS_BLOCK@@
@@KEP@@
@@ROW_OF@@
@@NMS_WAKE@@

struct MappedInputStub { bool touch = false; bool hasTouch() const { return touch; } };

// --- the session screens ---------------------------------------------------------------------------------------
struct CrossPointWebServerActivity : Activity {
  NetworkMode networkMode = NetworkMode::JOIN_NETWORK;
  std::string returnBook;
  bool openFontStore = false;
  CrossPointWebServerActivity() : Activity("CrossPointWebServer") {}
  CrossPointWebServerActivity(int&, const MappedInputStub&, std::string book, bool store)
      : Activity("CrossPointWebServer"), returnBook(std::move(book)), openFontStore(store) {}
  @@CPWS_WAKE@@
};
struct WebDavPullActivity : Activity {
  WebDavPullActivity() : Activity("WebDavPull") {}
  bool wakePlace(WakePlace& place) const override;
};
@@WEBDAV_WAKE@@
struct KOReaderSyncActivity : Activity {
  std::string epubPath;
  KOReaderSyncActivity() : Activity("KOReaderSync") {}
  @@KOSYNC_WAKE@@
};
namespace FsHelpers {
@@FOLDER_OF@@
}  // namespace FsHelpers
struct BmpViewerActivity : Activity {
  std::string filePath;
  BmpViewerActivity() : Activity("BmpViewer") {}
  bool wakePlace(WakePlace& place) const override;
};
@@BMP_WAKE@@

// Screens that never answer, or answer as they did before.
struct Silent : Activity { using Activity::Activity; };
struct Answers : Activity {
  WakePlace answer;
  Answers(std::string n, WakePlace a) : Activity(std::move(n)), answer(std::move(a)) {}
  bool wakePlace(WakePlace& place) const override { place.kind = answer.kind; place.tab = answer.tab; place.path = answer.path; place.name = answer.name; return true; }
};

// --- the manager -----------------------------------------------------------------------------------------------
enum class HomeMenuItem { RECENTS, FILE_BROWSER, STATS_TAB, SETTINGS_MENU, FAVORITES_TAB, RECENT_CONTINUE, NONE };
struct HomeActivity { static constexpr int TAB_COUNT = 5; };
struct { uint32_t opened() const { return 7; } } RECENT_BOOKS;
struct ActivityManager {
  std::unique_ptr<Activity> currentActivity;
  std::vector<std::unique_ptr<Activity>> stackActivities;
  MappedInputStub mappedInput;
  MenuNavigationMemory navigationMemory;
  int renderer = 0;
  bool reader = false;  // isReaderActivity()
  // what the last go-to asked for
  std::string went;
  std::unique_ptr<CrossPointWebServerActivity> transfer;
  int settingsTab = -1;
  std::string settingsKey;
  std::string browserPath;
  bool isReaderActivity() const { return reader; }
  void replaceActivity(std::unique_ptr<CrossPointWebServerActivity>&& a) { transfer = std::move(a); went = "transfer"; }
  void goHome(HomeMenuItem item, bool) { went = "home:" + std::to_string(static_cast<int>(item)); }
  void goToFileBrowser(std::string path) { browserPath = std::move(path); went = "files"; }
  void goToSettings(int tab, std::string key = {}) { settingsTab = tab; settingsKey = std::move(key); went = "settings"; }
  void goToFileTransfer(bool openFontStore = false, std::string returnBook = {});
  WakePlace captureWakePlace() const;
  void goToWakePlace(const WakePlace& place, bool cleanInitialRefresh);
};
ActivityManager activityManager;
@@CAPTURE@@
@@TO_PLACE@@
@@TO_TRANSFER@@

// --- Settings --------------------------------------------------------------------------------------------------
struct SettingInfo { const char* key = nullptr; SettingAction action = SettingAction::None; };
namespace { @@SETTINGS_KEY@@ }
struct SettingsActivity : Activity {
  int selectedCategoryIndex = 0;
  int settingsCount = 0;
  const std::vector<SettingInfo>* currentSettings = nullptr;
  int ring = 0;
  int ringPos() const { return ring; }
  SettingsActivity() : Activity("Settings") {}
  bool wakePlace(WakePlace& place) const override;
};
@@SETTINGS_WAKE@@

// ---------------------------------------------------------------------------------------------------------------
WakePlace sleepNow() { return activityManager.captureWakePlace(); }
void reset() {
  activityManager.currentActivity.reset();
  activityManager.stackActivities.clear();
  activityManager.went.clear();
  activityManager.transfer.reset();
  activityManager.navigationMemory = MenuNavigationMemory{};
  activityManager.reader = false;
  activityManager.mappedInput.touch = false;
  APP_STATE.openEpubPath.clear();
}
template <class T> T* top(std::unique_ptr<T> a) { auto* raw = a.get(); activityManager.currentActivity = std::move(a); return raw; }
template <class T> T* under(std::unique_ptr<T> a) { auto* raw = a.get(); activityManager.stackActivities.push_back(std::move(a)); return raw; }
std::unique_ptr<CrossPointWebServerActivity> session(NetworkMode mode, std::string book = "", bool prevent = false) {
  auto a = std::make_unique<CrossPointWebServerActivity>();
  a->networkMode = mode;
  a->returnBook = std::move(book);
  a->prevent = prevent;
  return a;
}
const int MODE_COUNT = NetworkModeSelectionActivity::MENU_ITEM_COUNT;
bool boardHas(NetworkMode m) { return NetworkModeSelectionActivity::rowOf(m) >= 0; }

int main() {
  const bool hasUsb = boardHas(NetworkMode::USB_DRIVE), hasWebDav = boardHas(NetworkMode::WEB_DAV);
  std::printf("board rows=%d usb=%d webdav=%d\n", MODE_COUNT, hasUsb, hasWebDav);

  // The rows table: one row a mode, each mode once, the font store last; the board's own count.
  CHECK(NetworkModeSelectionActivity::rowOf(NetworkMode::JOIN_NETWORK) == 0);
  CHECK(NetworkModeSelectionActivity::rowOf(NetworkMode::FONT_STORE) == MODE_COUNT - 1);
  CHECK(MODE_COUNT == 4 + hasUsb + hasWebDav);
  int rows = 0;
  for (int m = -2; m < 12; ++m) rows += NetworkModeSelectionActivity::rowOf(static_cast<NetworkMode>(m)) >= 0;
  CHECK(rows == MODE_COUNT);  // a value that is no mode has no row

  // The mode numbers the host test of resolve() holds to.
  CHECK(static_cast<int>(NetworkMode::JOIN_NETWORK) == 0 && static_cast<int>(NetworkMode::CONNECT_CALIBRE) == 1);
  CHECK(static_cast<int>(NetworkMode::CREATE_HOTSPOT) == 2 && static_cast<int>(NetworkMode::USB_DRIVE) == 3);
  CHECK(static_cast<int>(NetworkMode::WEB_DAV) == 4 && static_cast<int>(NetworkMode::FONT_STORE) == 5);
  CHECK(static_cast<int>(WakeKind::Connect) == 5);

  // 1. The font store, over the session, in any state of its own (the store does not answer: the session below does).
  reset();
  under(session(NetworkMode::FONT_STORE, "/books/dune.epub"));
  top(std::make_unique<Silent>("FontDownload"))->prevent = true;  // downloading
  auto w = sleepNow();
  CHECK(w.kind == WakeKind::Connect && w.row == static_cast<int>(NetworkMode::FONT_STORE) && w.path == "/books/dune.epub");
  activityManager.currentActivity->prevent = false;  // the store list, add-new list, complete, error
  w = sleepNow();
  CHECK(w.kind == WakeKind::Connect && w.row == static_cast<int>(NetworkMode::FONT_STORE));

  // 2. Wi-Fi selection or a keyboard over the store: both keep the screen from sleeping by themselves, neither answers.
  reset();
  under(session(NetworkMode::FONT_STORE));
  under(std::make_unique<Silent>("FontDownload"));
  top(std::make_unique<Silent>("WifiSelection"))->prevent = true;
  w = sleepNow();
  CHECK(w.kind == WakeKind::Connect && w.row == static_cast<int>(NetworkMode::FONT_STORE));

  // 3. The Connections list in front: the row the cursor is on, whatever the session behind it was last in.
  for (int row = 0; row < MODE_COUNT; ++row) {
    reset();
    under(session(NetworkMode::CONNECT_CALIBRE, "/a.epub"));
    auto* list = top(std::make_unique<NetworkModeSelectionActivity>());
    list->returnBook_ = "/a.epub";
    list->nav.selected = row;
    w = sleepNow();
    CHECK(w.kind == WakeKind::Connect && NetworkModeSelectionActivity::rowOf(static_cast<NetworkMode>(w.row)) == row);
    CHECK(w.path == "/a.epub");
  }
  reset();
  auto* wild = top(std::make_unique<NetworkModeSelectionActivity>());
  wild->nav.selected = 99;  // a cursor past the last row (a list that shrank): the last row, not a read past the table
  CHECK(sleepNow().row == static_cast<int>(NetworkMode::FONT_STORE));

  // 4. A session running: the server keeps the screen awake, and that must not hide its answer.
  for (const auto mode : {NetworkMode::JOIN_NETWORK, NetworkMode::CREATE_HOTSPOT, NetworkMode::CONNECT_CALIBRE}) {
    reset();
    top(session(mode, "", /*prevent=*/true));
    w = sleepNow();
    CHECK(w.kind == WakeKind::Connect && w.row == static_cast<int>(mode));
  }
  // ...also under Calibre, which holds the screen awake as well.
  reset();
  under(session(NetworkMode::CONNECT_CALIBRE));
  top(std::make_unique<Silent>("CalibreConnect"))->prevent = true;
  CHECK(sleepNow().row == static_cast<int>(NetworkMode::CONNECT_CALIBRE));

  // 5. WebDAV pull replaces the session: no session under it.
  if (hasWebDav) {
    reset();
    top(std::make_unique<WebDavPullActivity>())->prevent = true;
    w = sleepNow();
    CHECK(w.kind == WakeKind::Connect && w.row == static_cast<int>(NetworkMode::WEB_DAV) && w.path.empty());
  }

  // 6. The screens that answered before still answer, and the screens above them that do not answer change nothing.
  reset();
  under(std::make_unique<Answers>("Home", WakePlace{WakeKind::HomeTab, 0, 4, 2, "", "pin"}));
  top(std::make_unique<Silent>("FontDownload"));  // the store pinned in Favorites: Home is still its parent
  w = sleepNow();
  CHECK(w.kind == WakeKind::HomeTab && w.tab == 4);
  reset();
  under(std::make_unique<Answers>("Reader", WakePlace{WakeKind::Book, 0, 0, 0, "/a.epub", ""}));
  top(std::make_unique<Silent>("KeyboardEntry"))->prevent = true;
  w = sleepNow();
  CHECK(w.kind == WakeKind::Book && w.path == "/a.epub");
  reset();
  under(std::make_unique<Answers>("Settings", WakePlace{WakeKind::SettingsTab, 0, 3, 0, "", "k"}));
  top(std::make_unique<Silent>("WifiSelection"))->prevent = true;
  CHECK(sleepNow().kind == WakeKind::SettingsTab);
  reset();
  top(std::make_unique<Silent>("Whatever"));
  CHECK(sleepNow().kind == WakeKind::None);
  reset();
  SETTINGS.uiShell = 1;
  top(session(NetworkMode::FONT_STORE));
  CHECK(sleepNow().shell == 1);  // the shell the place was kept in
  SETTINGS.uiShell = 0;

  // 7. The book detours.
  reset();
  auto* sync = top(std::make_unique<KOReaderSyncActivity>());
  sync->epubPath = "/books/dune.epub";
  sync->prevent = true;  // syncing
  w = sleepNow();
  CHECK(w.kind == WakeKind::Book && w.path == "/books/dune.epub");
  reset();
  auto* image = top(std::make_unique<BmpViewerActivity>());
  image->filePath = "/books/sf/cover.png";
  w = sleepNow();
  CHECK(w.kind == WakeKind::Files && w.path == "/books/sf" && w.name == "cover.png");
  image->filePath = "/pic.bmp";
  w = sleepNow();
  CHECK(w.kind == WakeKind::Files && w.path == "/" && w.name == "pic.bmp");

  // 8. Settings: an action row has no JSON key and travels as action/N, the number the pins use.
  std::vector<SettingInfo> rowsOfTab{{"sleepTimeoutMinutes", SettingAction::None}, {nullptr, SettingAction::Network},
                                     {nullptr, SettingAction::None}, {nullptr, SettingAction::CustomiseStatusBar}};
  reset();
  auto* st = top(std::make_unique<SettingsActivity>());
  st->currentSettings = &rowsOfTab;
  st->settingsCount = static_cast<int>(rowsOfTab.size());
  st->selectedCategoryIndex = 4;
  const int network = static_cast<int>(SettingAction::Network), bar = static_cast<int>(SettingAction::CustomiseStatusBar);
  st->ring = 1;
  CHECK(sleepNow().name == "sleepTimeoutMinutes");  // a keyed row: as before
  st->ring = 2;
  w = sleepNow();
  CHECK(w.kind == WakeKind::SettingsTab && w.tab == 4 && w.name == "action/" + std::to_string(network));
  st->ring = 3;
  CHECK(sleepNow().name.empty());  // a row with neither key nor action
  st->ring = 4;
  CHECK(sleepNow().name == "action/" + std::to_string(bar));
  st->ring = 0;
  CHECK(sleepNow().name.empty());  // the tab bar

  // 9. The wake. Connections is row 0 of the Home Settings tab: seeded, so Back and the X4 Pro bar have a parent.
  for (const bool touch : {false, true}) {
    for (const auto mode : {NetworkMode::FONT_STORE, NetworkMode::JOIN_NETWORK, NetworkMode::CREATE_HOTSPOT, NetworkMode::CONNECT_CALIBRE,
                            NetworkMode::USB_DRIVE, NetworkMode::WEB_DAV}) {
      if (!boardHas(mode)) continue;
      reset();
      activityManager.mappedInput.touch = touch;
      WakePlace place;
      place.kind = WakeKind::Connect;
      place.row = static_cast<int16_t>(mode);
      place.path = "/books/dune.epub";
      activityManager.goToWakePlace(place, true);
      CHECK(activityManager.went == "transfer");
      CHECK(activityManager.transfer && activityManager.transfer->returnBook == "/books/dune.epub");  // the book it came from
      CHECK(activityManager.transfer->openFontStore == (mode == NetworkMode::FONT_STORE));
      MenuNavigationState home;
      CHECK(activityManager.navigationMemory.load("Home", home));
      CHECK(home.tab == 3 && home.committedTab == 3 && home.cursors[3].selected == (touch ? 0 : 1));
      MenuNavigationState list;
      const bool cursorKept = activityManager.navigationMemory.load("NetworkModeSelection", list);
      if (mode == NetworkMode::FONT_STORE) {
        CHECK(!cursorKept);  // straight into the store: no list
      } else {
        CHECK(cursorKept && list.count == 1);
        CHECK(list.cursors[0].selected == NetworkModeSelectionActivity::rowOf(mode));  // the cursor on that mode's row
      }
    }
  }
  reset();
  WakePlace none;
  none.kind = WakeKind::Connect;
  none.row = static_cast<int16_t>(NetworkMode::JOIN_NETWORK);
  activityManager.goToWakePlace(none, true);
  CHECK(activityManager.transfer && activityManager.transfer->returnBook.empty());  // no book: Back goes Home

  // 10. A file transfer opened from a reader keeps the book it was opened from; a saved book wins over none.
  reset();
  activityManager.reader = true;
  APP_STATE.openEpubPath = "/books/open.epub";
  activityManager.goToFileTransfer();
  CHECK(activityManager.transfer && activityManager.transfer->returnBook == "/books/open.epub");
  reset();
  APP_STATE.openEpubPath = "/books/stale.epub";  // the last book opened, but no reader in front: not a detour
  activityManager.goToFileTransfer();
  CHECK(activityManager.transfer && activityManager.transfer->returnBook.empty());
  reset();
  activityManager.goToFileTransfer(false, "/books/saved.epub");
  CHECK(activityManager.transfer && activityManager.transfer->returnBook == "/books/saved.epub");

  // The other wakes are as they were.
  reset();
  WakePlace files; files.kind = WakeKind::Files; files.path = "/books"; files.name = "a.epub";
  activityManager.goToWakePlace(files, true);
  CHECK(activityManager.went == "files" && activityManager.browserPath == "/books");
  reset();
  WakePlace sett; sett.kind = WakeKind::SettingsTab; sett.tab = 2; sett.name = "action/5";
  activityManager.goToWakePlace(sett, true);
  CHECK(activityManager.went == "settings" && activityManager.settingsTab == 2 && activityManager.settingsKey == "action/5");

  std::printf(failures ? "RED %d failure(s)\n" : "GREEN connect wake\n", failures);
  return failures ? 1 : 0;
}
'''

harness = (harness.replace("@@STRS@@", ", ".join(strs)).replace("@@ICONS@@", ", ".join(icons))
           .replace("@@ENUM_MODE@@", enum_mode).replace("@@ACTION_ENUM@@", action_enum)
           .replace("@@COUNT_BLOCK@@", count_block).replace("@@ROWS_BLOCK@@", rows_block).replace("@@KEP@@", kep)
           .replace("@@ROW_OF@@", row_of).replace("@@NMS_WAKE@@", nms_wake)
           .replace("@@CPWS_WAKE@@", cpws_wake).replace("@@WEBDAV_WAKE@@", webdav_wake)
           .replace("@@KOSYNC_WAKE@@", kosync_wake).replace("@@FOLDER_OF@@", folder_of)
           .replace("@@BMP_WAKE@@", bmp_wake).replace("@@CAPTURE@@", capture)
           .replace("@@TO_PLACE@@", to_place).replace("@@TO_TRANSFER@@", to_transfer)
           .replace("@@SETTINGS_KEY@@", settings_key).replace("@@SETTINGS_WAKE@@", settings_wake))
(out / "connect.cpp").write_text(harness)

failed = 0
boards = (("x3", ["-DFREEINK_DEVICE_X3=1", "-DFREEINK_CAP_USB_MSC=0"]),
          ("x4c-x4pro", ["-DFREEINK_DEVICE_X3=0", "-DFREEINK_CAP_USB_MSC=1"]),
          ("webdav-no-usb", ["-DFREEINK_DEVICE_X3=0", "-DFREEINK_CAP_USB_MSC=0"]))
for name, flags in boards:
    exe = out / f"connect-{name}"
    build = subprocess.run([args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-unused-function",
                            "-fsanitize=address,undefined", *flags, "-I" + str(src), str(out / "connect.cpp"),
                            "-o", str(exe)], capture_output=True, text=True)
    if build.returncode:
        print(f"RED {name}: the production cuts do not build against the stand-ins\n{build.stderr[:3000]}")
        failed = 1
        continue
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    print(f"{name}: {run.stdout.strip()} {run.stderr.strip()[:500]}")
    failed |= run.returncode

# --- what a host stand-in cannot see: the wiring in the production text --------------------------------------------
def need(cond, what):
    global failed
    if not cond:
        print(f"RED {what}")
        failed = 1

need("preventAutoSleep" not in capture, "captureWakePlace must not skip a screen that holds the display awake")
before_early_return = selected[:selected.index("if (mode == NetworkMode::FONT_STORE)")]
need("networkMode = mode;" in before_early_return,
     "onNetworkModeSelected must record the mode before the font store / USB / WebDAV early returns")
need("WakeKind::Connect" in main[main.index("const bool wakeToScreen"):][:300],
     "main.cpp wakeToScreen must include Connect (clean first refresh, deferred state write)")
need("rowOf" in main[main.index("wakeplace::resolve("):][:900],
     "main.cpp must give resolve the board's Connections rows")
need("wakeKey(" in settings_enter, "SettingsActivity::onEnter must find the wake row by wakeKey, action rows included")
need('UiListActivity("NetworkModeSelection"' in nms, "the cursor seed in goToWakePlace uses the list's memory key")
need("returnBook" in cpws[cpws.index("void CrossPointWebServerActivity::chooseMode()"):][:400],
     "the Connections list is given the session's book (it answers wakePlace with it)")
need("wakePlace" not in fontdl_h,
     "the font store is answered by the session under it (one place, and the Favorites pin keeps its Home parent)")
sys.exit(failed)
