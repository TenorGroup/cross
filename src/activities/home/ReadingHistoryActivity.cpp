#include "ReadingHistoryActivity.h"

#include "activities/X3ActivityLayout.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <new>
#include <string>

#include "ReadingStatsStore.h"
#include "components/ReadingStatsFormat.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/themes/TenorRadius.h"
#include "fontIds.h"
#include "shells/Shell.h"
#include "shells/ugly/UglyInk.h"
#include "util/Lich30.h"

const char* ReadingHistoryActivity::headerTitle() const {
  return hourPage() ? tr(STR_STATS_BY_HOUR) : tr(STR_STATS_12_WEEKS);
}

void ReadingHistoryActivity::onEnter() {
  RenderLock lock(*this);
  today = ReadingStatsStore::habitStamp().day;
  history.reset(new (std::nothrow) readinghistory::Snapshot());
  ready = today && history && READING_STATS.statisticsReadable && READING_STATS.loadHistory(*history);
  if (ready) {
    showPage(0);
  } else {
    history.reset();
    count = 1;
    labels[0] = !today ? tr(STR_STATS_UNDATED) : tr(STR_STATS_READ_ERROR);
    rows[0].label = labels[0].c_str();
    rows[0].value = "";
    rows[0].actionValue = 0;
  }
  UiListActivity::onEnter();
}

// The page's cells, weeks and list rows. Days after today have no cell and no row.
void ReadingHistoryActivity::showPage(const int next) {
  page = std::clamp(next, 0, lichdai::SO_TRANG - 1);
  char text[96];
  count = 0;
  // `counted` names what the subtitle counts: page turns for a day, sessions begun for an hour.
  const auto row = [&](const std::string& label, const uint64_t ms, const uint32_t number, const char* counted) {
    labels[count] = label;
    if (ms) {
      readingstatsview::duration(ms, text, sizeof(text));
      values[count] = text;
    } else {
      values[count] = tr(STR_STATS_NOT_RECORDED);
    }
    subtitles[count].clear();
    if (number) {
      snprintf(text, sizeof(text), "%s: %lu", counted, static_cast<unsigned long>(number));
      subtitles[count] = text;
    }
    ++count;
  };
  if (hourPage()) {
    for (int h = 0; h < 24; ++h) {
      snprintf(text, sizeof(text), "%02d:00", h);
      row(text, history->hourMs[h], history->hourSessions[h], tr(STR_STATS_SESSION_COUNT));
    }
  } else {
    const uint32_t first = lichdai::dau(today, page), last = std::min(first + lichdai::SO_O - 1, today);
    todayCell = -1;
    for (int i = 0; i < lichdai::SO_O; ++i) {
      const auto* n = lichdai::ngay(*history, first + i);
      const uint32_t ms = n ? n->phut * 60000u + n->leMs : 0;
      // A day with turns and no time is still a day read.
      cells[i] = n ? lich30::muc(ms == 0 && n->trang ? 1 : ms) : 0;
      if (first + i == today) todayCell = i;
    }
    lichdai::tuan(*history, first, weeks);
    firstKey = habits::dateKey(first);
    lastKey = habits::dateKey(last);
    for (uint32_t d = last; d >= first; --d) {
      const uint32_t key = habits::dateKey(d);
      snprintf(text, sizeof(text), "%02lu/%02lu/%04lu", static_cast<unsigned long>(key % 100),
               static_cast<unsigned long>(key / 100 % 100), static_cast<unsigned long>(key / 10000));
      const auto* n = lichdai::ngay(*history, d);
      row(text, n ? static_cast<uint64_t>(n->phut) * 60000 + n->leMs : 0, n ? n->trang : 0, tr(STR_STATS_TURNS));
    }
  }
  for (int i = 0; i < count; ++i) {
    rows[i].label = labels[i].c_str();
    rows[i].value = values[i].c_str();
    rows[i].subtitle = subtitles[i].empty() ? nullptr : subtitles[i].c_str();
    rows[i].actionValue = i;
  }
  auto& n = activeNav();
  n.selected = 0;
  n.top = 0;
  // Another page is another row set: the last page's measured rows say nothing about this one.
  n.drawnRows = 0;
  n.drawnCount = 0;
  n.followOnBuild = true;
}

