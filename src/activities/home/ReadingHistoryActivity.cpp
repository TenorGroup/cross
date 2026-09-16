#include "ReadingHistoryActivity.h"

#include <I18n.h>

#include <cstdio>

#include "ReadingStatsStore.h"
#include "components/ReadingStatsFormat.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
const char* ReadingHistoryActivity::headerTitle() const { return tr(STR_STATS_MONTH); }
void ReadingHistoryActivity::onEnter() {
  RenderLock lock(*this);
  const uint32_t day = ReadingStatsStore::habitStamp().day;
  char text[96];
  if (!day || !READING_STATS.statisticsReadable) {
    count = 1;
    labels[0] = !day ? tr(STR_STATS_UNDATED) : tr(STR_STATS_READ_ERROR);
  } else
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
}
void ReadingHistoryActivity::buildScreen(UiScreen& screen) {
  const auto& m = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(
      {static_cast<int16_t>(m.topPadding + m.headerHeight), 0, static_cast<int16_t>(m.buttonHintsHeight), 0});
  freeink::ui::ListProps props;
  props.items = rows.data();
  props.count = count;
  props.action = ACTION_ROW;
  props.labelText = uiMenuLabelText(screen.theme());
  syncListViewport(screen, props, true);
  screen.list(props);
}
