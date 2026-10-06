#pragma once
// Where a finger lands on the tenor/ugly shell of a touch screen (X4 Pro, 480x800 portrait), and where
// the papers that open under it go. Pure and integer only, like UglyLogic.h, so a host test runs it.
//
// Everything sits on a 16 px grid. A touch target is at least 64 px (4 cells, 7.4 mm) tall.
#include <algorithm>

namespace ugly::touch {

inline constexpr int W = 480, H = 800;
inline constexpr int STATUS_H = 48;                   // the top band: date or title, clock, battery
inline constexpr int MARGIN_X = 48, TEXT_X = 64, TEXT_R = 464;
inline constexpr int TITLE_BASE = 104, SUB_BASE = 140;
inline constexpr int ROW = 64, LIST_TOP = 144, ROWS = 8;  // eight rows of 64 px, 144 to 656
inline constexpr int FOOT_TOP = 656, FOOT_BOTTOM = 720;   // "page 1/3": the next page of rows
inline constexpr int NAV_TOP = 720, NAV_BOTTOM = 784;     // three cells over the Home key
inline constexpr int NAV_CELL = 160;
inline constexpr int PAPER_TOP = 8, PAPER_BOTTOM = 784;   // a paper may cover the whole page
inline constexpr int PAPER_X0 = 24, PAPER_X1 = 468;

// ---- the notebook page ----
enum class Spot : unsigned char { None, Row, Foot, Prev, Back, Next };
struct Hit {
  Spot spot = Spot::None;
  int row = -1;  // the row of the page (0..ROWS-1) for Spot::Row
};
// Rows take the whole width, margin included; the bottom band splits in three.
inline Hit notebookAt(const int x, const int y) {
  if (y >= LIST_TOP && y < LIST_TOP + ROWS * ROW) return {Spot::Row, (y - LIST_TOP) / ROW};
  if (y >= FOOT_TOP && y < FOOT_BOTTOM) return {Spot::Foot, -1};
  if (y >= NAV_TOP && y < NAV_BOTTOM) return {x < NAV_CELL ? Spot::Prev : x < 2 * NAV_CELL ? Spot::Back : Spot::Next, -1};
  return {};
}
inline int rowTop(const int row) { return LIST_TOP + row * ROW; }

// ---- the diary: underlined words ----
// One underlined word as drawn: its baseline and its ink from x0 to x1.
struct Word {
  int base, x0, x1;
};
// The band of a line runs from 44 px above its baseline to 20 below, so lines 64 px apart tile the page.
// A word alone on its line owns the whole width; words sharing a line split it halfway across the gap
// between them. Returns the index in `words`, or -1.
inline int wordAt(const Word* words, const int count, const int x, const int y) {
  int best = -1;
  for (int i = 0; i < count; ++i) {
    if (y < words[i].base - 44 || y >= words[i].base + 20) continue;
    if (best < 0) {
      best = i;
      continue;
    }
    // Two words on the line: the one left of the split keeps the touch.
    const Word& a = words[best].x0 <= words[i].x0 ? words[best] : words[i];
    const int left = &a == &words[best] ? best : i, right = left == best ? i : best;
    const int split = (a.x1 + (left == best ? words[i] : words[best]).x0) / 2;
    best = x < split ? left : right;
  }
  return best;
}

// ---- the desk: one object a cell, the cell is the target ----
// The cells in the order of UglyDesk's objects (STATS, RECENT, READING, FOLDER, FAVORITES, SETTINGS).
struct Cell {
  int x0, y0, x1, y1;
};
inline constexpr Cell DESK_CELLS[6] = {{320, 48, 480, 288}, {160, 48, 320, 288}, {0, 288, 480, 544},
                                       {0, 544, 240, 720},  {240, 544, 480, 720}, {0, 48, 160, 288}};
inline int deskAt(const int x, const int y) {
  for (int i = 0; i < 6; ++i) {
    const Cell& c = DESK_CELLS[i];
    if (x >= c.x0 && x < c.x1 && y >= c.y0 && y < c.y1) return i;
  }
  return -1;
}

// ---- a paper of values, opened on the row touched ----
// The value chosen sits on the row the finger is on, always: the paper never moves to make room. A side
// short of room is torn, keeping a band "n more" that scrolls the paper by a page.
inline constexpr int PAPER_LABEL = 40, PAPER_MORE = 64;
struct Paper {
  int top = 0, bottom = 0;  // the paper, edge to edge
  int anchor = 0;           // the value that sits on the row touched ...
  int anchorTop = 0;        // ... at this y
  int first = 0, last = 0;  // values shown
  int hiddenAbove = 0, hiddenBelow = 0;
};
inline int paperRowTop(const Paper& p, const int value) { return p.anchorTop + (value - p.anchor) * ROW; }

inline Paper placePaper(const int anchorTop, const int count, const int anchor) {
  Paper p;
  p.anchor = std::clamp(anchor, 0, std::max(0, count - 1));
  p.anchorTop = anchorTop;
  const int upRoom = anchorTop - PAPER_TOP;
  int visUp;
  if (p.anchor * ROW + PAPER_LABEL <= upRoom) {
    visUp = p.anchor;
    p.top = anchorTop - visUp * ROW - PAPER_LABEL;
  } else {
    visUp = std::max(0, (upRoom - PAPER_MORE) / ROW);
    p.hiddenAbove = p.anchor - visUp;
    p.top = anchorTop - visUp * ROW - PAPER_MORE;
  }
  const int below = count - 1 - p.anchor;
  const int downRoom = PAPER_BOTTOM - (anchorTop + ROW) - 12;
  int visDown;
  if (below * ROW <= downRoom) {
    visDown = below;
    p.bottom = anchorTop + ROW + below * ROW - 4;
  } else {
    visDown = std::max(0, (downRoom + 12 - PAPER_MORE) / ROW);
    p.hiddenBelow = below - visDown;
    p.bottom = anchorTop + ROW + visDown * ROW + PAPER_MORE;
  }
  p.first = p.anchor - visUp;
  p.last = p.anchor + visDown;
  return p;
}

enum class PaperSpot : unsigned char { Outside, Inside, Value, MoreAbove, MoreBelow };
inline PaperSpot paperAt(const Paper& p, const int x, const int y, int& value) {
  value = -1;
  if (x < PAPER_X0 || x >= PAPER_X1 || y < p.top || y >= p.bottom) return PaperSpot::Outside;
  if (p.hiddenAbove > 0 && y < p.top + PAPER_MORE) return PaperSpot::MoreAbove;
  if (p.hiddenBelow > 0 && y >= p.bottom - PAPER_MORE) return PaperSpot::MoreBelow;
  for (int k = p.first; k <= p.last; ++k) {
    const int t = paperRowTop(p, k);
    if (y >= t && y < t + ROW) {
      value = k;
      return PaperSpot::Value;
    }
  }
  return PaperSpot::Inside;
}
// The paper turned by a band "n more": the first value hidden on that side takes the row touched.
inline Paper scrollPaper(const Paper& p, const int count, const bool down) {
  return placePaper(p.anchorTop, count, down ? p.last + 1 : p.first - 1);
}

// A paper's rubbed-out area, widened so that no row below it is left with half its letters: an edge
// that falls inside a row's ink moves to that row's edge.
inline int rubEdge(const int y, const bool upper) {
  for (int k = 0; k <= ROWS + 1; ++k) {  // the list rows, the foot, the bottom band
    const int a = k < ROWS ? rowTop(k) : k == ROWS ? FOOT_TOP : NAV_TOP;
    const int b = a + ROW;
    if (a + 10 < y && y < b - 8) return upper ? a : b;
  }
  return y;
}

// ---- how a value is changed by a touch ----
// Two values: a box ticks. Three: the next one. Four and more: a paper of them (the same count
// settingstabs::moTrinhChon opens a picker at).
enum class Change : unsigned char { Tick, Next, Paper };
inline Change changeFor(const int values, const bool opensPicker) {
  if (opensPicker) return Change::Paper;
  return values == 2 ? Change::Tick : Change::Next;
}

// ---- asking before a delete ----
// The paper opens under the row crossed out, or over it when there is no room below. "leave it" is the
// one nearer the finger; "bin it" is at least a row away from where the finger lifted.
struct Ask {
  int top, bottom;  // the paper
  int textBase;     // baseline of the first line of the question
  int noTop, yesTop;
};
inline constexpr int ASK_LINE = 40;
inline Ask placeAsk(const int rowTopY, const int lines, const int liftY) {
  const int height = 16 + ASK_LINE * lines + ROW + 24 + ROW + 12;
  Ask a{};
  // Below: question, "leave it", a gap, "bin it".
  a.top = rowTopY + ROW + 6;
  a.textBase = a.top + 40;
  a.noTop = a.top + 16 + ASK_LINE * lines;
  a.yesTop = std::max(a.noTop + ROW + 24, liftY + ROW);
  a.bottom = a.yesTop + ROW + 12;
  if (a.bottom <= PAPER_BOTTOM) return a;
  // Over: question, "bin it", a gap, "leave it" next to the row.
  a.bottom = rowTopY - 6;
  a.top = a.bottom - height;
  a.textBase = a.top + 40;
  a.yesTop = a.top + 16 + ASK_LINE * lines;
  a.noTop = a.yesTop + ROW + 24;
  const int lowest = std::min(liftY - 2 * ROW, a.yesTop);  // keep "bin it" a row clear of the finger
  if (lowest < a.yesTop) {
    const int lift = a.yesTop - lowest;
    a.top -= lift;
    a.textBase -= lift;
    a.yesTop -= lift;
  }
  return a;
}
enum class AskSpot : unsigned char { Outside, Inside, No, Yes };
inline AskSpot askAt(const Ask& a, const int x, const int y) {
  if (x < PAPER_X0 || x >= PAPER_X1 || y < a.top || y >= a.bottom) return AskSpot::Outside;
  if (y >= a.noTop && y < a.noTop + ROW) return AskSpot::No;
  if (y >= a.yesTop && y < a.yesTop + ROW) return AskSpot::Yes;
  return AskSpot::Inside;
}

// ---- a scribble aimed at a row ----
// The row of the page an X's crossing or a ring's middle falls on, or -1 when it falls off the list.
inline int scribbleRow(const int x, const int y, const int rowsShown) {
  const Hit h = notebookAt(x, y);
  return h.spot == Spot::Row && h.row < rowsShown ? h.row : -1;
}

// ---- what a scribble does to the row it lands on ----
// The one place for the rule: the page asks it on a scribble, the hint asks it before teaching.
enum class Sheet : unsigned char { Folder, Recent, Favorites, Other };
enum class Mark : unsigned char { Erase, Keep };  // an X, a ring
enum class Act : unsigned char { None, TogglePin, Unpin, AskDelete, NotHere };
inline Act scribbleAct(const Sheet sheet, const Mark mark, const bool deletableFile) {
  switch (sheet) {
    case Sheet::Folder:
      return mark == Mark::Keep ? Act::TogglePin : deletableFile ? Act::AskDelete : Act::NotHere;
    case Sheet::Recent:
      return mark == Mark::Keep ? Act::TogglePin : Act::NotHere;
    case Sheet::Favorites:  // every row is pinned: either mark takes it off
      return Act::Unpin;
    case Sheet::Other:
      break;
  }
  return Act::None;
}
// The hint "an X bins it, a ring pins it" (STR_UGLY_X4_HINT_GESTURE) shows only on a page where both hold.
inline bool hintHolds(const Sheet sheet) {
  return scribbleAct(sheet, Mark::Erase, true) == Act::AskDelete && scribbleAct(sheet, Mark::Keep, false) == Act::TogglePin;
}

// The hint under the title teaches the two scribbles until each has been used once: two bits.
inline constexpr unsigned USED_CROSS = 1, USED_RING = 2;
inline bool teachScribbles(const unsigned used) { return (used & (USED_CROSS | USED_RING)) != (USED_CROSS | USED_RING); }

}  // namespace ugly::touch
