#!/usr/bin/env python3
"""The radio the plugin events brought up at sleep entry is kept for the KOReader push that is owed.

D12: the plugin sleep.enter delivery and the automatic KOReader sync share one radio-up. enterDeepSleep() takes the
radio down after the sleep screen; when the push still has books to send and a join is up, that teardown would make
pushBeforeSleep() join again (a second connect of up to 10 s). The production tearDownSleepWifi() in src/main.cpp is
cut out and compiled against a radio and a push that answer as each case says.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

repo = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
main = (repo / "src/main.cpp").read_text()
signature = "static void tearDownSleepWifi("
start = main.find(signature)
if start < 0:
    sys.exit("RED missing in production: tearDownSleepWifi")
brace = main.index("{", start)
depth = 0
for i in range(brace, len(main)):
    depth += (main[i] == "{") - (main[i] == "}")
    if depth == 0:
        function = main[start:i + 1]
        break

harness = r'''
#include <cstdio>
constexpr int WIFI_MODE_NULL = 0, WIFI_STA = 1, WIFI_OFF = 0, WL_CONNECTED = 3;
struct { int mode = 0; int st = 0; int offs = 0;
  int getMode() const { return mode; } int status() const { return st; }
  void disconnect(bool) {} void setMode(int m) { mode = m; if (m == WIFI_OFF) ++offs; } } WiFi_;
struct WiFiProxy { int getMode() const { return WiFi_.getMode(); } int status() const { return WiFi_.status(); }
  void disconnect(bool b) { WiFi_.disconnect(b); } void mode(int m) { WiFi_.setMode(m); } } WiFi;
namespace koautosync { static bool owed = false; static bool pushIsOwed() { return owed; } }

@@FUNCTION@@

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("RED %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)
static bool offAfter(int mode, int status, bool owed) {
  WiFi_ = {}; WiFi_.mode = mode; WiFi_.st = status; koautosync::owed = owed;
  tearDownSleepWifi();
  return WiFi_.offs > 0;
}
int main() {
  CHECK(!offAfter(WIFI_MODE_NULL, 0, true));          // the radio was never up: nothing to do
  CHECK(!offAfter(WIFI_STA, WL_CONNECTED, true));     // joined for the plugin events and the push is owed: kept for it
  CHECK(offAfter(WIFI_STA, WL_CONNECTED, false));     // joined, nothing owed: down before the panel and the card
  CHECK(offAfter(WIFI_STA, 0, true));                 // up but not joined: nothing to reuse, the push joins itself
  CHECK(offAfter(WIFI_STA, 0, false));
  std::printf("%s\n", failures ? "RED" : "GREEN: the sleep radio stays up only for a push that is owed");
  return failures ? 1 : 0;
}
'''.replace("@@FUNCTION@@", function)
with tempfile.TemporaryDirectory(prefix="sleep-wifi-") as tmp:
    cpp = Path(tmp) / "w.cpp"
    exe = Path(tmp) / "w"
    cpp.write_text(harness)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wno-unused-function", str(cpp), "-o", str(exe)], check=True)
    sys.exit(subprocess.run([str(exe)]).returncode)
