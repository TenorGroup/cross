// The expected finish date of a book: average pace from the first reading day to today, over
// calendar days, so days without reading count and a book set aside moves its date out.
#include <gtest/gtest.h>

#include "util/NgayDocXong.h"
using namespace ngaydocxong;
namespace {
UocTinh tinh(uint8_t progress, uint8_t start, uint32_t days, uint32_t first, uint32_t last, uint32_t today) {
  return uocTinh(progress, start, days, first, last, today);
}
}  // namespace

TEST(FinishEstimate, AveragePaceOverCalendarDaysGivesTheDate) {
  // 34 % from 5 to 17 August: 13 calendar days, 66 % left, ceil(66 x 13 / 34) = 26 days.
  const auto r = tinh(34, 0, 6, 20260805, 20260817, 20260817);
  EXPECT_EQ(r.trangThai, TrangThai::NgayCuThe);
  EXPECT_EQ(r.soNgay, 26u);
  EXPECT_EQ(r.ngay, 20260912u);
}

TEST(FinishEstimate, DaysSetAsideMoveTheDateOut) {
  // Same record, looked at two weeks later with no reading since: 27 calendar days.
  const auto r = tinh(34, 0, 6, 20260805, 20260817, 20260831);
  EXPECT_EQ(r.trangThai, TrangThai::NgayCuThe);
  EXPECT_EQ(r.soNgay, 53u);
  EXPECT_EQ(r.ngay, 20261023u);
}

TEST(FinishEstimate, CountsOnlyWhatThisRecordRead) {
  // Opened at 30 %, now 40 %: 10 points in 10 days, 60 left.
  const auto r = tinh(40, 30, 5, 20260901, 20260910, 20260910);
  EXPECT_EQ(r.trangThai, TrangThai::NgayCuThe);
  EXPECT_EQ(r.soNgay, 60u);
}

TEST(FinishEstimate, FinishedBook) {
  EXPECT_EQ(tinh(100, 0, 1, 0, 0, 0).trangThai, TrangThai::DaXong);
  EXPECT_EQ(tinh(100, 90, 9, 20260801, 20260830, 20260901).trangThai, TrangThai::DaXong);
}

TEST(FinishEstimate, TooLittleData) {
  EXPECT_EQ(tinh(34, 0, 1, 20260817, 20260817, 20260817).trangThai, TrangThai::ChuaDu);   // one reading day
  EXPECT_EQ(tinh(14, 10, 6, 20260805, 20260817, 20260817).trangThai, TrangThai::ChuaDu);  // 4 points read
  EXPECT_EQ(tinh(15, 10, 6, 20260805, 20260817, 20260817).trangThai, TrangThai::NgayCuThe);  // 5 points
  EXPECT_EQ(tinh(34, 0, 6, 0, 0, 20260817).trangThai, TrangThai::ChuaDu);                 // never dated
  EXPECT_EQ(tinh(20, 60, 6, 20260805, 20260817, 20260817).trangThai, TrangThai::ChuaDu);  // jumped back
  EXPECT_EQ(tinh(34, 0, 6, 20260805, 20260817, 20260801).trangThai, TrangThai::ChuaDu);   // clock behind
}

TEST(FinishEstimate, WithoutTodayCountsDaysFromTheLastReadingDay) {
  const auto r = tinh(34, 0, 6, 20260805, 20260817, 0);
  EXPECT_EQ(r.trangThai, TrangThai::SoNgay);
  EXPECT_EQ(r.soNgay, 26u);
  EXPECT_EQ(r.ngay, 0u);
}

TEST(FinishEstimate, CrossesTheYear) {
  // 25 % in 10 days, 75 % left: 30 days after 15 December.
  const auto r = tinh(25, 0, 4, 20261206, 20261215, 20261215);
  EXPECT_EQ(r.soNgay, 30u);
  EXPECT_EQ(r.ngay, 20270114u);
}

TEST(FinishEstimate, LeapDay) {
  EXPECT_EQ(tinh(50, 0, 7, 20280202, 20280215, 20280215).ngay, 20280229u);
  EXPECT_EQ(tinh(25, 0, 4, 20280206, 20280215, 20280215).ngay, 20280316u);
}

TEST(FinishEstimate, MoreThanAYearIsTooFar) {
  // 7 % in 46 days: 612 days left.
  EXPECT_EQ(tinh(7, 0, 2, 20260810, 20260811, 20260924).trangThai, TrangThai::QuaXa);
  // 365 days is still a date.
  EXPECT_EQ(tinh(50, 0, 30, 20250925, 20260924, 20260924).trangThai, TrangThai::NgayCuThe);
}

TEST(FinishEstimate, DateBeyondTheCalendarFallsBackToDays) {
  // A clock set far ahead puts the date past the last day the calendar can name; the count
  // of days is still right, so show that instead of an empty date.
  const auto r = tinh(34, 0, 6, 20991205, 20991217, 20991217);
  EXPECT_EQ(r.trangThai, TrangThai::SoNgay);
  EXPECT_EQ(r.soNgay, 26u);
}
