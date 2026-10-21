import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--repo", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
repo = args.repo.resolve()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
settings = (repo / "src/CrossPointSettings.h").read_text()
main = (repo / "src/main.cpp").read_text()
wake_fields = settings[settings.index("  enum WAKE_TARGET"):settings.index("  uint8_t wakeTarget = DEFAULT_WAKE_TARGET;") + len("  uint8_t wakeTarget = DEFAULT_WAKE_TARGET;")]
fields = settings[settings.index("  enum UGLY_START_SCREEN"):settings.index("  // Whether a power-button wake")]
boot = main[main.index("  bool normalColdBoot ="):main.index("  setupDisplayAndFonts(resume != BootResume::Splash")]
home_start = main.index("    const HomeMenuItem startHome =")
home = main[home_start:main.index(";", home_start) + 1]
harness = r'''
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "util/WakePlace.h"
struct CrossPointSettings {
  uint8_t uiShell = 1;
@@WAKE_FIELDS@@
@@FIELDS@@
};
CrossPointSettings SETTINGS;
namespace shell { bool isUgly() { return SETTINGS.uiShell == 1; } }
enum Reset { ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_DEEPSLEEP, ESP_RST_PANIC, ESP_RST_CPU_LOCKUP,
             ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_BROWNOUT, ESP_RST_SW, ESP_RST_UNKNOWN };
Reset currentReset;
Reset esp_reset_reason() { return currentReset; }
struct Book { std::string path; };
struct { std::string openEpubPath = "/book.epub"; WakePlace wake; } APP_STATE;
struct {
  std::vector<Book> books{{"/book.epub"}};
  const std::vector<Book>& getBooks() const { return books; }
} RECENT_BOOKS;
struct Dir { bool isDirectory() const { return true; } explicit operator bool() const { return true; } };
struct { bool exists(const char*) const { return true; } Dir open(const char*) const { return {}; } } Storage;
enum class HomeMenuItem { RECENTS, DESK, RECENT_CONTINUE };
struct Decision { uint8_t target; bool opensBook; bool opensPlace; HomeMenuItem home; };
Decision decide(bool isSleepWake, bool recoveryFirmwareMode, bool rebootedFromPanic,
                bool updateBoot, bool isSilentReboot) {
@@BOOT@@
@@HOME@@
  return {wakeTarget, wakeToBook, wakePlace.kind != WakeKind::None, startHome};
}
void require(bool condition, const char* name) {
  if (!condition) { std::fprintf(stderr, "RED %s\n", name); std::exit(1); }
}
int main() {
  struct Context { const char* name; Reset reset; bool wake, recovery, panic, ota, silent, allowed; };
  const Context contexts[] = {
    {"power-on", ESP_RST_POWERON, false, false, false, false, false, true},
    {"external-reset", ESP_RST_EXT, false, false, false, false, false, true},
    {"power-wake", ESP_RST_DEEPSLEEP, true, false, false, false, false, true},
    {"cold-panic", ESP_RST_PANIC, false, false, true, false, false, false},
    {"cold-lockup", ESP_RST_CPU_LOCKUP, false, false, true, false, false, false},
    {"interrupt-watchdog", ESP_RST_INT_WDT, false, false, false, false, false, false},
    {"task-watchdog", ESP_RST_TASK_WDT, false, false, false, false, false, false},
    {"watchdog", ESP_RST_WDT, false, false, false, false, false, false},
    {"brownout", ESP_RST_BROWNOUT, false, false, false, false, false, false},
    {"after-ota", ESP_RST_SW, false, false, false, false, false, false},
    {"unknown-reset", ESP_RST_UNKNOWN, false, false, false, false, false, false},
    {"recovery", ESP_RST_POWERON, false, true, false, false, false, false},
    {"ota-target", ESP_RST_POWERON, false, false, false, true, false, false},
    {"silent-target", ESP_RST_POWERON, false, false, false, false, true, false},
    {"wake-recovery", ESP_RST_DEEPSLEEP, true, true, false, false, false, false},
    {"wake-panic", ESP_RST_DEEPSLEEP, true, false, true, false, false, false},
  };
  int checks = 0;
  for (const auto& context : contexts) {
    currentReset = context.reset;
    for (uint8_t root = 0; root < CrossPointSettings::UGLY_ROOT_SCREEN_COUNT; ++root) {
      SETTINGS.uglyRootScreen = root;
      for (const uint8_t choice : {0, 1, 2, 3}) {
        SETTINGS.wakeTarget = choice;
        // The place the last sleep kept: a book that is still in Recent and on the card.
        APP_STATE.wake = WakePlace{WakeKind::Book, SETTINGS.uiShell, 0, 0, "/book.epub", ""};
        const auto result = decide(context.wake, context.recovery, context.panic, context.ota, context.silent);
        const uint8_t expected = context.allowed ? choice : CrossPointSettings::WAKE_HOME;
        require(result.target == expected, context.name);
        // Resume reopens only out of deep sleep, whatever else looks like a wake.
        const bool resumes = expected == CrossPointSettings::WAKE_RESUME && context.wake && context.reset == ESP_RST_DEEPSLEEP;
        require(result.opensPlace == resumes, context.name);
        require(result.opensBook == (expected == CrossPointSettings::WAKE_BOOK || resumes), context.name);
        require(APP_STATE.wake.kind == WakeKind::None, "the place is spent on every start");
        const auto expectedHome = expected == CrossPointSettings::WAKE_RECENT ? HomeMenuItem::RECENTS
                                : HomeMenuItem::RECENT_CONTINUE;
        require(result.home == expectedHome, context.name);
        require(SETTINGS.uglyRootIsDesk() == (root == CrossPointSettings::UGLY_ROOT_DESK), context.name);
        ++checks;
      }
    }
  }
  SETTINGS.uiShell = 0;
  SETTINGS.wakeTarget = CrossPointSettings::WAKE_BOOK;
  currentReset = ESP_RST_POWERON;
  require(!decide(false, false, false, false, false).opensBook, "cross cold with wake into the book on");
  currentReset = ESP_RST_DEEPSLEEP;
  require(decide(true, false, false, false, false).opensBook, "cross sleep wake still opens the book");
  // Resume on cross: a watchdog or brownout that reads as a power-button wake does not reopen the place.
  SETTINGS.wakeTarget = CrossPointSettings::WAKE_RESUME;
  APP_STATE.wake = WakePlace{WakeKind::Book, 0, 0, 0, "/book.epub", ""};
  currentReset = ESP_RST_BROWNOUT;
  require(!decide(true, false, false, false, false).opensPlace, "cross resume after a brownout");
  APP_STATE.wake = WakePlace{WakeKind::Book, 0, 0, 0, "/book.epub", ""};
  currentReset = ESP_RST_DEEPSLEEP;
  require(decide(true, false, false, false, false).opensPlace, "cross resume out of deep sleep");
  std::printf("GREEN production boot guard: %d ugly contexts, 2 cross contexts\n", checks);
}
'''
harness = (harness.replace("@@WAKE_FIELDS@@", wake_fields).replace("@@FIELDS@@", fields)
           .replace("@@BOOT@@", boot).replace("@@HOME@@", home))
(output / "boot.cpp").write_text(harness)
command = [os.environ.get("CXX", "c++"), "-std=c++20", "-fsanitize=address,undefined",
           "-I" + str(repo / "src"), str(output / "boot.cpp"), "-o", str(output / "boot")]
subprocess.run(command, check=True)
subprocess.run([str(output / "boot")], check=True)
