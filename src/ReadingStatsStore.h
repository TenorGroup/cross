#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include "util/ReadingHabits.h"
#include "util/ReadingHistory.h"
#include "util/SoLieuDoc.h"

struct BookReadingRecord {
  uint32_t minutes = 0;
  uint16_t remainderMs = 0;
  uint32_t turns = 0;
  uint32_t firstDay = 0;
  uint32_t lastDay = 0;
  uint32_t days = 0;
  uint8_t progress = 0;
  uint8_t startProgress = 0;
};

class ReadingStatsStore : public PersistableStore<ReadingStatsStore> {
  bool writableSchema = true;
  uint32_t bookEpoch = 0;
  uint32_t bookListGeneration = 0;
  // Reading by hour of the local day since the last save; the card keeps the running totals.
  mutable std::array<uint32_t, readinghistory::HOURS> pendingHourMs{};
  mutable std::array<uint16_t, readinghistory::HOURS> pendingHourSessions{};
  ReadingStatsStore() = default;
  ~ReadingStatsStore() = default;

  friend class PersistableStore<ReadingStatsStore>;

 public:
  enum class ResetResult : uint8_t { Failed, Pending, Complete };
  solieu::Kho kho;
  habits::Ledger habitLedger;
  bool statisticsReadable = true;
  static habits::Stamp habitStamp();
  void observeHabits(uint32_t elapsed, uint16_t turns, uint32_t now);
  void prepareHabits();
  // A session the habits ledger just counted goes into the calendar and the hour counters.
  void takeCountedSession();
  void markHabitsSleep();
  struct BookEntry {
    std::string path, title;
    uint32_t day = 0;
  };
  void listBooks(const BookEntry& boundary, bool previous, std::vector<BookEntry>& result) const;
  uint32_t listGeneration() const { return bookListGeneration; }
  std::string activeBookTitle;
  std::string activeBookPath;
  BookReadingRecord activeBook;
  ResetResult resetStatistics(bool all);
  bool saveToFile() const;
  bool loadFromFile();
  // The long calendar from the card with RAM's newer days laid over it. False when the card has a file
  // that does not read whole: a save then refuses too, rather than drop the days it cannot read.
  bool loadHistory(readinghistory::Snapshot& history) const;
  // progress: 0-100, or negative while the book's progress is not known (its index is still
  // being built); the record then keeps the last known one.
  bool activateBook(const std::string& path, int progress, const std::string& title = "");
  bool readBook(const std::string& path, BookReadingRecord& record) const;

 private:
  bool readHistory(readinghistory::Snapshot& history) const;
  bool readBookFile(const std::string& path, BookReadingRecord& record, int memo) const;

 public:
  void record(uint32_t day, uint32_t ms, uint16_t turns, int progress);
  static uint32_t currentDay();
  // Minute of the local day (0-1439) at the same moment currentDay() names, or NO_MINUTE
  // when the clock cannot be read. Records written without a clock keep the date only.
  static constexpr uint16_t NO_MINUTE = 0xFFFF;
  static uint16_t currentMinute();

  static const char* getFilePath() { return "/.crosspoint/reading-stats.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);
};

#define READING_STATS ReadingStatsStore::getInstance()
