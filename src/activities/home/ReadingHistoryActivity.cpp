#include "ReadingHistoryActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "ReadingStatsStore.h"
#include "components/ReadingStatsFormat.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/themes/TenorRadius.h"
#include "fontIds.h"
const char* ReadingHistoryActivity::headerTitle() const { return tr(STR_STATS_MONTH); }
void ReadingHistoryActivity::onEnter() {
  RenderLock lock(*this);
  const uint32_t day = ReadingStatsStore::habitStamp().day;
  char text[96];
  if (!day || !READING_STATS.statisticsReadable) {
    count = 1;
    labels[0] = !day ? tr(STR_STATS_UNDATED) : tr(STR_STATS_READ_ERROR);
  } else {
    grid = lich30::tinh(READING_STATS.kho.cacNgay(), day, cells);
    lich30::tuan(READING_STATS.kho.cacNgay(), day, weekMinutes);
    for (int k = 0; k < lich30::SO_TUAN; ++k)
      weekKey[k] = habits::dateKey(day - (7 * lich30::SO_TUAN - 1) + 7 * k);
    firstKey = habits::dateKey(day - (lich30::SO_O - 1));
    lastKey = habits::dateKey(day);
    for (int i = 0; i < count; ++i) {
      const uint32_t key = habits::dateKey(day > static_cast<uint32_t>(i) ? day - i : 0);
      snprintf(text, sizeof(text), "%02lu/%02lu/%04lu", static_cast<unsigned long>(key % 100),
               static_cast<unsigned long>(key / 100 % 100), static_cast<unsigned long>(key / 10000));
      labels[i] = text;
      values[i] = tr(STR_STATS_NOT_RECORDED);
      for (const auto& d : READING_STATS.kho.cacNgay())
        if (d.ma == key) {
          readingstatsview::duration(static_cast<uint64_t>(d.phut) * 60000 + d.leMs, text, sizeof(text));
          values[i] = text;
          snprintf(text, sizeof(text), "%s: %lu", tr(STR_STATS_TURNS), static_cast<unsigned long>(d.trang));
          subtitles[i] = text;
          break;
        }
    }
  }
  for (int i = 0; i < count; ++i) {
    rows[i].label = labels[i].c_str();
    rows[i].value = values[i].c_str();
    rows[i].subtitle = subtitles[i].empty() ? nullptr : subtitles[i].c_str();
    rows[i].actionValue = i;
  }
  UiListActivity::onEnter();
}
void ReadingHistoryActivity::drawChrome() {
  if (tenorchrome::enabled())
    tenorchrome::drawHeader(renderer, headerTitle(), tr(STR_HOME_TAB_STATS));
  else
    UiListActivity::drawChrome();
  if (grid) {
    drawGrid();
    drawChart();
  }
}
// 10 columns, 3 rows, oldest day top left and today bottom right. The cell size follows the screen width.
int ReadingHistoryActivity::gridTop() const {
  const auto& m = UITheme::getInstance().getMetrics();
  return (tenorchrome::enabled() ? tenorchrome::tabTop() : m.topPadding + m.headerHeight) + 12;
}
int ReadingHistoryActivity::gridHeight() const {
  return 3 * ((renderer.getScreenWidth() - 48) / 10) + renderer.getLineHeight(SMALL_FONT_ID) + 12;
}
// Weekly hours under the grid: lich30::SO_TUAN columns of 7 days, the last one ending today. Compact (a bar zone of
// CHART_BAR_MAX px plus one text line): a hairline base, a thin black bar, its hours and unit ("23 h") to the right,
// the first day below. Bar height is relative to the busiest week; the hours label gives the real level, where the
// grid above uses fixed minutes. The unit is the stats card's own string, so there is no new translation.
constexpr int CHART_BAR_MAX = 36;
int ReadingHistoryActivity::chartHeight() const { return renderer.getLineHeight(SMALL_FONT_ID) + CHART_BAR_MAX + 10; }
void ReadingHistoryActivity::drawChart() const {
  const int line = renderer.getLineHeight(SMALL_FONT_ID), left = 24, width = renderer.getScreenWidth() - 2 * left,
            pitch = width / lich30::SO_TUAN, bar = pitch / 8, base = gridTop() + gridHeight() + 4 + CHART_BAR_MAX;
  renderer.drawLine(left, base, left + width, base, true);
  uint32_t most = 1;
  for (const uint32_t m : weekMinutes) most = std::max(most, m);
  char text[40];
  for (int k = 0; k < lich30::SO_TUAN; ++k) {
    const uint32_t m = weekMinutes[k];
    const int x = left + k * pitch + 12, height = m ? std::max<int>(2, m * CHART_BAR_MAX / most) : 0;
    if (height) renderer.fillRect(x, base - height, bar, height);
    if (m >= 60)
      snprintf(text, sizeof(text), "%lu %s", static_cast<unsigned long>((m + 30) / 60), tr(STR_STATS_HOURS));
    else
      snprintf(text, sizeof(text), "%s %s", m ? "<1" : "0", tr(STR_STATS_HOURS));
    renderer.drawText(SMALL_FONT_ID, x + bar + 6, base - line - 1, text);
    snprintf(text, sizeof(text), "%02lu/%02lu", static_cast<unsigned long>(weekKey[k] % 100),
             static_cast<unsigned long>(weekKey[k] / 100 % 100));
    renderer.drawText(SMALL_FONT_ID, x, base + 3, text);
  }
}
void ReadingHistoryActivity::drawGrid() const {
  const int pitch = (renderer.getScreenWidth() - 48) / 10, side = pitch - 8, radius = tenorradius::tile(side),
            top = gridTop();
  constexpr Color shade[] = {Color::White, Color::LightGray, Color::DarkGray, Color::Black};
  int x = 0, y = 0;
  for (int i = 0; i < lich30::SO_O; ++i) {
    x = 24 + i % 10 * pitch + 4;
    y = top + i / 10 * pitch;
    // One rounded block of the day's shade. Only an empty day gets a rim: grey, 2 px deep (1 px of dither
    // reads as dots), round a white cell.
    if (cells[i]) {
      renderer.fillRoundedRect(x, y, side, side, radius, shade[cells[i]]);
    } else {
      renderer.fillRoundedRect(x, y, side, side, radius, Color::DarkGray);
      renderer.fillRoundedRect(x + 2, y + 2, side - 4, side - 4, tenorradius::nest(radius, 2), Color::White);
    }
  }
  // Today (the last cell drawn) wears a black frame set off by a 2 px gap, concentric with its corner.
  renderer.drawRoundedRect(x - 3, y - 3, side + 6, side + 6, 1, tenorradius::container(radius, 3), true);
  char text[8];
  snprintf(text, sizeof(text), "%02lu/%02lu", static_cast<unsigned long>(firstKey % 100),
           static_cast<unsigned long>(firstKey / 100 % 100));
  const int labelY = top + 3 * pitch + 2;
  renderer.drawText(SMALL_FONT_ID, 24, labelY, text);
  snprintf(text, sizeof(text), "%02lu/%02lu", static_cast<unsigned long>(lastKey % 100),
           static_cast<unsigned long>(lastKey / 100 % 100));
  renderer.drawText(SMALL_FONT_ID, renderer.getScreenWidth() - 24 - renderer.getTextWidth(SMALL_FONT_ID, text), labelY,
                    text);
}
void ReadingHistoryActivity::buildScreen(UiScreen& screen) {
  const auto& m = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(
      {static_cast<int16_t>(grid ? gridTop() + gridHeight() + chartHeight() : m.topPadding + m.headerHeight), 0,
       static_cast<int16_t>(m.buttonHintsHeight), 0});
  freeink::ui::ListProps props;
  props.items = rows.data();
  props.count = count;
  props.action = ACTION_ROW;
  props.labelText = uiMenuLabelText(screen.theme());
  syncListViewport(screen, props, true);
  screen.list(props);
}
