// tenor/ugly on a touch screen: where a finger lands, and where the papers it opens go.
#include <gtest/gtest.h>

#include "shells/ugly/UglyTouch.h"

namespace {
using namespace ugly::touch;

TEST(TouchPage, RowsFootAndTheBottomBand) {
  EXPECT_EQ(notebookAt(240, 143).spot, Spot::None) << "the title band is no row";
  EXPECT_EQ(notebookAt(2, 144).spot, Spot::Row) << "a row takes the margin too";
  EXPECT_EQ(notebookAt(2, 144).row, 0);
  EXPECT_EQ(notebookAt(470, 655).row, 7);
  EXPECT_EQ(notebookAt(240, 656).spot, Spot::Foot);
  EXPECT_EQ(notebookAt(159, 730).spot, Spot::Prev);
  EXPECT_EQ(notebookAt(160, 730).spot, Spot::Back);
  EXPECT_EQ(notebookAt(320, 783).spot, Spot::Next);
  EXPECT_EQ(notebookAt(240, 784).spot, Spot::None) << "the strip over the Home key is nobody's";
}

TEST(TouchDiary, AWordAloneOwnsItsLineTwoWordsSplitTheGap) {
  // "Đọc tiếp hay đổi sách?" on one line, "xem cả bàn" two lines down.
  const Word words[] = {{392, 32, 180}, {392, 260, 400}, {520, 120, 330}};
  EXPECT_EQ(wordAt(words, 3, 5, 360), 0);
  EXPECT_EQ(wordAt(words, 3, 219, 400), 0) << "left of the middle of the gap";
  EXPECT_EQ(wordAt(words, 3, 221, 400), 1) << "right of it";
  EXPECT_EQ(wordAt(words, 3, 479, 411), 1);
  EXPECT_EQ(wordAt(words, 3, 3, 500), 2) << "a word alone owns the whole width";
  EXPECT_EQ(wordAt(words, 3, 240, 412), -1) << "between the bands";
  // The order the words come in does not matter.
  const Word swapped[] = {{392, 260, 400}, {392, 32, 180}};
  EXPECT_EQ(wordAt(swapped, 2, 100, 380), 1);
  EXPECT_EQ(wordAt(swapped, 2, 300, 380), 0);
}

TEST(TouchDesk, EveryCellIsATargetAndTheyTileTheDesk) {
  EXPECT_EQ(deskAt(10, 60), 5) << "the lamp: Settings";
  EXPECT_EQ(deskAt(240, 60), 1) << "the clock: Recent";
  EXPECT_EQ(deskAt(479, 287), 0) << "the calendar: Stats";
  EXPECT_EQ(deskAt(0, 288), 2) << "the open book";
  EXPECT_EQ(deskAt(239, 719), 3);
  EXPECT_EQ(deskAt(240, 544), 4);
  EXPECT_EQ(deskAt(240, 30), -1) << "the status band";
  EXPECT_EQ(deskAt(240, 730), -1) << "the bottom band";
  for (int y = 48; y < 720; y += 4)
    for (int x = 0; x < 480; x += 4) ASSERT_GE(deskAt(x, y), 0) << x << "," << y;
}

TEST(TouchChange, TwoTickThreeStepFourOpenAPaper) {
  EXPECT_EQ(changeFor(2, false), Change::Tick);
  EXPECT_EQ(changeFor(3, false), Change::Next);
  EXPECT_EQ(changeFor(4, true), Change::Paper);
  EXPECT_EQ(changeFor(6, true), Change::Paper);
}

// The chosen value lies under the finger in the three cases drawn (PHAC-VONG2 05a, 05b, 05c).
void underFinger(const int row, const int count, const int chosen, const int shown, const int above, const int below) {
  const int top = rowTop(row);
  const Paper p = placePaper(top, count, chosen);
  EXPECT_EQ(paperRowTop(p, chosen), top);
  EXPECT_EQ(p.last - p.first + 1, shown);
  EXPECT_EQ(p.hiddenAbove, above);
  EXPECT_EQ(p.hiddenBelow, below);
  EXPECT_GE(p.top, PAPER_TOP);
  EXPECT_LE(p.bottom, PAPER_BOTTOM);
  int value = -1;
  EXPECT_EQ(paperAt(p, 240, top + 32, value), PaperSpot::Value);
  EXPECT_EQ(value, chosen) << "a second tap where the first one was keeps the value";
}

TEST(TouchPaper, MiddleOfThePage) { underFinger(4, 5, 2, 5, 0, 0); }
TEST(TouchPaper, NearTheBottom) { underFinger(7, 5, 0, 2, 0, 3); }
TEST(TouchPaper, NearTheTop) { underFinger(0, 6, 2, 5, 1, 0); }

TEST(TouchPaper, TheTornBandsScrollAndTheRestCloses) {
  Paper p = placePaper(rowTop(7), 5, 0);
  int value = -1;
  EXPECT_EQ(paperAt(p, 240, p.bottom - 10, value), PaperSpot::MoreBelow);
  EXPECT_EQ(paperAt(p, 240, p.top - 1, value), PaperSpot::Outside);
  EXPECT_EQ(paperAt(p, 10, rowTop(7) + 30, value), PaperSpot::Outside) << "left of the paper";
  for (int turns = 0; p.hiddenBelow > 0 && turns < 5; ++turns) p = scrollPaper(p, 5, true);
  EXPECT_EQ(p.hiddenBelow, 0);
  EXPECT_EQ(p.last, 4) << "scrolled down to the last value";
  EXPECT_LE(p.bottom, PAPER_BOTTOM);
  Paper q = placePaper(rowTop(0), 6, 2);
  EXPECT_EQ(paperAt(q, 240, q.top + 10, value), PaperSpot::MoreAbove);
  q = scrollPaper(q, 6, false);
  EXPECT_EQ(q.hiddenAbove, 0);
  EXPECT_EQ(q.first, 0);
}

TEST(TouchPaper, ItsRubbedEdgeNeverHalvesARow) {
  EXPECT_EQ(rubEdge(rowTop(3) + 30, true), rowTop(3));
  EXPECT_EQ(rubEdge(rowTop(3) + 30, false), rowTop(4));
  EXPECT_EQ(rubEdge(rowTop(3) + 4, true), rowTop(3) + 4) << "in the gap above the letters: stays";
}

// "bin it" is a row or more away from where the finger lifted, in the middle, near the bottom and at the top.
void askClear(const int row, const int liftY, const bool onRow = true) {
  const Ask a = placeAsk(rowTop(row), 2, liftY);
  EXPECT_GE(a.top, PAPER_TOP);
  EXPECT_LE(a.bottom, PAPER_BOTTOM);
  const int gap = a.yesTop >= liftY ? a.yesTop - liftY : liftY - (a.yesTop + ROW);
  EXPECT_GE(gap, ROW) << "row " << row;
  if (onRow) EXPECT_LT(std::abs(a.noTop - liftY), std::abs(a.yesTop - liftY)) << "leave it is the nearer";
  EXPECT_EQ(askAt(a, 240, a.yesTop + 5), AskSpot::Yes);
  EXPECT_EQ(askAt(a, 240, a.noTop + 5), AskSpot::No);
  EXPECT_EQ(askAt(a, 240, rowTop(row) + 30), AskSpot::Outside) << "the row crossed out is outside: a tap there keeps it";
}

TEST(TouchAsk, BinItStaysARowAwayFromTheFinger) {
  askClear(4, rowTop(4) + 56);
  askClear(0, rowTop(0) + 60);
  askClear(7, rowTop(7) + 50);
  askClear(5, rowTop(5) + 63);
  askClear(4, rowTop(5) + 40);  // lifted below the row it crossed
  askClear(2, rowTop(5) + 40, false);  // the second arm ran three rows down
}

TEST(TouchScribble, TheRowAnXOrARingAimsAt) {
  EXPECT_EQ(scribbleRow(90, rowTop(4) + 20, 8), 4);
  EXPECT_EQ(scribbleRow(90, rowTop(5) + 1, 5), -1) << "past the last row shown";
  EXPECT_EQ(scribbleRow(90, 100, 8), -1) << "on the title";
  EXPECT_EQ(scribbleRow(90, 700, 8), -1) << "on the foot";
}

TEST(TouchScribble, TheHintStaysUntilBothHaveBeenUsed) {
  EXPECT_TRUE(teachScribbles(0));
  EXPECT_TRUE(teachScribbles(USED_CROSS));
  EXPECT_TRUE(teachScribbles(USED_RING));
  EXPECT_FALSE(teachScribbles(USED_CROSS | USED_RING));
}

}  // namespace

