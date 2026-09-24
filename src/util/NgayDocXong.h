#pragma once
#include <cstdint>

#include "ReadingHabits.h"

// When a book will be finished at the pace it has been read so far. The one place this is decided:
// the Recent card and the book stats screen both ask here.
//
// Pace is the percent this record has read (progress less the percent it started at) over the
// calendar days from the first reading day to today, days without reading included: a book set
// aside for two weeks moves its date out, as it will in fact. Too little to go on (one reading
// day, under 5 points read, or no dated day) gives no date: progress is a rounded whole percent,
// so 5 points read can still be up to 20 % off in pace.
namespace ngaydocxong {

enum class TrangThai : uint8_t {
  ChuaDu,     // too little reading to estimate
  DaXong,     // the book is at 100 %
  NgayCuThe,  // `ngay` is the expected day (YYYYMMDD), `soNgay` days from today
  SoNgay,     // no clock today: about `soNgay` days from the last reading day
  QuaXa,      // more than a year away
};

struct UocTinh {
  TrangThai trangThai = TrangThai::ChuaDu;
  uint32_t ngay = 0;
  uint32_t soNgay = 0;
};

constexpr uint32_t TRAN_NGAY = 365;
constexpr uint8_t SAN_DIEM = 5;

inline uint32_t thuTu(const uint32_t ma) {
  return habits::ordinal(static_cast<uint16_t>(ma / 10000), static_cast<uint8_t>(ma / 100 % 100),
                         static_cast<uint8_t>(ma % 100));
}

// Day codes are YYYYMMDD, 0 when unknown; `homNay` is 0 when the clock cannot be read.
inline UocTinh uocTinh(const uint8_t progress, const uint8_t startProgress, const uint32_t days,
                       const uint32_t firstDay, const uint32_t lastDay, const uint32_t homNay) {
  UocTinh r;
  if (progress >= 100) {
    r.trangThai = TrangThai::DaXong;
    return r;
  }
  const uint32_t dau = thuTu(firstDay), moc = thuTu(homNay ? homNay : lastDay);
  if (days < 2 || progress < startProgress + SAN_DIEM || !dau || moc < dau) return r;
  const uint32_t daDoc = progress - startProgress, con = 100u - progress, khoang = moc - dau + 1;
  r.soNgay = (con * khoang + daDoc - 1) / daDoc;
  if (r.soNgay > TRAN_NGAY) {
    r.trangThai = TrangThai::QuaXa;
  } else if (homNay) {
    r.trangThai = TrangThai::NgayCuThe;
    r.ngay = habits::dateKey(moc + r.soNgay);
  } else {
    r.trangThai = TrangThai::SoNgay;
  }
  return r;
}

}  // namespace ngaydocxong
