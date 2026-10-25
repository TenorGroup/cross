#include <gtest/gtest.h>

#include "util/FrontlightSchedule.h"

using frontlightschedule::lightOnAt;

TEST(FrontlightSchedule, EqualHoursMeanNoSchedule) {
  for (uint8_t h = 0; h < 24; ++h) EXPECT_FALSE(lightOnAt(h, 7, 7));
}

TEST(FrontlightSchedule, ADayWindowIncludesItsStartAndNotItsEnd) {
  EXPECT_FALSE(lightOnAt(17, 18, 23));
  EXPECT_TRUE(lightOnAt(18, 18, 23));
  EXPECT_TRUE(lightOnAt(22, 18, 23));
  EXPECT_FALSE(lightOnAt(23, 18, 23));
}

TEST(FrontlightSchedule, AWindowRunsPastMidnight) {
  EXPECT_TRUE(lightOnAt(21, 21, 6));
  EXPECT_TRUE(lightOnAt(0, 21, 6));
  EXPECT_TRUE(lightOnAt(5, 21, 6));
  EXPECT_FALSE(lightOnAt(6, 21, 6));
  EXPECT_FALSE(lightOnAt(12, 21, 6));
}

// The window to the minute (Settings keeps the hour and a minute in steps of 5): minutes of the day, 0..1439.
using frontlightschedule::lightOnAtMinute;
using frontlightschedule::minuteOfDay;

TEST(FrontlightSchedule, AMinuteWindowStartsAtItsMinuteAndEndsAtItsOwn) {
  const auto from = minuteOfDay(21, 30), until = minuteOfDay(22, 0);
  EXPECT_FALSE(lightOnAtMinute(minuteOfDay(21, 29), from, until));
  EXPECT_TRUE(lightOnAtMinute(minuteOfDay(21, 30), from, until));
  EXPECT_TRUE(lightOnAtMinute(minuteOfDay(21, 59), from, until));
  EXPECT_FALSE(lightOnAtMinute(minuteOfDay(22, 0), from, until));
}

TEST(FrontlightSchedule, AMinuteWindowRunsPastMidnight) {
  const auto from = minuteOfDay(22, 30), until = minuteOfDay(6, 15);
  EXPECT_FALSE(lightOnAtMinute(minuteOfDay(22, 29), from, until));
  EXPECT_TRUE(lightOnAtMinute(minuteOfDay(22, 30), from, until));
  EXPECT_TRUE(lightOnAtMinute(minuteOfDay(0, 0), from, until));
  EXPECT_TRUE(lightOnAtMinute(minuteOfDay(6, 14), from, until));
  EXPECT_FALSE(lightOnAtMinute(minuteOfDay(6, 15), from, until));
  EXPECT_FALSE(lightOnAtMinute(minuteOfDay(12, 0), from, until));
}

TEST(FrontlightSchedule, TheSameMinuteMeansNoScheduleAndADifferentMinuteInTheSameHourIsOne) {
  for (uint16_t m = 0; m < 1440; m += 7) EXPECT_FALSE(lightOnAtMinute(m, minuteOfDay(7, 30), minuteOfDay(7, 30)));
  EXPECT_TRUE(lightOnAtMinute(minuteOfDay(7, 10), minuteOfDay(7, 5), minuteOfDay(7, 20)));
  EXPECT_FALSE(lightOnAtMinute(minuteOfDay(7, 25), minuteOfDay(7, 5), minuteOfDay(7, 20)));
}

TEST(FrontlightSchedule, AFileWithHoursOnlyKeepsTheWindowItHad) {
  // v1.0.76 stored whole hours: its minutes read as 0, and every minute of every hour answers as the hour did.
  for (uint8_t hour = 0; hour < 24; ++hour)
    for (uint8_t minute = 0; minute < 60; minute += 5)
      EXPECT_EQ(lightOnAtMinute(minuteOfDay(hour, minute), minuteOfDay(21, 0), minuteOfDay(6, 0)),
                lightOnAt(hour, 21, 6));
}
