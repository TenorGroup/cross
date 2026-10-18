// X3 boot: a card that does not answer gets its rail cut and is tried again, at most twice more.
#include <Logging.h>

#include <BoardConfig.h>
#include <HwLog.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "../../lib/hal/HalStorage.h"

static int failures = 0;
#define CHECK(cond)                                                \
  do {                                                             \
    if (!(cond)) {                                                 \
      ++failures;                                                  \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
    }                                                              \
  } while (0)

using Log = std::vector<std::string>;

static bool beginAfter(const BoardConfig::BoardProfile& profile, int failingBegins) {
  BoardConfig::ACTIVE = profile;
  SDCardManager::beginFailures = failingBegins;
  hwLog.clear();
  traceLines.clear();
  return HalStorage::getInstance().begin();
}

static long count(const Log& log, const std::string& entry) { return std::count(log.begin(), log.end(), entry); }
static long at(const Log& log, const std::string& entry, size_t from = 0) {
  for (size_t i = from; i < log.size(); ++i)
    if (log[i] == entry) return static_cast<long>(i);
  return -1;
}
static bool touches(const Log& log, int pin) {
  const std::string p = " " + std::to_string(pin) + " ";
  for (const auto& e : log)
    if ((e.rfind("mode ", 0) == 0 || e.rfind("write ", 0) == 0) && e.find(p) != std::string::npos) return true;
  return false;
}

// One power cycle starting at `from`: SPI released, bus LOW, rail off for offMs, bus back to
// INPUT, rail on, then the next begin. Returns the index of that begin, or -1.
static long checkCycle(const Log& log, size_t from, int offMs) {
  const long end = at(log, "SPI.end", from);
  const long railOff = at(log, "write 13 LOW", from);
  const long wait = at(log, "delay " + std::to_string(offMs), from);
  const long railOn = at(log, "write 13 HIGH", from);
  long next = at(log, "begin ok", from);
  const long failed = at(log, "begin fail", from);
  if (next < 0 || (failed >= 0 && failed < next)) next = failed;
  CHECK(end >= 0 && railOff > end && wait > railOff && railOn > wait && next > railOn);
  if (end < 0 || railOn < 0 || next < 0) return -1;
  for (int pin : {7, 8, 10, 12}) {
    const std::string n = std::to_string(pin);
    const long out = at(log, "mode " + n + " OUTPUT", end);
    const long low = at(log, "write " + n + " LOW", end);
    const long in = at(log, "mode " + n + " INPUT", end);
    CHECK(out > end && low > out && low < railOff);  // LOW before the rail goes off
    CHECK(in > wait && in < railOn);                 // released before the rail comes back
    // Nothing touches a bus pin before SPI.end() in this cycle.
    for (long i = static_cast<long>(from); i < end; ++i) CHECK(log[i].find(" " + n + " ") == std::string::npos);
  }
  return next;
}

int main() {
  {  // Healthy card: the boot path is exactly what it was.
    CHECK(beginAfter(BoardConfig::X3, 0));
    CHECK(hwLog == Log{"begin ok"});
    CHECK(traceLines.empty());
  }
  {  // One failed begin: one 250 ms cycle, second try succeeds.
    CHECK(beginAfter(BoardConfig::X3, 1));
    CHECK(count(hwLog, "begin fail") == 1 && count(hwLog, "begin ok") == 1);
    CHECK(checkCycle(hwLog, 1, 250) >= 0);
    CHECK(count(hwLog, "SPI.end") == 1);
    CHECK(!touches(hwLog, 21));
    CHECK(traceLines == Log{"[SD] SD_RECOVER attempt=1 ok=1\n"});
  }
  {  // Two failed begins: 250 ms, then 1000 ms.
    CHECK(beginAfter(BoardConfig::X3, 2));
    const long second = checkCycle(hwLog, 1, 250);
    CHECK(second >= 0 && checkCycle(hwLog, second + 1, 1000) >= 0);
    CHECK(count(hwLog, "begin ok") == 1);
    CHECK((traceLines == Log{"[SD] SD_RECOVER attempt=1 ok=0\n", "[SD] SD_RECOVER attempt=2 ok=1\n"}));
  }
  {  // Dead card: three begins in all, then the error screen.
    BoardConfig::BoardProfile uc8279 = BoardConfig::X3;
    uc8279.board = BoardConfig::Board::XteinkX3Uc8279;
    CHECK(!beginAfter(uc8279, 100));
    CHECK(count(hwLog, "begin fail") == 3);
    CHECK(count(hwLog, "write 13 LOW") == 2 && count(hwLog, "write 13 HIGH") == 2);
    CHECK(traceLines.size() == 2);
    CHECK(!touches(hwLog, 21));
  }
  {  // X4: GPIO13 is the battery latch. One try, no pin touched.
    CHECK(!beginAfter(BoardConfig::X4, 100));
    CHECK(hwLog == Log{"begin fail"});
    CHECK(!touches(hwLog, 13));
    CHECK(traceLines.empty());
  }
  {  // An X3-shaped profile on another board is left alone too.
    BoardConfig::BoardProfile other = BoardConfig::X4;
    other.sd.powerEnable = 13;
    CHECK(!beginAfter(other, 100));
    CHECK(hwLog == Log{"begin fail"});
  }
  std::printf("boot_cycle_test: %s (%d failed checks)\n", failures ? "FAIL" : "PASS", failures);
  return failures ? 1 : 0;
}