bool ReadingHistoryActivity::handleCustomInput() {
  if (!ready) return false;
  int step = 0;
  if (tenorchrome::kTouchShell) {
    const auto swipe = mappedInput.wasSwipe();
    if (!mappedInput.wasBackGesture()) {
      if (swipe == MappedInputManager::SwipeDir::Left) step = 1;
      if (swipe == MappedInputManager::SwipeDir::Right) step = -1;
    }
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    step = 1;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    step = -1;
  }
  if (!step) return false;
  if (page + step < 0 || page + step >= lichdai::SO_TRANG) return true;
  {
    RenderLock lock(*this);
    showPage(page + step);
  }
  requestUpdate();
  return true;
}

void ReadingHistoryActivity::drawPage() const {
  if (hourPage()) {
    drawHourChart();
  } else {
    drawGrid();
    drawWeekChart();
  }
}
void ReadingHistoryActivity::drawChrome() {
  if (tenorchrome::x3ScreenLayoutEnabled()) {
    const auto layout = tenorchrome::x3ScreenLayout(renderer, tenorchrome::tipHeight(renderer, nullptr, 8));
    x3activity::drawBar(renderer, layout.barRect, headerTitle(), navigationPrefix.c_str());
    if (ready) drawPage();
    return;
  }
  if (tenorchrome::enabled())
    tenorchrome::drawHeader(renderer, headerTitle(), tr(STR_HOME_TAB_STATS));
  else
    UiListActivity::drawChrome();
  if (ready) drawPage();
}

int ReadingHistoryActivity::gridTop() const {
  const auto& m = UITheme::getInstance().getMetrics();
  if (tenorchrome::x3ScreenLayoutEnabled()) return tenorchrome::x3ScreenLayout(renderer).bodyRect.y + 4;
  return (tenorchrome::enabled() ? tenorchrome::tabTop() : m.topPadding + m.headerHeight) + 12;
}
static int pitch(const GfxRenderer& renderer) { return (renderer.getScreenWidth() - 48) / 7; }
// The weekday line, 4 rows of cells and the line under them (dates of the page); none on the page by hour.
// tenor/ugly circles today by hand, 4 px out plus the pen's swing: the dates under the grid stand this much lower there.
static int rimGap() { return shell::uglyParts() ? 12 : 0; }
int ReadingHistoryActivity::gridHeight() const {
  if (hourPage()) return 0;
  const int line = renderer.getLineHeight(SMALL_FONT_ID);
  return line + 4 + lichdai::SO_TUAN * pitch(renderer) + rimGap() + line + 12;
}

// A short label centred on `cx`, its top at `top`: the pen's hand in tenor/ugly, the caption face otherwise.
static void label(const GfxRenderer& renderer, const int cx, const int top, const char* text) {
  if (shell::uglyParts()) {
    ugly::text(renderer, ugly::Size::S22, cx - ugly::width(renderer, ugly::Size::S22, text) / 2,
               top + ugly::ascent(ugly::Size::S22), text);
    return;
  }
  renderer.drawText(SMALL_FONT_ID, cx - renderer.getTextWidth(SMALL_FONT_ID, text) / 2, top, text);
}

