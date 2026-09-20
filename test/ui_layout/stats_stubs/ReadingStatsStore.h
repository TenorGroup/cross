#pragma once
#include <cstdint>
#include <string>
#include <vector>
struct StatsDay { uint32_t ma, phut, leMs, trang; };
struct StatsKho {
  std::vector<StatsDay> days{{20260920, 50, 1000, 20}, {20260919, 20, 0, 10}};
  const auto& cacNgay() const { return days; }
  uint32_t phutChuaBietNgay() const { return 0; }
  uint32_t msChuaBietNgay() const { return 0; }
  uint32_t trangChuaBietNgay() const { return 0; }
};
struct StatsHabit {
  uint32_t sessions = 23, coverage = 1, activeMs = 100000, nightMs = 40000, shortSessions = 5, longSessions = 3;
};
struct StatsLedger { StatsHabit summarize(uint32_t) const { return {}; } };
struct ReadingStatsStore {
  bool statisticsReadable = true;
  StatsKho kho;
  StatsLedger habitLedger;
  std::string activeBookPath = "/book.epub", activeBookTitle = "A sample book";
  struct { uint32_t minutes = 100, remainderMs = 1000, progress = 42; } activeBook;
  static uint32_t currentDay() { return 20260920; }
  struct Stamp { uint32_t day; };
  static Stamp habitStamp() { return {20260920}; }
};
inline ReadingStatsStore READING_STATS;
