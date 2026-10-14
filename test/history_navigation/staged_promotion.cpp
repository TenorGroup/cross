// A save cut at the last rename leaves the whole new snapshot in .tmp and the main file missing. The next save
// must not truncate that .tmp before it is safe: if it then fails (card full), the hours the cut save had on the
// card are gone for good. They are promoted to the main file first.
#define STATS_SCHEMA_HELPERS_ONLY
#include "schema_preservation.cpp"
#include "ReadingStatsStore.cpp"
#include "util/LichDai.h"

int main() {
  seed();
  const std::string path = ReadingStatsStore::getFilePath();
  halClock.localSet = true;
  halClock.local.tm_hour = 8;
  halClock.local.tm_min = 1;
  READING_STATS.record(20261014, 60000, 1, 10);
  check(READING_STATS.saveToFile(), "initial save");
  READING_STATS.record(20261014, 60000, 1, 10);
  storageFault = {StorageFaultOperation::Rename, path + ".tmp", path};
  check(!READING_STATS.saveToFile(), "the final rename is cut");
  readinghistory::Snapshot history;
  storageFault = {};
  check(READING_STATS.loadHistory(history), "the complete .tmp is read");
  check(history.hourMs[8] == 120000, "the cut save holds 120000 ms of hour 8");
  storageFault = {StorageFaultOperation::Write, path + ".tmp"};
  check(!READING_STATS.saveToFile(), "the card is full at the next save");
  storageFault = {};
  check(READING_STATS.loadHistory(history), "read after the failed save");
  std::cout << "after the failed save hour8_ms=" << history.hourMs[8] << '\n';
  check(history.hourMs[8] == 120000, "the hours of the cut save are still on the card");
  check(READING_STATS.saveToFile(), "save once the card takes writes");
  check(READING_STATS.loadFromFile(), "reload");
  check(READING_STATS.loadHistory(history), "read the final file");
  check(history.hourMs[8] == 120000, "no hour counted twice or lost");
  std::filesystem::remove_all(fixtureRoot);
  std::cout << "RESULT " << checks - failures << '/' << checks << " assertions passed\n";
  return failures ? 1 : 0;
}
