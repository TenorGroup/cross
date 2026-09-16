#include <gtest/gtest.h>

#include "util/ReadingHabits.h"
using namespace habits;
namespace {
Stamp stamp(uint32_t day, int minute = 600) {
  return {day, (day - 1) * 1440 + static_cast<uint32_t>(minute), static_cast<uint16_t>(minute), 0};
}
void read(Ledger& l, uint32_t day, int minute, int seconds, uint32_t& mono, int turnEvery = 30) {
  for (int i = 0; i < seconds; ++i) {
    mono += 1000;
    l.observe(1000, turnEvery && i % turnEvery == 0 ? 1 : 0, stamp(day, minute + i / 60), mono);
  }
}
Ledger nightHistory(uint32_t today) {
  Ledger l;
  l.firstDay = today - 28;
  for (int i = 0; i < 14; ++i) l.days[i] = {today - 1 - static_cast<uint32_t>(i), 600000, 600000, 0, 2, 2, 0, 0, true};
  return l;
}
TEST(Habits, DateRoundTripAndLeapDay) {
  EXPECT_EQ(dateKey(ordinal(2024, 2, 29)), 20240229u);
  EXPECT_EQ(ordinal(2023, 2, 29), 0u);
  EXPECT_EQ(dateKey(ordinal(2026, 9, 15)), 20260915u);
}
TEST(Habits, OneCompletedNightGetsSuggestion) {
  Ledger l;
  uint32_t n = 0;
  read(l, 100, 1380, 1800, n);
  l.finish();
  EXPECT_TRUE(l.summarize(101).eligible & (1 << NIGHT));
  l.evaluate(101);
  EXPECT_TRUE(l.awarded & (1 << NIGHT));
}
TEST(Habits, IdleBookIsNotLongSession) {
  Ledger l;
  uint32_t n = 0;
  read(l, 100, 600, 2400, n, 0);
  l.finish();
  EXPECT_EQ(l.days[0].uncertain, 1);
  EXPECT_EQ(l.days[0].longSessions, 0);
  EXPECT_LE(l.days[0].activeMs, 300000u);
}
TEST(Habits, ShortMenuAndBookSwitchStayOneSession) {
  Ledger l;
  uint32_t n = 0;
  read(l, 100, 600, 180, n);
  n += 60000;
  read(l, 100, 604, 180, n);
  l.finish();
  EXPECT_EQ(l.days[0].sessions, 1);
  EXPECT_EQ(l.days[0].shortSessions, 1);
  EXPECT_EQ(l.days[0].activeMs, 360000u);
}
TEST(Habits, LongBreakSplitsSessionsWithoutCallingItUncertain) {
  Ledger l;
  uint32_t n = 0;
  read(l, 100, 600, 180, n);
  n += 360000;
  read(l, 100, 609, 180, n);
  l.finish();
  EXPECT_EQ(l.days[0].sessions, 2);
  EXPECT_EQ(l.days[0].shortSessions, 2);
  EXPECT_EQ(l.days[0].uncertain, 0);
}
TEST(Habits, MidnightIsTwoDatesAndOneNight) {
  Ledger l;
  uint32_t n = 0;
  read(l, 100, 1439, 60, n);
  read(l, 101, 0, 60, n);
  l.finish();
  EXPECT_EQ(l.days[0].activeMs, 60000u);
  EXPECT_EQ(l.days[1].activeMs, 60000u);
  EXPECT_TRUE(l.days[0].night);
  EXPECT_FALSE(l.days[1].night);
}
TEST(Habits, FiveAmBoundarySeparatesNightAndMorning) {
  Ledger l;
  uint32_t n = 0;
  read(l, 100, 299, 60, n);
  read(l, 100, 300, 60, n);
  EXPECT_EQ(l.days[0].nightMs, 60000u);
  EXPECT_EQ(l.days[0].earlyMs, 60000u);
}
TEST(Habits, CrashCheckpointIsUncertain) {
  Ledger l;
  uint32_t n = 0;
  read(l, 100, 600, 180, n);
  l.restored = true;
  l.settle(stamp(100, 610));
  EXPECT_EQ(l.days[0].uncertain, 1);
  EXPECT_EQ(l.days[0].shortSessions, 0);
}
TEST(Habits, CleanSleepCheckpointCanRejoin) {
  Ledger l;
  uint32_t n = 0;
  read(l, 100, 600, 180, n);
  l.session.clean = true;
  l.restored = true;
  n += 60000;
  read(l, 100, 604, 180, n);
  l.finish();
  EXPECT_EQ(l.days[0].shortSessions, 1);
  EXPECT_EQ(l.days[0].sessions, 1);
}
TEST(Habits, ClockRollbackNeverCreatesLongSession) {
  Ledger l;
  uint32_t n = 0;
  read(l, 100, 600, 180, n);
  read(l, 99, 600, 180, n);
  l.finish();
  EXPECT_EQ(l.days[0].uncertain, 1);
  EXPECT_EQ(l.summarize(101).eligible, 0);
}
TEST(Habits, UnknownClockStopsTimeNicknames) {
  auto l = nightHistory(200);
  l.clockLost = true;
  EXPECT_EQ(l.summarize(200).eligible, 0);
}
TEST(Habits, UncertainSessionsStayInDenominator) {
  auto l = nightHistory(200);
  for (auto& d : l.days)
    if (d.day) {
      d.sessions = 22;
      d.uncertain = 20;
    }
  EXPECT_EQ(l.summarize(200).eligible & ~(1 << REGULAR), 0);
}
TEST(Habits, TodayCannotChangeCompletedDayAnalysis) {
  auto l = nightHistory(200);
  uint32_t n = 0;
  auto before = l.summarize(200);
  read(l, 200, 600, 180, n);
  l.finish();
  auto after = l.summarize(200);
  EXPECT_EQ(before.sessions, after.sessions);
  EXPECT_EQ(before.eligible, after.eligible);
}
TEST(Habits, SuggestionsRefreshOnNextEvaluationDay) {
  auto l = nightHistory(200);
  l.evaluate(200);
  EXPECT_TRUE(l.awarded & (1 << NIGHT));
  for (auto& d : l.days) {
    d.earlyMs = d.activeMs;
    d.nightMs = 0;
    d.night = false;
  }
  l.evaluate(201);
  EXPECT_FALSE(l.awarded & (1 << NIGHT));
  EXPECT_TRUE(l.awarded & (1 << EARLY));
}
TEST(Habits, TwoDaysCanSuggestRegularReading) {
  Ledger l;
  uint32_t n = 0;
  read(l, 100, 600, 600, n);
  l.finish();
  read(l, 101, 600, 600, n);
  l.finish();
  l.evaluate(102);
  EXPECT_TRUE(l.awarded & (1 << REGULAR));
}
TEST(Habits, OneShortOrIdleVisitDoesNotSuggestHabit) {
  Ledger l;
  uint32_t n = 0;
  read(l, 100, 1380, 30, n);
  l.finish();
  l.evaluate(101);
  EXPECT_EQ(l.awarded, 0);
}
TEST(Habits, RetentionStaysFixedAndDropsOldDate) {
  Ledger l;
  uint32_t n = 0;
  for (uint32_t d = 100; d < 150; ++d) {
    read(l, d, 600, 120, n);
    l.finish();
  }
  for (const auto& d : l.days) EXPECT_GE(d.day, 121u);
  EXPECT_LE(sizeof(Ledger), 1200u);
}
TEST(Habits, WeekendOnlyHasItsOwnGateAndZeroWeekdayBranch) {
  Ledger l;
  l.firstDay = 100;
  int i = 0;
  for (uint32_t d = 100; d < 128; ++d)
    if ((d - 1) % 7 == 0) l.days[i++] = {d, 1200000, 0, 0, 2, 0, 0, 0, false};
  auto s = l.summarize(128);
  EXPECT_GE(s.weekends, 3);
  EXPECT_TRUE(s.eligible & (1 << WEEKEND));
}
TEST(Habits, ManyShortLowSignalSessionsCannotBecomeMarathoner) {
  auto l = nightHistory(200);
  for (auto& d : l.days)
    if (d.day) {
      d.sessions = 21;
      d.shortSessions = 0;
      d.longSessions = 1;
      d.uncertain = 20;
    }
  EXPECT_FALSE(l.summarize(200).eligible & (1 << LONG));
}
TEST(Habits, StaleRtcIsNotTrustedAfterNinetySeconds) {
  Ledger l;
  uint32_t n = 0;
  for (int i = 0; i < 130; ++i) {
    n += 1000;
    l.observe(1000, i % 30 == 0, stamp(100), n);
  }
  EXPECT_TRUE(l.clockLost);
  EXPECT_TRUE(l.session.uncertain);
}
TEST(Habits, EveryNicknameHasAPositivePath) {
  auto l = nightHistory(200);
  auto s = l.summarize(200);
  EXPECT_TRUE(s.eligible & (1 << NIGHT));
  EXPECT_TRUE(s.eligible & (1 << SHORT));
  EXPECT_TRUE(s.eligible & (1 << REGULAR));
  for (auto& d : l.days)
    if (d.day) {
      d.earlyMs = d.activeMs;
      d.nightMs = 0;
      d.night = false;
      d.activeMs = 3600000;
      d.earlyMs = d.activeMs;
      d.shortSessions = 0;
      d.longSessions = 2;
    }
  s = l.summarize(200);
  EXPECT_TRUE(s.eligible & (1 << EARLY));
  EXPECT_TRUE(s.eligible & (1 << LONG));
}
}  // namespace

