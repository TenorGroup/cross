#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include "util/ReadingHabits.h"
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
  ReadingStatsStore() = default;
  ~ReadingStatsStore() = default;

  friend class PersistableStore<ReadingStatsStore>;

 public:
  solieu::Kho kho;
  habits::Ledger habitLedger;
  bool statisticsReadable = true;
  static habits::Stamp habitStamp();
  void observeHabits(uint32_t elapsed, uint16_t turns, uint32_t now);
  void prepareHabits();
  void markHabitsSleep();
  struct BookEntry {
    std::string path, title;
    uint32_t day = 0;
  };
  void listBooks(const BookEntry& boundary, bool previous, std::vector<BookEntry>& result) const;
  std::string activeBookTitle;
  std::string activeBookPath;
  BookReadingRecord activeBook;
  bool resetStatistics(bool all);
  bool saveToFile() const;
  bool loadFromFile();
  bool activateBook(const std::string& path, uint8_t progress, const std::string& title = "");
  bool readBook(const std::string& path, BookReadingRecord& record) const;
  void record(uint32_t day, uint32_t ms, uint16_t turns, uint8_t progress);
  static uint32_t currentDay();

  static const char* getFilePath() { return "/.crosspoint/reading-stats.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);
};

#define READING_STATS ReadingStatsStore::getInstance()
