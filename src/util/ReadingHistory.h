#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "SoLieuDoc.h"

// The long reading calendar lives on the card ("ngay" in reading-stats.json); RAM keeps only the 30 newest
// days (solieu::Kho). A save reads the card's rows back, lays RAM's rows over them and writes up to DAYS
// rows, so the older days never take resident heap. The screen reads the same snapshot when it opens.
// The 24 hour totals work the same way: the card keeps the running sums, RAM only what came since the last save.
namespace readinghistory {
constexpr size_t DAYS = 90;
constexpr size_t HOURS = 24;
constexpr uint32_t DAY_MS = 86400000;

struct Snapshot {
  std::array<solieu::NgayDoc, DAYS> days{};  // oldest first
  size_t count = 0;
  std::array<uint64_t, HOURS> hourMs{};  // reading in each hour of the local day, since the counters began
  std::array<uint32_t, HOURS> hourSessions{};  // reading sessions begun in each hour of the local day, the same way

  // A later row for a date replaces the earlier one: RAM's row was read from the card and has only grown.
  // The DAYS newest dates stay, as in Kho, so a clock set years ahead costs one row, not the calendar.
  void merge(const solieu::NgayDoc& day) {
    if (!day.ma) return;
    const auto end = days.begin() + count;
    auto at = std::lower_bound(days.begin(), end, day.ma,
                               [](const solieu::NgayDoc& row, const uint32_t date) { return row.ma < date; });
    if (at != end && at->ma == day.ma) {
      *at = day;
      return;
    }
    if (count == DAYS) {
      if (at == days.begin()) return;  // older than every kept date
      std::move(days.begin() + 1, at, days.begin());
      *(at - 1) = day;
      return;
    }
    std::move_backward(at, end, end + 1);
    *at = day;
    ++count;
  }
};

// Lays `elapsed` ms of reading that ended `endMs` into the local day on the hours it covered: split at each
// hour mark, and past midnight into the last hours of the day before. RAM keeps these since the last save.
inline void addHours(std::array<uint32_t, HOURS>& hours, uint32_t endMs, uint32_t elapsed) {
  if (endMs >= DAY_MS) return;
  while (elapsed) {
    if (!endMs) endMs = DAY_MS;
    const uint32_t hour = (endMs - 1) / 3600000;
    const uint32_t part = std::min(elapsed, endMs - hour * 3600000);
    hours[hour] += std::min(part, UINT32_MAX - hours[hour]);
    elapsed -= part;
    endMs -= part;
  }
}

// A reading session counts in the hour of the local day it began in (`startMinute`, 0-1439). RAM keeps these since
// the last save.
inline void addSession(std::array<uint16_t, HOURS>& hours, const uint16_t startMinute) {
  if (startMinute >= 1440) return;
  auto& count = hours[startMinute / 60];
  if (count < UINT16_MAX) ++count;
}
}  // namespace readinghistory
