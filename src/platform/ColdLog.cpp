#include "ColdLog.h"

#if TENOR_COLD_LOG

#include <HalStorage.h>
#include <esp_system.h>

#include <string>

#include "ColdScript.h"

namespace coldlog {
namespace {
constexpr char SCRIPT[] = "/x4pro/lenh.txt";
constexpr char LOG_DIR[] = "/x4pro/log";
constexpr char LOG_FILE[] = "/x4pro/log/nhat-ky.txt";

// Boots counted since power came on (RTC memory survives restarts, crashes and deep sleep).
RTC_NOINIT_ATTR uint32_t bootMagic;
RTC_NOINIT_ATTR uint32_t bootCount;
constexpr uint32_t BOOT_MAGIC = 0x434F4C44;  // "COLD"

uint32_t bootNumber() {
  static uint32_t number = 0;
  if (number == 0) {
    if (bootMagic != BOOT_MAGIC) bootCount = 0;
    bootMagic = BOOT_MAGIC;
    number = ++bootCount;
  }
  return number;
}

const char* resetName() {
  static const char* const NAMES[] = {"UNKNOWN", "POWERON",  "EXT",       "SW",       "PANIC",
                                      "INT_WDT", "TASK_WDT", "WDT",       "DEEPSLEEP", "BROWNOUT"};
  const unsigned reason = static_cast<unsigned>(esp_reset_reason());
  return reason < sizeof(NAMES) / sizeof(NAMES[0]) ? NAMES[reason] : "OTHER";
}

// The card as ColdScript.h needs it.
struct Card {
  bool exists(const char* path) { return Storage.exists(path); }
  bool remove(const char* path) { return Storage.remove(path); }
  bool rename(const char* from, const char* to) { return Storage.rename(from, to); }
  bool read(const char* path, std::string& out) {
    HalFile file;
    if (!Storage.openFileForRead("COLD", path, file)) return false;
    const size_t size = file.size();
    if (size > 16384) return false;
    out.resize(size);
    return file.read(out.data(), size) == static_cast<int>(size);
  }
  bool write(const char* path, const std::string& text) {
    HalFile file;
    if (!Storage.openFileForWrite("COLD", path, file)) return false;
    const bool written = file.write(text.data(), text.size()) == text.size();
    return file.close() && written;
  }
};
}  // namespace

void flush(const char* why) {
  if (!Storage.ready()) return;
  size_t len = 0, dropped = 0;
  const char* data = probeLogPending(len, dropped);
  if (len == 0 && dropped == 0) return;
  static bool dirMade = false;
  if (!dirMade) dirMade = Storage.ensureDirectoryExists("/x4pro") && Storage.ensureDirectoryExists(LOG_DIR);
  HalFile file = Storage.open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND);
  if (!file) return;
  char head[128];
  const int headLen = snprintf(head, sizeof(head), "=== boot=%lu reset=%s ms=%lu at=%s dropped=%u ===\n",
                               static_cast<unsigned long>(bootNumber()), resetName(), millis(), why,
                               static_cast<unsigned>(dropped));
  const bool written = file.write(head, headLen) == static_cast<size_t>(headLen) && file.write(data, len) == len;
  if (file.close() && written) probeLogConsume(len);
}

String nextLine(const bool firstFrameUp) {
  static bool started = false, finished = false, waiting = false;
  static uint32_t waitUntil = 0;
  if (!firstFrameUp || finished) return String();
  if (waiting && !cold_script::waitOver(millis(), waitUntil)) return String();
  waiting = false;
  // The boot's lines the first time, then what the last command or wait printed.
  flush(started ? "step" : "boot");
  if (!started) {
    started = true;
    // Every ESP.restart (SD_FLASH, ROLLBACK_TEST, the restart after USB Drive) flushes first.
    esp_register_shutdown_handler([] { flush("restart"); });
  }

  Card card;
  std::string line;
  switch (cold_script::take(card, SCRIPT, line)) {
    case cold_script::Take::Done:
      finished = true;
      return String();
    case cold_script::Take::Failed:
      finished = true;
      LOG_ERR("COLD", "Script %s not read or not committed, stopped", SCRIPT);
      flush("stop");
      return String();
    case cold_script::Take::Line:
      break;
  }
  LOG_INF("COLD", "Run %s", line.c_str());
  uint32_t ms = 0;
  if (cold_script::parseWait(line, ms)) {
    waiting = true;
    waitUntil = millis() + ms;
    return String();
  }
  return String(line.c_str());
}

}  // namespace coldlog

#endif
