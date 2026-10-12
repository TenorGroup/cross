#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "SoLieuDoc.h"

// The long reading calendar lives on the card ("ngay" in reading-stats.json); RAM keeps only the 30 newest
// days (solieu::Kho). A save reads the card's rows back, lays RAM's rows over them and writes up to DAYS
// rows, so the older days never take resident heap. The screen reads the same snapshot when it opens.
namespace readinghistory {
constexpr size_t DAYS = 90;

struct Snapshot {
  std::array<solieu::NgayDoc, DAYS> days{};  // oldest first
  size_t count = 0;

  // A later row for a date replaces the earlier one: RAM's row was read from the card and has only grown.
  // The DAYS newest dates stay, as in Kho, so a clock set years ahead costs one row, not the calendar.
  void merge(const solieu::NgayDoc& day) {
    if (!day.ma) return;
    auto* const end = days.begin() + count;
    auto* at = std::lower_bound(days.begin(), end, day.ma,
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
}  // namespace readinghistory
