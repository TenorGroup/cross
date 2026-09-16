#include "BookStatsActivity.h"

#include <I18n.h>

#include <cstdio>

#include "ReadingStatsStore.h"
#include "components/ReadingStatsFormat.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"

BookStatsActivity::BookStatsActivity(GfxRenderer& r, MappedInputManager& input, std::string path, std::string title)
    : UiListActivity("BookStats", r, input), path(std::move(path)), title(std::move(title)) {}
void BookStatsActivity::onEnter() {
  RenderLock lock(*this);
  BookReadingRecord b;
  const bool readable = READING_STATS.readBook(path, b);
  const StrId labels[] = {StrId::STR_STATS_BOOK_TIME, StrId::STR_STATS_MEAN_BOOK,   StrId::STR_STATS_READING_DAYS,
                          StrId::STR_STATS_FIRST_DAY, StrId::STR_STATS_LAST_DAY,    StrId::STR_STATS_POSITION,
                          StrId::STR_STATS_TURNS,     StrId::STR_STATS_MEASURE_NOTE};
  char text[96];
  const uint64_t elapsed = static_cast<uint64_t>(b.minutes) * 60000 + b.remainderMs;
  readingstatsview::duration(elapsed, text, sizeof(text));
  values[0] = text;
  if (b.days) {
    readingstatsview::duration(elapsed / b.days, text, sizeof(text));
    values[1] = text;
  } else
    values[1] = tr(STR_STATS_NO_SAMPLE);
  values[2] = std::to_string(b.days);
  for (int i = 0; i < 2; ++i) {
    const uint32_t day = i ? b.lastDay : b.firstDay;
    if (day) {
      snprintf(text, sizeof(text), "%02lu/%02lu/%04lu", static_cast<unsigned long>(day % 100),
               static_cast<unsigned long>(day / 100 % 100), static_cast<unsigned long>(day / 10000));
      values[3 + i] = text;
    } else
      values[3 + i] = tr(STR_STATS_UNDATED);
  }
  values[5] = std::to_string(b.progress) + "%";
  values[6] = std::to_string(b.turns);
  values[7] = "";
  if (!readable)
    for (auto& v : values) v = tr(STR_STATS_NOT_RECORDED);
  for (size_t i = 0; i < rows.size(); ++i) {
    rows[i].label = I18N.get(labels[i]);
    rows[i].value = values[i].c_str();
    rows[i].actionValue = i;
  }
  UiListActivity::onEnter();
}
void BookStatsActivity::buildScreen(UiScreen& screen) {
  const auto& m = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(
      {static_cast<int16_t>(m.topPadding + m.headerHeight), 0, static_cast<int16_t>(m.buttonHintsHeight), 0});
  freeink::ui::ListProps props;
  props.items = rows.data();
  props.count = rows.size();
  props.action = ACTION_ROW;
  props.labelText = uiMenuLabelText(screen.theme());
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void BookStatsActivity::drawChrome() {
  if (tenorchrome::enabled())
    tenorchrome::drawHeader(renderer, headerTitle(), tr(STR_HOME_TAB_STATS));
  else
    UiListActivity::drawChrome();
}
