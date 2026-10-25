#!/usr/bin/env python3
"""Settings > Wi-Fi ends in a fresh boot's clock sync, not a time zone lookup under Settings' heap.

The production `case SettingAction::Network` block of SettingsActivity.cpp is cut out and compiled against stand-ins:
the Wi-Fi screen it opens (which flag it is built with), the result handler it registers, the radio, the render lock.
Settings leaves the X3 about 25 KB, the lookup's TLS wants about 40 KB: with the automatic time zone on, the screen
must not look it up in place (syncClock off), and once a network is joined the restart is the clock sync's own boot
(startsOnFreshBoot), asked for outside the RenderLock because it takes its own to paint the restart notice.
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[3])
p.add_argument("--cxx", default="c++")
args = p.parse_args()
repo = args.repo.resolve()
text = (repo / "src/activities/settings/SettingsActivity.cpp").read_text()
start = text.find("case SettingAction::Network: {")
if start < 0:
    sys.exit("RED missing in production: case SettingAction::Network")
brace = text.index("{", start)
depth = 0
for i in range(brace, len(text)):
    depth += (text[i] == "{") - (text[i] == "}")
    if depth == 0:
        block = text[brace:i + 1]
        break

harness = r'''
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("RED %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)
#define LOG_ERR(...) do {} while (0)

struct ActivityResult { bool isCancelled = false; };
using Handler = std::function<void(const ActivityResult&)>;
struct GfxRenderer {} renderer;
struct MappedInputManager {} mappedInput;
// The Wi-Fi screen: the flags it is built with.
struct WifiSelectionActivity {
  bool autoConnect, syncClock;
  WifiSelectionActivity(GfxRenderer&, MappedInputManager&, bool a = true, bool s = true) : autoConnect(a), syncClock(s) {}
};
template <typename T, typename... A> std::unique_ptr<T> makeUniqueNoThrow(A&&... a) {
  return std::make_unique<T>(static_cast<A&&>(a)...);
}
struct { int clockAutoTimezone = 1; void saveToFile() {} } SETTINGS;

enum class FreshBootTask { ClockSync, KOReaderLogin, KOReaderSignUp };
static int lockHeld = 0;
struct RenderLock { RenderLock() { ++lockHeld; } ~RenderLock() { --lockHeld; } };
constexpr int WIFI_MODE_NULL = 0, WL_CONNECTED = 3;
struct { int mode = 1; int st = 0; int getMode() const { return mode; } int status() const { return st; } void disconnect(bool) { st = 0; } } WiFi;
static void delay(int) {}

static std::vector<std::string> calls;
static bool freshBootTakesIt = true;
static bool startsOnFreshBoot(bool freshBoot, FreshBootTask task) {
  calls.push_back("fresh:" + std::to_string(freshBoot) + ":" + std::to_string(static_cast<int>(task)) + ":lock" + std::to_string(lockHeld));
  return freshBootTakesIt;
}
static void silentRestartToSettings() { calls.push_back("settings:lock" + std::to_string(lockHeld)); }

static bool built = false, builtSyncClock = true;
static Handler handler;
static void startActivityForResult(std::unique_ptr<WifiSelectionActivity> a, Handler h) {
  built = true;
  builtSyncClock = a->syncClock;
  handler = std::move(h);
}

static void networkRow() {
  switch (0) {
    default:
@@BLOCK@@
  }
}

static void press(int autoTimezone, bool radioUp, bool joined, bool takes = true) {
  SETTINGS.clockAutoTimezone = autoTimezone;
  WiFi.mode = radioUp ? 1 : WIFI_MODE_NULL;
  WiFi.st = joined ? WL_CONNECTED : 0;
  freshBootTakesIt = takes;
  calls.clear();
  built = false;
  networkRow();
  CHECK(built);
  handler(ActivityResult{});
}
using V = std::vector<std::string>;

int main() {
  // Automatic time zone on: the screen skips its own lookup; a joined network restarts into the clock sync's boot,
  // asked for outside the lock.
  press(1, true, true);
  CHECK(!builtSyncClock);
  CHECK(calls == (V{"fresh:0:0:lock0"}));
  // Automatic time zone off: NTP alone (no TLS) stays on the screen, as before; the restart is Settings' own.
  press(0, true, true);
  CHECK(builtSyncClock);
  CHECK(calls == (V{"settings:lock1"}));
  // Not joined (cancelled or failed), the scan left the radio up: no clock sync, Settings' own restart.
  press(1, true, false);
  CHECK(calls == (V{"settings:lock1"}));
  // The radio never came up: nothing restarts.
  press(1, false, false);
  CHECK(calls.empty());
  // The fresh boot was not taken (the simulator): fall back to Settings' restart.
  press(1, true, true, false);
  CHECK(calls == (V{"fresh:0:0:lock0", "settings:lock1"}));
  std::printf("%s\n", failures ? "RED" : "GREEN: Settings > Wi-Fi hands the time zone lookup to a fresh boot");
  return failures ? 1 : 0;
}
'''.replace("@@BLOCK@@", block)
with tempfile.TemporaryDirectory(prefix="wifi-row-") as tmp:
    cpp = Path(tmp) / "row.cpp"
    exe = Path(tmp) / "row"
    cpp.write_text(harness)
    subprocess.run([args.cxx, "-std=c++20", "-Wall", "-Wno-unused-function", str(cpp), "-o", str(exe)], check=True)
    sys.exit(subprocess.run([str(exe)]).returncode)