// Each chart: reading time as bars on a y axis at the left, a second measure as a line on its own y axis at the right
// (the weeks: minutes a reading session; the hours: sessions begun in the hour). Both
// axes are numbered at 0, the middle and a round top (lichdai::dinh), each unit written once over its axis; a two-line
// legend above names the bar and the line. The line runs on a white margin and each point is a black dot ringed in
// white, so neither hides in a black bar; the figures stand outside the plot, where the line never goes.
namespace {
constexpr int WEEK_PLOT = 72;
constexpr int HOUR_PLOT = 132;
struct Series {
  int n;
  const uint64_t* ms;
  const uint32_t* line;        // the second measure, one a slot
  const bool* point;           // false: no point in that slot, and the line breaks there
  const char* lineLegend;      // the words after the line in the legend
  const char* lineUnit;        // written once over the right axis
  int barPercent;              // bar width, in % of its slot
  const char* const* xLabels;  // one a slot, nullptr for none
};
int textLine(const GfxRenderer& renderer) { return shell::uglyParts() ? 26 : renderer.getLineHeight(SMALL_FONT_ID); }
int textWidth(const GfxRenderer& renderer, const char* text) {
  return shell::uglyParts() ? ugly::width(renderer, ugly::Size::S22, text) : renderer.getTextWidth(SMALL_FONT_ID, text);
}
// Text with its top at `top`: the pen's hand in tenor/ugly, the body face (caption size) otherwise.
void write(const GfxRenderer& renderer, const int x, const int top, const char* text) {
  if (shell::uglyParts()) ugly::text(renderer, ugly::Size::S22, x, top + ugly::ascent(ugly::Size::S22), text);
  else renderer.drawText(SMALL_FONT_ID, x, top, text);
}
void stroke(const GfxRenderer& renderer, const int x0, const int y0, const int x1, const int y1, const uint32_t seed) {
  if (shell::uglyParts()) ugly::line(renderer, x0, y0, x1, y1, seed, 2);
  else renderer.drawLine(x0, y0, x1, y1, 2, true);
}
void dot(const GfxRenderer& renderer, const int cx, const int cy) {
  renderer.fillRoundedRect(cx - 5, cy - 5, 11, 11, 5, Color::White);
  renderer.fillRoundedRect(cx - 3, cy - 3, 7, 7, 3, Color::Black);
}
// Legend (2 lines), units (1), half a line so the top figures clear the units, the plot, the x labels (1).
int chartBlockHeight(const GfxRenderer& renderer, const int plot) { return 5 * textLine(renderer) + plot + 8; }

void drawChartBlock(const GfxRenderer& renderer, const int top, const int plot, const Series& s) {
  const int w = renderer.getScreenWidth(), line = textLine(renderer), margin = 24;
  uint64_t mostMs = 0;
  uint32_t mostLine = 0;
  for (int i = 0; i < s.n; ++i) {
    mostMs = std::max(mostMs, s.ms[i]);
    if (s.point[i]) mostLine = std::max(mostLine, s.line[i]);
  }
  const auto time = lichdai::trucThoiGian(mostMs);
  const uint32_t lineTop = lichdai::dinh(mostLine);
  char text[24];
  // Legend: a small bar, then a short line through a dot, each before its words.
  const int legendX = margin + 26;
  renderer.fillRect(margin + 5, top + line / 2 - 6, 8, 12, true);
  write(renderer, legendX, top, time.gio ? tr(STR_STATS_LEGEND_HOURS) : tr(STR_STATS_LEGEND_MINUTES));
  stroke(renderer, margin, top + line + line / 2, margin + 18, top + line + line / 2, 7);
  dot(renderer, margin + 9, top + line + line / 2);
  write(renderer, legendX, top + line, s.lineLegend);
  // Axes: figures right-aligned left of the left axis, left-aligned right of the right axis.
  snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(time.dinh));
  const int leftWidth = textWidth(renderer, text);
  snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(lineTop));
  const int rightWidth = textWidth(renderer, text);
  const int x0 = margin + leftWidth + 8, x1 = w - margin - rightWidth - 8;
  const int plotTop = top + 3 * line + line / 2, base = plotTop + plot;
  write(renderer, margin, top + 2 * line, time.gio ? tr(STR_STATS_HOURS) : tr(STR_STATS_MINUTES));
  write(renderer, w - margin - textWidth(renderer, s.lineUnit), top + 2 * line, s.lineUnit);
  stroke(renderer, x0, plotTop, x0, base, 11);
  stroke(renderer, x1, plotTop, x1, base, 12);
  stroke(renderer, x0, base, x1, base, 13);
  const uint32_t leftTicks[3] = {0, time.dinh / 2, time.dinh}, rightTicks[3] = {0, lineTop / 2, lineTop};
  for (int t = 0; t < 3; ++t) {
    const int y = base - t * plot / 2;
    stroke(renderer, x0 - 5, y, x0, y, 20 + t);
    stroke(renderer, x1, y, x1 + 5, y, 30 + t);
    snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(leftTicks[t]));
    write(renderer, x0 - 8 - textWidth(renderer, text), y - line / 2, text);
    snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(rightTicks[t]));
    write(renderer, x1 + 8, y - line / 2, text);
  }
  // Bars, each centred in its slot, its name under the base.
  const int slot = (x1 - x0 - 8) / s.n, bar = std::max(2, slot * s.barPercent / 100), first = x0 + 4 + slot / 2;
  for (int i = 0; i < s.n; ++i) {
    const int cx = first + i * slot;
    const int height = s.ms[i] ? std::max<int>(2, static_cast<int>(s.ms[i] * plot / time.ms())) : 0;
    if (height && shell::uglyParts()) {
      // tenor/ugly: the bar scribbled with the pen, stroke by stroke.
      for (int dx = 0; dx < bar; dx += 2)
        ugly::line(renderer, cx - bar / 2 + dx, base, cx - bar / 2 + dx + 1, base - height, 70 + i * 16 + dx, 2);
    } else if (height) {
      renderer.fillRect(cx - bar / 2, base - height, bar, height);
    }
    if (s.xLabels[i]) write(renderer, cx - textWidth(renderer, s.xLabels[i]) / 2, base + 4, s.xLabels[i]);
  }
  // The line over the bars: a white margin first, then the black stroke, then the dots. A slot without a point breaks
  // the line on both sides.
  const auto y = [&](const int i) { return base - static_cast<int>(static_cast<uint64_t>(s.line[i]) * plot / lineTop); };
  for (int i = 1; i < s.n; ++i)
    if (s.point[i - 1] && s.point[i])
      renderer.drawLine(first + (i - 1) * slot, y(i - 1), first + i * slot, y(i), 6, false);
  for (int i = 1; i < s.n; ++i)
    if (s.point[i - 1] && s.point[i]) stroke(renderer, first + (i - 1) * slot, y(i - 1), first + i * slot, y(i), 40 + i);
  for (int i = 0; i < s.n; ++i)
    if (s.point[i]) dot(renderer, first + i * slot, y(i));
}
}  // namespace

