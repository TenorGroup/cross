// The long reading calendar: RAM keeps the 30 newest days, the card keeps 90, and a save never
// drops the older ones. The heap a save takes is counted here: ArduinoJson allocates with malloc, strings
// (String on the device) with operator new.
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

namespace jsonheap {
size_t live = 0, peak = 0;
struct Head {
  size_t size, pad;
};
void* take(const size_t size) {
  auto* head = static_cast<Head*>(std::malloc(sizeof(Head) + size));
  if (!head) return nullptr;
  head->size = size;
  peak = std::max(peak, live += size);
  return head + 1;
}
void give(void* p) {
  if (!p) return;
  auto* head = static_cast<Head*>(p) - 1;
  live -= head->size;
  std::free(head);
}
void* resize(void* p, const size_t size) {
  if (!p) return take(size);
  void* next = take(size);
  if (next) std::copy_n(static_cast<char*>(p), std::min(size, (static_cast<Head*>(p) - 1)->size), static_cast<char*>(next));
  if (next) give(p);
  return next;
}
}  // namespace jsonheap
void* operator new(const size_t size) {
  if (void* p = jsonheap::take(size)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { jsonheap::give(p); }
void operator delete(void* p, size_t) noexcept { jsonheap::give(p); }
#define malloc(size) jsonheap::take(size)
#define free(p) jsonheap::give(p)
#define realloc(p, size) jsonheap::resize(p, size)
#include <ArduinoJson.h>
#undef malloc
#undef free
#undef realloc

#define STATS_SCHEMA_HELPERS_ONLY
#include "schema_preservation.cpp"
#include "ReadingStatsStore.cpp"
#include "util/ReadingHistory.h"

// HISTORY_BASE builds this test on a store without the long calendar, to watch it fail there.
#ifdef HISTORY_BASE
#define LOAD_HISTORY(history) false
#else
#define LOAD_HISTORY(history) READING_STATS.loadHistory(history)
#endif

namespace {
const std::string MAIN = ReadingStatsStore::getFilePath();

JsonDocument savedFile() {
  JsonDocument doc;
  check(PersistableStoreBase::readDocFromFile(MAIN.c_str(), doc), "read the saved file");
  return doc;
}

// `count` consecutive days from 1 January 2026: day i read i + 1 minutes and 500 ms, turned i + 2 pages.
void writeDays(const unsigned schema, const unsigned count, const bool brokenRow = false) {
  JsonDocument input;
  input["schema"] = schema;
  auto rows = input["ngay"].to<JsonArray>();
  if (brokenRow) rows.add("not a row");
  const auto first = habits::ordinal(2026, 1, 1);
  for (unsigned i = 0; i < count; ++i) {
    auto row = rows.add<JsonArray>();
    row.add(habits::dateKey(first + i));
    row.add(i + 1);
    row.add(i + 2);
    row.add(500);
  }
  check(PersistableStoreBase::writeDocToFile(MAIN.c_str(), input), "seed the old file");
}

void oldFiles() {
  for (const unsigned schema : {3u, 4u}) {
    const std::string label = "schema " + std::to_string(schema) + ": ";
    seed();
    writeDays(schema, 90, true);
    check(READING_STATS.loadFromFile(), label + "old file loads");
    check(READING_STATS.kho.cacNgay().size() == 30, label + "RAM keeps 30 days");
    readinghistory::Snapshot history;
    check(LOAD_HISTORY(history) && history.count == 90 && history.days[0].ma == 20260101 &&
              history.days[89].ma == 20260331 && history.days[89].leMs == 500,
          label + "the screen reads 90 days from the card");
    check(READING_STATS.saveToFile(), label + "save");
    auto doc = savedFile();
    check(doc["ngay"].size() == 90 && doc["ngay"][0][0] == 20260101 && doc["ngay"][0][1] == 1 &&
              doc["ngay"][89][2] == 91 && doc["ngay"][89][3] == 500,
          label + "a save keeps the 60 days RAM does not hold");
    READING_STATS.record(20260401, 60000, 1, 50);
    check(READING_STATS.saveToFile(), label + "save the 91st day");
    doc = savedFile();
    check(doc["ngay"].size() == 90 && doc["ngay"][0][0] == 20260102 && doc["ngay"][89][0] == 20260401,
          label + "the 91st day drops the oldest");
    check(READING_STATS.resetStatistics(false) == ReadingStatsStore::ResetResult::Complete, label + "habits reset");
    check(savedFile()["ngay"].size() == 90, label + "a habits reset keeps the calendar");
    check(READING_STATS.resetStatistics(true) == ReadingStatsStore::ResetResult::Complete, label + "full reset");
    check(READING_STATS.saveToFile() && savedFile()["ngay"].size() == 0, label + "a full reset clears the calendar");
    std::filesystem::remove_all(fixtureRoot);
  }
}

void brokenFiles() {
  seed();
  writeDays(3, 40);
  check(READING_STATS.loadFromFile(), "load before the card breaks");
  check(Storage.writeFile(MAIN.c_str(), "{broken"), "break main");
  check(Storage.writeFile((MAIN + ".bak").c_str(), "{broken"), "break backup");
  const auto before = snapshot();
  readinghistory::Snapshot history;
  check(!LOAD_HISTORY(history), "an unreadable card is no history");
  check(!READING_STATS.saveToFile(), "a save refuses to replace days it cannot read");
  check(snapshot() == before, "the broken bytes stay for recovery");
  std::filesystem::remove_all(fixtureRoot);
  seed();
  check(LOAD_HISTORY(history) && history.count == 0, "no file yet is an empty history");
  std::filesystem::remove_all(fixtureRoot);
}

// The heap one save holds at its peak: 30 days (the old window) and 90.
size_t savePeak(const unsigned days) {
  seed();
  writeDays(4, days);
  check(READING_STATS.loadFromFile(), "load for the heap count");
  READING_STATS.record(20260401, 60000, 1, 50);
  jsonheap::peak = jsonheap::live;
  const size_t before = jsonheap::live;
  io = {};
  check(READING_STATS.saveToFile(), "save for the heap count");
  std::cout << days << " days: a save reads " << io.bytesRead << " B in " << io.readCalls << " reads, writes "
            << std::filesystem::file_size(fixtureRoot + MAIN) << " B\n";
  std::filesystem::remove_all(fixtureRoot);
  return jsonheap::peak - before;
}
}  // namespace

int main() {
  oldFiles();
  brokenFiles();
  const size_t peak30 = savePeak(30), peak90 = savePeak(90);
  std::cout << "heap peak per save (host, 64-bit): 30 days " << peak30 << " B, 90 days " << peak90
            << " B\n";
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
