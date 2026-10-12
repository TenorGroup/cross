// The long reading calendar: RAM keeps the 30 newest days, the card keeps 90 and the 24 hour totals, and a
// save never drops the older days. The heap a save takes is counted here: ArduinoJson allocates with malloc, strings
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

void at(const int hour, const int minute, const int second) {
  halClock.local = {};
  halClock.local.tm_hour = hour;
  halClock.local.tm_min = minute;
  halClock.local.tm_sec = second;
  halClock.localSet = true;
}
unsigned hourTurns(const JsonDocument& doc, const int hour) { return doc["hourTurns"][hour].as<unsigned>(); }
uint64_t hourMs(const JsonDocument& doc, const int hour) {
  return doc["hourMinutes"][hour].as<uint64_t>() * 60000 + doc["hourRemainderMs"][hour].as<uint64_t>();
}

// 24 running totals by hour of the local day: split at each hour mark and at midnight, a short update adds its
// ms, and a save that fails half way counts its hours once.
void hours() {
  seed();
  at(1, 0, 30);
  READING_STATS.record(20260914, 60000, 1, 50);  // 00:59:30 to 01:00:30
  for (int second = 0; second < 60; ++second) {
    at(1, 1, second);
    READING_STATS.record(20260914, 1000, 0, 50);
  }
  check(READING_STATS.saveToFile(), "save the first hours");
  auto doc = savedFile();
  check(doc["hourMinutes"].size() == 24 && doc["hourRemainderMs"].size() == 24, "24 counters on the card");
  check(hourMs(doc, 0) == 30000 && hourMs(doc, 1) == 90000, "an hour mark splits the minute, short updates add up");
  check(doc["hourTurns"].size() == 24 && hourTurns(doc, 0) == 0 && hourTurns(doc, 1) == 1,
        "a page turn counts in the hour it was turned");
  check(READING_STATS.saveToFile() && hourMs(savedFile(), 1) == 90000, "a second save counts nothing twice");
  check(READING_STATS.loadFromFile(), "reload");
  at(0, 0, 30);
  READING_STATS.record(20260915, 60000, 1, 50);  // 23:59:30 to 00:00:30
  check(READING_STATS.saveToFile(), "save across midnight");
  doc = savedFile();
  check(hourMs(doc, 23) == 30000 && hourMs(doc, 0) == 60000 && hourMs(doc, 1) == 90000,
        "midnight splits into hour 23 and hour 0, the totals carry over a reload");
  check(hourTurns(doc, 23) == 0 && hourTurns(doc, 0) == 1 && hourTurns(doc, 1) == 1,
        "the turn after midnight counts in hour 0, the earlier ones carry over a reload");
  at(0, 2, 0);
  READING_STATS.record(20260915, 60000, 1, 50);
  storageFault = {StorageFaultOperation::Rename, MAIN + ".tmp", MAIN};
  check(!READING_STATS.saveToFile() && storageFault.hits == 1, "the new file stops at .tmp");
  storageFault = {};
  check(READING_STATS.saveToFile() && hourMs(savedFile(), 0) == 120000 && hourTurns(savedFile(), 0) == 2,
        "hours staged in .tmp count once");
  at(0, 3, 0);
  READING_STATS.record(20260915, 60000, 1, 50);
  storageFault = {StorageFaultOperation::Rename, MAIN, MAIN + ".bak"};
  check(!READING_STATS.saveToFile() && storageFault.hits == 1, "the old file stays in place");
  storageFault = {};
  check(READING_STATS.saveToFile() && hourMs(savedFile(), 0) == 180000 && hourTurns(savedFile(), 0) == 3,
        "hours kept in RAM count once");
  at(5, 0, 0);
  READING_STATS.record(0, 60000, 1, 50);
  check(READING_STATS.saveToFile() && hourMs(savedFile(), 5) == 0 && hourTurns(savedFile(), 5) == 0,
        "reading without a date has no hour");
  at(4, 10, 0);
  READING_STATS.record(20260915, 0, 2, 50);
  check(READING_STATS.saveToFile() && hourTurns(savedFile(), 4) == 2 && hourMs(savedFile(), 4) == 0,
        "turns without reading time still count in their hour");
  at(6, 30, 0);
  READING_STATS.record(20260915, 60000, 1, 50);
  readinghistory::Snapshot history;
  check(LOAD_HISTORY(history) && history.hourMs[6] == 60000 && history.hourTurns[6] == 1 && history.hourTurns[4] == 2,
        "the screen sees hours not saved yet");
  check(READING_STATS.resetStatistics(true) == ReadingStatsStore::ResetResult::Complete && READING_STATS.saveToFile(),
        "full reset");
  doc = savedFile();
  check(hourMs(doc, 0) == 0 && hourMs(doc, 6) == 0 && hourTurns(doc, 0) == 0, "a full reset clears the hours");
  halClock.localSet = false;
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
  hours();
  const size_t peak30 = savePeak(30), peak90 = savePeak(90);
  std::cout << "heap peak per save (host, 64-bit): 30 days " << peak30 << " B, 90 days " << peak90
            << " B\n";
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