// ---- the lines of abuse ----
#include "shells/ugly/UglyLogic.h"
#include "shells/ugly/UglyQuips.h"

namespace {
using namespace ugly::logic;
constexpr int QUIP_SLOTS = sizeof(ugly::quips::SLOTS) / sizeof(ugly::quips::SLOTS[0]);
constexpr int OPEN_PAGE = 0, OPEN_GROUP = 1, SET_VALUE = 2, SLEEP = 8;  // ugly::Quip

TEST(Quips, TheKeyIsTheGeneratorsKey) {
  // Values from scripts/ugly/gen_quips.py key16().
  EXPECT_EQ(quipKey("Hiển thị"), 8386);
  EXPECT_EQ(quipKey("Chế độ ban đêm", "Bật"), 45941);
  EXPECT_EQ(quipKey("a"), 52512);
}

TEST(Quips, EveryPageAndTheSettingsGroupsHaveLines) {
  for (int page = 0; page < 5; ++page) {
    const int s = quipSlot(ugly::quips::SLOTS, QUIP_SLOTS, OPEN_PAGE, page, 0);
    ASSERT_GE(s, 0) << page;
    EXPECT_EQ(ugly::quips::SLOTS[s].count, 3) << page;
  }
  EXPECT_GE(quipSlot(ugly::quips::SLOTS, QUIP_SLOTS, OPEN_GROUP, quipKey("Hiển thị"), 0), 0);
  EXPECT_GE(quipSlot(ugly::quips::SLOTS, QUIP_SLOTS, SET_VALUE, quipKey("Chế độ ban đêm", "Bật"), 0), 0);
  EXPECT_LT(quipSlot(ugly::quips::SLOTS, QUIP_SLOTS, SET_VALUE, quipKey("Chế độ ban đêm", "xanh"), 0), 0);
}

TEST(Quips, AConditionWinsElseThePlainLines) {
  // Sleep has no screen that says its lines yet (out of the blocks): its slots, by hand.
  constexpr ugly::quips::Slot slots[] = {{0, SLEEP << 4, 3}, {0, SLEEP << 4 | 1, 2}, {7, SLEEP << 4 | 1, 1}};
  const int zero = quipSlot(slots, 3, SLEEP, 0, 1);
  const int plain = quipSlot(slots, 3, SLEEP, 0, 0);
  EXPECT_EQ(zero, 1);
  EXPECT_EQ(plain, 0);
  EXPECT_EQ(quipSlot(slots, 3, SLEEP, 0, 9), plain) << "a condition without lines";
  EXPECT_LT(quipSlot(slots, 3, SLEEP, 5, 0), 0) << "a key without lines";
}

TEST(Quips, TheLinesTakeTurnsAndNeverTheLastOneTwice) {
  const auto id = [](int i) { return static_cast<uint32_t>(40 + i); };  // the lines of this slot are 40, 41, 42
  EXPECT_EQ(takeTurn(100, 3, 0xFFFF, id), 1);
  EXPECT_EQ(takeTurn(101, 3, 41, id), 2);
  EXPECT_EQ(takeTurn(102, 3, 42, id), 0);
  EXPECT_EQ(takeTurn(102, 3, 40, id), 1) << "the turn lands on the line shown last: the next one";
  EXPECT_EQ(takeTurn(102, 3, 40, id), 1);
  EXPECT_EQ(takeTurn(5, 1, 40, id), 0) << "one line: it has to repeat";
  EXPECT_EQ(takeTurn(5, 0, 40, id), -1);
  uint32_t last = 0xFFFF;
  for (uint32_t turn : {7u, 7u, 7u, 9u, 8u}) {  // a turn that stands still (a wake that lost it) or jumps back
    const uint32_t now = id(takeTurn(turn, 3, last, id));
    EXPECT_NE(now, last) << turn;
    last = now;
  }
}
}  // namespace