#include "util/ReadingExcerpt.h"
TEST(ReadingExcerpt, SkipsPartialSentenceAndPreservesWords) {
  readingexcerpt::Builder b;
  for (auto word : {"phần", "dở.", "Đây", "là", "một", "câu", "văn", "đầy", "đủ."}) b.word(word);
  EXPECT_EQ(b.result(), "Đây là một câu văn đầy đủ.");
}
TEST(ReadingExcerpt, RejectsIncompleteAndOverlongSentences) {
  readingexcerpt::Builder b;
  b.word("Cuối.");
  for (int i = 0; i < 500; ++i) b.word("dài");
  b.word("hết.");
  EXPECT_TRUE(b.result().empty());
  for (auto word : {"Một", "câu", "tiếp", "theo", "vẫn", "được", "giữ."}) b.word(word);
  EXPECT_EQ(b.result(), "Một câu tiếp theo vẫn được giữ.");
}

TEST(ReadingExcerpt, CurlyClosingQuoteEndsSentence) {
  readingexcerpt::Builder b;
  b.word("trước.”");
  for (auto word : {"“Đây", "là", "một", "câu", "đối", "thoại", "trọn", "vẹn.”"}) b.word(word);
  EXPECT_EQ(b.result(), "“Đây là một câu đối thoại trọn vẹn.”");
}

