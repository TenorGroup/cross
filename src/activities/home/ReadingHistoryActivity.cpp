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
  const auto row = [&](const std::string& label, const uint64_t ms, const uint32_t turns) {
    labels[count] = label;
    if (ms) {
      readingstatsview::duration(ms, text, sizeof(text));
      values[count] = text;
    } else {
      values[count] = tr(STR_STATS_NOT_RECORDED);
    }
    subtitles[count].clear();
    if (turns) {
      snprintf(text, sizeof(text), "%s: %lu", tr(STR_STATS_TURNS), static_cast<unsigned long>(turns));
      subtitles[count] = text;
    }
    ++count;
  };
  if (hourPage()) {
    for (int h = 0; h < 24; ++h) {
      snprintf(text, sizeof(text), "%02d:00", h);
      row(text, history->hourMs[h], history->hourTurns[h]);
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
      row(text, n ? static_cast<uint64_t>(n->phut) * 60000 + n->leMs : 0, n ? n->trang : 0);
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
int ReadingHistoryActivity::gridHeight() const {
  if (hourPage()) return 0;
  const int line = renderer.getLineHeight(SMALL_FONT_ID);
  return line + 4 + lichdai::SO_TUAN * pitch(renderer) + line + 12;
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

constexpr int CHART_BAR_MAX = 36;
constexpr int HOUR_BAR_MAX = 96;
int ReadingHistoryActivity::chartHeight() const {
  const int line = renderer.getLineHeight(SMALL_FONT_ID);
  return (hourPage() ? HOUR_BAR_MAX : CHART_BAR_MAX) + line + 12;
}

// Weekly reading under the grid: 4 bars, one a week of the page, each named by its Monday.
void ReadingHistoryActivity::drawWeekChart() const {
  const int left = 24, width = renderer.getScreenWidth() - 2 * left, step = width / lichdai::SO_TUAN, bar = step / 8,
            base = gridTop() + gridHeight() + CHART_BAR_MAX;
  const bool handwritten = shell::uglyParts();
  if (handwritten) ugly::line(renderer, left, base, left + width, base - 2, 61, 2);
  else renderer.drawLine(left, base, left + width, base, true);
  uint64_t most = 1;
  for (const auto& w : weeks) most = std::max(most, w.ms);
  char text[8];
  for (int k = 0; k < lichdai::SO_TUAN; ++k) {
    const int x = left + k * step + (step - bar) / 2,
              height = weeks[k].ms ? std::max<int>(2, weeks[k].ms * CHART_BAR_MAX / most) : 0;
    if (height) renderer.fillRect(x, base - height, bar, height);
    const uint32_t key = habits::dateKey(lichdai::dau(today, page) + 7 * k);
    snprintf(text, sizeof(text), "%02lu/%02lu", static_cast<unsigned long>(key % 100),
             static_cast<unsigned long>(key / 100 % 100));
    label(renderer, x + bar / 2, base + 3, text);
  }
}

// Reading by hour: one thin bar an hour of the local day, the busiest hour full height; 0, 6, 12, 18 named below.
void ReadingHistoryActivity::drawHourChart() const {
  const int left = 24, width = renderer.getScreenWidth() - 2 * left, step = width / 24, bar = step / 2,
            base = gridTop() + HOUR_BAR_MAX;
  const bool handwritten = shell::uglyParts();
  if (handwritten) ugly::line(renderer, left, base, left + width, base - 2, 61, 2);
  else renderer.drawLine(left, base, left + width, base, true);
  uint64_t most = 1;
  for (const uint64_t ms : history->hourMs) most = std::max(most, ms);
  char text[8];
  for (int h = 0; h < 24; ++h) {
    const int x = left + h * step + (step - bar) / 2,
              height = history->hourMs[h] ? std::max<int>(2, history->hourMs[h] * HOUR_BAR_MAX / most) : 0;
    if (height) renderer.fillRect(x, base - height, bar, height);
    if (h % 6) continue;
    snprintf(text, sizeof(text), "%d", h);
    label(renderer, x + bar / 2, base + 3, text);
  }
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
                              {0, top + lichdai::SO_TUAN * step + 2, renderer.getScreenWidth(), line}, 1);
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
