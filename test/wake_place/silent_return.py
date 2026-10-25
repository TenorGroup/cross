#!/usr/bin/env python3
"""A silent restart that leaves a Settings tab comes back to that tab, and Back from the task lands there.

The production functions of src/main.cpp are cut out and compiled against stand-ins for the screens around them:
armSilentReboot / silentRestartTo / silentRestart / silentRestartToSettings / startsOnFreshBoot (the way out) and
openSettingsAt / openFreshBootTask (the way back in), with the RTC variables as main.cpp declares them. The host plays
the reboot: it reads the RTC variables the way setup() does and calls the routing the target names.
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
p.add_argument("--cxx", default="c++")
args = p.parse_args()
repo = args.repo.resolve()
main = (repo / "src/main.cpp").read_text()


def cut(text, signature):
    start = text.find(signature)
    if start < 0:
        sys.exit(f"RED missing in production: {signature}")
    brace = text.index("{", start)
    depth = 0
    for i in range(brace, len(text)):
        depth += (text[i] == "{") - (text[i] == "}")
        if depth == 0:
            return text[start:i + 1] + "\n"
    sys.exit(f"unterminated: {signature}")


def between(text, first, last):
    start = text.index(first)
    return text[start:text.index(last, start) + len(last)] + "\n"


rtc = between(main, "RTC_NOINIT_ATTR uint32_t silentRebootMagic;", "RTC_NOINIT_ATTR update_boot::Slot otaBootSlot;")
consts = between(main, "constexpr uint32_t SILENT_REBOOT_MAGIC", "SILENT_REBOOT_TARGET_MAX = SILENT_REBOOT_TARGET_JOIN_NETWORK;")
functions = "".join(cut(main, s) for s in (
    "static void armSilentReboot(", "static void silentRestartTo(", "void silentRestart() {", "void silentRestartToSettings() {",
    "bool startsOnFreshBoot(", "static void openSettingsAt(", "static void openFreshBootTask(", "void silentRestartToJoinNetwork(",
    "static void openJoinNetwork("))

harness = r'''
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "SilentRestart.h"
#include "util/WakePlace.h"
#if __has_include("util/WakeReturn.h")
#include "util/WakeReturn.h"
#define HAVE_RETURN 1
#endif

#define RTC_NOINIT_ATTR
#define LOG_DBG(...) do {} while (0)
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("RED %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

namespace update_boot { struct Slot {}; }
namespace cardrescue { constexpr uint32_t PAYLOAD_LIGHT_ON = 1; inline uint32_t payload(bool on) { return on ? 1u : 0u; } }
@@CONSTS@@
@@RTC@@

struct Activity { virtual ~Activity() = default; };
struct GfxRenderer {} renderer;
struct MappedInputManager {} mappedInputManager;
struct ClockSyncActivity : Activity {
  bool fresh;
  ClockSyncActivity(GfxRenderer&, MappedInputManager&, bool f) : fresh(f) {}
};
struct KOReaderAuthActivity : Activity {
  enum class Mode { AUTHENTICATE, SIGN_UP };
  Mode mode;
  bool fresh;
  KOReaderAuthActivity(GfxRenderer&, MappedInputManager&, Mode m, bool f) : mode(m), fresh(f) {}
};
template <typename T, typename... A> std::unique_ptr<T> makeUniqueNoThrow(A&&... a) {
  return std::make_unique<T>(static_cast<A&&>(a)...);
}
enum class HomeMenuItem { NONE, RECENTS };

// What the screens around the restart answer, and what the host's reboot asks of them.
struct Manager {
  WakePlace below;       // what captureWakePlace() finds in the stack
  bool popping = false;  // Back is on its way (a Pop pending), not a Replace such as the home gesture
  std::vector<std::string> calls;
  WakePlace captureWakePlace() const { return below; }
  bool isPopping() const { return popping; }
  HomeMenuItem homeMenuOrigin() const { return HomeMenuItem::RECENTS; }
  void goToWakePlace(const WakePlace& p, bool) { calls.push_back("wake:" + std::to_string(p.tab) + ":" + p.name); }
  void goToSettings() { calls.push_back("settings"); }
  void goToJoinNetwork(const std::string& book = {}) { calls.push_back("join:" + book); }
  void goHome(HomeMenuItem = HomeMenuItem::NONE) { calls.push_back("home"); }
  static std::string describe(Activity* a) {
    if (auto* c = dynamic_cast<ClockSyncActivity*>(a)) return std::string("ClockSync") + (c->fresh ? ":fresh" : "");
    if (auto* k = dynamic_cast<KOReaderAuthActivity*>(a))
      return std::string(k->mode == KOReaderAuthActivity::Mode::SIGN_UP ? "SignUp" : "Login") + (k->fresh ? ":fresh" : "");
    return "?";
  }
  void replaceActivity(std::unique_ptr<Activity>&& a) { calls.push_back("replace:" + describe(a.get())); }
  void pushActivity(std::unique_ptr<Activity>&& a) { calls.push_back("push:" + describe(a.get())); }
} activityManager;

struct Frontlight_ { bool isOn() const { return false; } } Frontlight;
struct { std::string openEpubPath = "/books/a.epub"; } APP_STATE;
struct { void drawPopup(GfxRenderer&, const char*) {} } GUI;
enum { STR_LOADING_POPUP };
static const char* tr(int) { return "Loading"; }
struct WiFi_ { int status() const { return 0; } } WiFi;
constexpr int WL_CONNECTED = 3;
struct RenderLock {};
struct Esp_ { int restarts = 0; void restart() { ++restarts; } } ESP;
static bool deepSleepInProgress = false;
namespace koautosync { inline bool pullRestartReady() { return true; } inline bool pullIsArmed() { return false; } }
static void showRestartNotice() {}
static void delay(int) {}
struct Storage_ { void prepareForDeepSleep() {} } Storage;
static bool probeKeptHeap() { return false; }

@@FUNCTIONS@@

// The reboot, as setup() plays it: read the RTC variables, clear them, route by the target.
static std::vector<std::string> reboot() {
  const bool silent = silentRebootMagic == SILENT_REBOOT_MAGIC;
  const uint32_t target = silent ? silentRebootTarget : 0;
#ifdef HAVE_RETURN
  const WakePlace place = silent ? wakereturn::load(silentRebootPlace) : WakePlace{};
#else
  const WakePlace place{};
#endif
  silentRebootMagic = 0;
  activityManager.calls.clear();
  if (target >= SILENT_REBOOT_TARGET_TASK && target <= SILENT_REBOOT_TARGET_TASK_LAST)
    openFreshBootTask(static_cast<FreshBootTask>(target - SILENT_REBOOT_TARGET_TASK), place);
  else if (target == SILENT_REBOOT_TARGET_SETTINGS)
    openSettingsAt(place);
  else if (target == SILENT_REBOOT_TARGET_JOIN_NETWORK)
    openJoinNetwork(silentRebootFromBook != 0);
  else
    activityManager.goHome();
  return activityManager.calls;
}

static WakePlace settings(int tab, const char* key) { return WakePlace{WakeKind::SettingsTab, 0, static_cast<int16_t>(tab), 0, "", key}; }
static void scene(const WakePlace& below, bool popping) {
  activityManager.below = below;
  activityManager.popping = popping;
  ESP.restarts = 0;
  silentRebootMagic = 0xDEADBEEF;  // a cold boot's garbage, until a restart is armed
}
using V = std::vector<std::string>;

int main() {
  // 1. Wi-Fi task opened under Settings: the restart carries the tab; the fresh boot puts the task over that tab.
  scene(settings(4, "clockAutoTimezone"), false);
  CHECK(startsOnFreshBoot(false, FreshBootTask::ClockSync));
  CHECK(ESP.restarts == 1);
  CHECK(reboot() == (V{"wake:4:clockAutoTimezone", "push:ClockSync:fresh"}));

  // 2. Back out of that task (a Pop): the restart goes to the same Settings tab and row, not Home.
  scene(settings(4, "clockAutoTimezone"), true);
  silentRestart();
  CHECK(ESP.restarts == 1);
  CHECK(reboot() == (V{"wake:4:clockAutoTimezone"}));

  // 3. Left by the home gesture (a Replace): Home, as before.
  scene(settings(4, "clockAutoTimezone"), false);
  silentRestart();
  CHECK(reboot() == (V{"home"}));

  // 4. The Wi-Fi row's own restart: the Settings tab it was left on, not the first.
  scene(settings(2, "action/7"), false);
  silentRestartToSettings();
  CHECK(reboot() == (V{"wake:2:action/7"}));

  // 5. Nothing of Settings in the stack: the task replaces, Settings opens on the first tab, Home as before.
  scene(WakePlace{WakeKind::HomeTab, 0, 1, 0, "", ""}, true);
  CHECK(startsOnFreshBoot(false, FreshBootTask::KOReaderSignUp));
  CHECK(reboot() == (V{"replace:SignUp:fresh"}));
  scene(WakePlace{}, false);
  silentRestartToSettings();
  CHECK(reboot() == (V{"settings"}));
  scene(WakePlace{WakeKind::Files, 0, 0, 0, "/books", "a.epub"}, true);
  silentRestart();
  CHECK(reboot() == (V{"home"}));

  // 6. The KOReader login carries the tab too.
  scene(settings(5, "action/3"), false);
  CHECK(startsOnFreshBoot(false, FreshBootTask::KOReaderLogin));
  CHECK(reboot() == (V{"wake:5:action/3", "push:Login:fresh"}));

  // 7. A place is spent by its boot: a later restart from Home does not find the old tab.
  scene(WakePlace{}, true);
  silentRestart();
  CHECK(reboot() == (V{"home"}));

  // 8. Join Network (a restart on the way in, the button boards): entered from a book, its exit is that book; entered
  // from Connections, nothing to hand back.
  scene(WakePlace{}, false);
  silentRestartToJoinNetwork(true);
  CHECK(ESP.restarts == 1);
  CHECK(reboot() == (V{"join:/books/a.epub"}));
  scene(WakePlace{}, false);
  silentRestartToJoinNetwork(false);
  CHECK(reboot() == (V{"join:"}));
  scene(WakePlace{}, false);
  silentRestartToJoinNetwork();
  CHECK(reboot() == (V{"join:"}));

  std::printf("%s\n", failures ? "RED" : "GREEN: a silent restart comes back to the Settings tab it left");
  return failures ? 1 : 0;
}
'''
harness = (harness.replace("@@CONSTS@@", consts).replace("@@RTC@@", rtc).replace("@@FUNCTIONS@@", functions))
with tempfile.TemporaryDirectory(prefix="silent-return-") as tmp:
    cpp = Path(tmp) / "silent.cpp"
    exe = Path(tmp) / "silent"
    cpp.write_text(harness)
    subprocess.run([args.cxx, "-std=c++20", "-Wall", "-Wno-unused-function", "-Wno-unused-const-variable", "-I" + str(repo / "src"), str(cpp), "-o", str(exe)],
                   check=True)
    sys.exit(subprocess.run([str(exe)]).returncode)