TEST(ReadingExcerpt, PlainTextWhitespaceDoesNotAllocatePerWord) {
  readingexcerpt::Builder b;
  b.line("Trang trước.  Đây là một câu");
  b.line("văn tiếng Việt đầy đủ.");
  EXPECT_EQ(b.result(), "Đây là một câu văn tiếng Việt đầy đủ.");
}

TEST(Habits, TwoObservedDaysFromDeviceCanSuggestRegularToday) {
  Ledger l;
  l.firstDay = 9755;
  l.days[0] = {9755, 2357844, 360819, 0, 7, 0, 0, 6, true};
  l.days[1] = {9756, 554045, 554045, 0, 3, 1, 0, 1, false};
  const auto s = l.summarize(9756);
  EXPECT_EQ(s.recentDays, 2);
  EXPECT_TRUE(s.eligible & (1 << REGULAR));
  EXPECT_FALSE(s.eligible & (1 << NIGHT));
  EXPECT_FALSE(s.eligible & (1 << LONG));
}
TEST(Habits, TwoIdleDaysCannotBecomeRegular) {
  Ledger l;
  l.firstDay = 100;
  l.days[0] = {100, 300000, 0, 0, 1, 0, 0, 1, false};
  l.days[1] = {101, 300000, 0, 0, 1, 0, 0, 1, false};
  EXPECT_EQ(l.summarize(101).eligible, 0);
}