int ReadingHistoryActivity::chartHeight() const {
  return chartBlockHeight(renderer, hourPage() ? HOUR_PLOT : WEEK_PLOT);
}

// The 4 weeks of the page, each named by its Monday.
void ReadingHistoryActivity::drawWeekChart() const {
  uint64_t ms[lichdai::SO_TUAN];
  uint32_t minutes[lichdai::SO_TUAN];
  bool point[lichdai::SO_TUAN];
  char names[lichdai::SO_TUAN][8];
  const char* labels[lichdai::SO_TUAN];
  for (int k = 0; k < lichdai::SO_TUAN; ++k) {
    ms[k] = weeks[k].ms;
    minutes[k] = 0;
    point[k] = lichdai::trungBinh(weeks[k], minutes[k]);
    const uint32_t key = habits::dateKey(lichdai::dau(today, page) + 7 * k);
    snprintf(names[k], sizeof(names[k]), "%02lu/%02lu", static_cast<unsigned long>(key % 100),
             static_cast<unsigned long>(key / 100 % 100));
    labels[k] = names[k];
  }
  drawChartBlock(renderer, gridTop() + gridHeight(), WEEK_PLOT,
                 {lichdai::SO_TUAN, ms, minutes, point, tr(STR_STATS_LEGEND_MEAN_SESSION),
                  tr(STR_STATS_MINUTES_PER_SESSION), 30, labels});
}

// The 24 hours of the local day; 0, 6, 12 and 18 named.
void ReadingHistoryActivity::drawHourChart() const {
  static const char* const labels[24] = {"0", nullptr, nullptr, nullptr, nullptr, nullptr, "6",  nullptr,
                                         nullptr, nullptr, nullptr, nullptr, "12", nullptr, nullptr, nullptr,
                                         nullptr, nullptr, "18", nullptr, nullptr, nullptr, nullptr, nullptr};
  static const bool every[24] = {true, true, true, true, true, true, true, true, true, true, true, true,
                                 true, true, true, true, true, true, true, true, true, true, true, true};
  drawChartBlock(renderer, gridTop(), HOUR_PLOT,
                 {24, history->hourMs.data(), history->hourSessions.data(), every, tr(STR_STATS_LEGEND_SESSIONS),
                  tr(STR_STATS_SESSIONS_UNIT), 50, labels});
}

