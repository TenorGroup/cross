#include "UglyNote.h"

#include <Logging.h>

#include <algorithm>

#include "fontIds.h"

namespace ugly {
namespace {
constexpr int TEXT_X = NOTE_X, MARGIN_X = 26;  // the notebook's margin
constexpr int LINE_HEIGHT = 44, DETAIL_HEIGHT = 32, BAR_H = 26;
}  // namespace

void notePaper(const GfxRenderer& r, const char* title) {
  r.clearScreen();
  ugly::line(r, MARGIN_X, 0, MARGIN_X + 1, r.getScreenHeight() - 80, 501);
  // S38: the S52 pen only knows the letters of the Home page titles.
  const int tw = text(r, Size::S38, TEXT_X, 74, fit(r, Size::S38, title, r.getScreenWidth() - TEXT_X - 30).c_str());
  underline(r, TEXT_X, TEXT_X + tw, 88, 17, 3);
}

void notePage(const GfxRenderer& r, const MappedInputManager& input, const char* title, const char* line,
              const char* detail, const int percent, const Hints hints, const HalDisplay::RefreshMode mode) {
  [[maybe_unused]] const uint32_t started = millis();
  notePaper(r, title);
  const int w = r.getScreenWidth(), h = r.getScreenHeight();
  const int room = w - TEXT_X - 30;

  // The sentence, the line under it and the bar sit as one block in the middle of the paper.
  const int lines = line ? paragraph(r, Size::S30, 0, 0, room, LINE_HEIGHT, line, false) : 0;
  const int block = lines * LINE_HEIGHT + (detail ? DETAIL_HEIGHT : 0) + (percent >= 0 ? BAR_H + 24 : 0);
  int y = std::max(150, (h - block) / 2) + ascent(Size::S30);
  if (line) paragraph(r, Size::S30, TEXT_X, y, room, LINE_HEIGHT, line);
  y += lines * LINE_HEIGHT;
  if (detail) {
    r.drawText(UI_10_FONT_ID, TEXT_X, y - 20, r.truncatedText(UI_10_FONT_ID, detail, room).c_str());
    y += DETAIL_HEIGHT;
  }
  if (percent >= 0) {
    // A shaky box, hatched by pen as far as the work went.
    const int x0 = TEXT_X, x1 = w - 30, top = y - 6, bottom = top + BAR_H;
    ugly::line(r, x0, top, x1, top + 2, 71, 2);
    ugly::line(r, x1, top + 2, x1 + 1, bottom, 72, 2);
    ugly::line(r, x1 + 1, bottom, x0, bottom + 1, 73, 2);
    ugly::line(r, x0, bottom + 1, x0, top, 74, 2);
    const int fill = x0 + (x1 - x0) * std::min(percent, 100) / 100;
    for (int x = x0 + 4; x + 8 <= fill; x += 7) ugly::line(r, x, bottom - 3, x + 8, top + 4, 80 + x, 1);
  }
  statusBar(r, input, hints);
  r.displayBuffer(mode);
#ifdef UGLY_FRAME_LOG
  LOG_INF("UGLY", "Note frame title=\"%s\" line=\"%s\" percent=%d total=%lums heap=%u", title, line ? line : "", percent,
          static_cast<unsigned long>(millis() - started), ESP.getFreeHeap());
#endif
}

}  // namespace ugly
