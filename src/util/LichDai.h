#pragma once
#include <cstdint>

#include "ReadingHabits.h"
#include "ReadingHistory.h"

// The long-term statistics screen: 3 pages of 4 weeks by 7 weekdays, newest first, then the page by hour. Pure,
// tested on the desktop (test/lich_30/LichDaiTest.cpp). What a page holds and how an axis is numbered are decided
// here and only here; the screen draws the results.
namespace lichdai {
constexpr int TRANG_TUAN = 3;  // pages of weeks; the page by hour comes after them
constexpr int SO_TRANG = TRANG_TUAN + 1;
constexpr int SO_TUAN = 4;
constexpr int SO_O = SO_TUAN * 7;

// Ordinal of the Monday in the top left cell of page `trang` (0 = the 4 weeks that end this Sunday).
// Ordinal 1, 1 January 2000, was a Saturday.
inline uint32_t dau(const uint32_t homNay, const int trang) {
  const uint32_t chuNhat = homNay + 6 - (homNay + 4) % 7;
  return chuNhat - static_cast<uint32_t>(SO_O * trang) - (SO_O - 1);
}

// The calendar row of a day (ordinal), or nullptr.
inline const solieu::NgayDoc* ngay(const readinghistory::Snapshot& lich, const uint32_t thuTu) {
  const uint32_t ma = habits::dateKey(thuTu);
  for (size_t i = 0; i < lich.count; ++i)
    if (lich.days[i].ma == ma) return &lich.days[i];
  return nullptr;
}

// Reading time and reading sessions of each week of the page that starts on `dauTrang`.
struct Tuan {
  uint64_t ms = 0;
  uint32_t lan = 0;
};
inline void tuan(const readinghistory::Snapshot& lich, const uint32_t dauTrang, Tuan (&ra)[SO_TUAN]) {
  for (int k = 0; k < SO_TUAN; ++k) {
    ra[k] = {};
    for (int d = 0; d < 7; ++d)
      if (const auto* n = ngay(lich, dauTrang + 7 * k + d)) {
        ra[k].ms += static_cast<uint64_t>(n->phut) * 60000 + n->leMs;
        ra[k].lan += n->lan;
      }
  }
}

// Minutes a reading session of the week, rounded; false (no point on the line) for a week without a counted session.
inline bool trungBinh(const Tuan& tuan, uint32_t& phut) {
  if (!tuan.lan) return false;
  phut = static_cast<uint32_t>((tuan.ms / tuan.lan + 30000) / 60000);
  return true;
}

// Top of a y axis: the smallest of 2, 4, 6, 10, 20, 40, 60, 100, 200... not below `lonNhat`, so the axis has round
// numbers at 0, the middle (half the top) and the top.
inline uint32_t dinh(const uint32_t lonNhat) {
  for (uint32_t bac = 1;; bac *= 10) {
    for (const uint32_t m : {1u, 2u, 3u, 5u})
      if (2 * m * bac >= lonNhat) return 2 * m * bac;
    if (bac > UINT32_MAX / 100) return UINT32_MAX - 1;
  }
}

// The time axis: minutes while the longest bar is under 2 hours, whole hours from there.
struct Truc {
  uint32_t dinh = 2;
  bool gio = false;
  uint64_t ms() const { return static_cast<uint64_t>(dinh) * (gio ? 3600000u : 60000u); }
};
inline Truc trucThoiGian(const uint64_t lonNhatMs) {
  const uint64_t phut = (lonNhatMs + 59999) / 60000;
  if (phut < 120) return {dinh(static_cast<uint32_t>(phut)), false};
  return {dinh(static_cast<uint32_t>((phut + 59) / 60)), true};
}
}  // namespace lichdai