// 7 columns centred, Monday first under the weekday line, 4 rows; the oldest week on top, today framed.
void ReadingHistoryActivity::drawGrid() const {
  const int cols = 7, step = pitch(renderer), side = step - 8, radius = tenorradius::tile(side),
            line = renderer.getLineHeight(SMALL_FONT_ID), top = gridTop() + line + 4,
            left = (renderer.getScreenWidth() - cols * step) / 2;
  constexpr Color shade[] = {Color::White, Color::LightGray, Color::DarkGray, Color::Black};
  const bool handwritten = shell::uglyParts();
  const std::string names = tr(STR_STATS_WEEKDAYS);
  size_t from = 0;
  for (int c = 0; c < cols && from <= names.size(); ++c) {
    const size_t to = std::min(names.find(' ', from), names.size());
    label(renderer, left + c * step + 4 + side / 2, gridTop(), names.substr(from, to - from).c_str());
    from = to + 1;
  }
  const uint32_t first = lichdai::dau(today, page);
  for (int i = 0; i < lichdai::SO_O; ++i) {
    if (first + i > today) continue;  // the rest of this week: no cell yet
    const int x = left + i % cols * step + 4, y = top + i / cols * step;
    if (handwritten) {
      // tenor/ugly: each day a square in 4 shaky strokes, read time hatched in: one way, both ways, then filled.
      const uint32_t seed = 100 + i * 8;
      ugly::line(renderer, x, y, x + side, y + 1, seed, 2);
      ugly::line(renderer, x + side, y, x + side - 1, y + side, seed + 1, 2);
      ugly::line(renderer, x + side, y + side, x, y + side - 1, seed + 2, 2);
      ugly::line(renderer, x, y + side, x + 1, y, seed + 3, 2);
      const int quarter = side / 4;
      for (int k = 1; cells[i] >= 1 && k < 4; ++k)
        ugly::line(renderer, x + 2, y + k * quarter, x + k * quarter, y + 2, seed + 4, 1);
      for (int k = 1; cells[i] >= 2 && k < 4; ++k)
        ugly::line(renderer, x + side - k * quarter, y + 2, x + side - 2, y + k * quarter, seed + 5, 1);
      if (cells[i] >= 3) renderer.fillRect(x + 4, y + 4, side - 8, side - 8, true);
    } else if (cells[i]) {
      // One rounded block of the day's shade. Only an empty day gets a rim: grey, 2 px deep, round a white cell.
      renderer.fillRoundedRect(x, y, side, side, radius, shade[cells[i]]);
    } else {
      renderer.fillRoundedRect(x, y, side, side, radius, Color::DarkGray);
      renderer.fillRoundedRect(x + 2, y + 2, side - 4, side - 4, tenorradius::nest(radius, 2), Color::White);
    }
    if (i != todayCell) continue;
    // Today wears a black frame set off by a 2 px gap, concentric with its corner; a pen circle in tenor/ugly.
    if (handwritten)
      ugly::circle(renderer, ugly::Circle::Object, {x, y, x + side, y + side}, 4, 4, mark_.get(firstKey), 2);
    else
      renderer.drawRoundedRect(x - 3, y - 3, side + 6, side + 6, 1, tenorradius::container(radius, 3), true);
  }
  char range[40];
  snprintf(range, sizeof(range), "%02lu/%02lu - %02lu/%02lu  %d/%d", static_cast<unsigned long>(firstKey % 100),
           static_cast<unsigned long>(firstKey / 100 % 100), static_cast<unsigned long>(lastKey % 100),
           static_cast<unsigned long>(lastKey / 100 % 100), page + 1, lichdai::SO_TRANG);
  tenorchrome::drawInfoInRect(renderer, range,
                              {0, top + lichdai::SO_TUAN * step + 2 + rimGap(), renderer.getScreenWidth(), line}, 1);
}

void ReadingHistoryActivity::buildScreen(UiScreen& screen) {
  const auto& m = UITheme::getInstance().getMetrics();
  if (tenorchrome::x3ScreenLayoutEnabled()) {
    const auto layout = tenorchrome::x3ScreenLayout(renderer, tenorchrome::tipHeight(renderer, nullptr, 8));
    screen.setContentMarginFromScreen({static_cast<int16_t>(ready ? gridTop() + gridHeight() + chartHeight() : layout.bodyRect.y), 0,
                                       static_cast<int16_t>(renderer.getScreenHeight() - layout.bodyRect.bottom()), 0});
  } else {
    screen.setContentMarginFromScreen(
        {static_cast<int16_t>(ready ? gridTop() + gridHeight() + chartHeight() : m.topPadding + m.headerHeight), 0,
         static_cast<int16_t>(m.buttonHintsHeight), 0});
  }
  freeink::ui::ListProps props;
  props.items = rows.data();
  props.count = count;
  props.action = ACTION_ROW;
  props.labelText = uiMenuLabelText(screen.theme());
  syncListViewport(screen, props, true);
  screen.list(props);
}
