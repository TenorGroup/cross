#include "ReadingHabitsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "ReadingStatsStore.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "fontIds.h"
namespace {
constexpr StrId names[] = {StrId::STR_HABIT_NIGHT,   StrId::STR_HABIT_SHORT,   StrId::STR_HABIT_EARLY,
                           StrId::STR_HABIT_REGULAR, StrId::STR_HABIT_WEEKEND, StrId::STR_HABIT_LONG};
constexpr StrId descriptions[] = {StrId::STR_HABIT_NIGHT_DESC,   StrId::STR_HABIT_SHORT_DESC,
                                  StrId::STR_HABIT_EARLY_DESC,   StrId::STR_HABIT_REGULAR_DESC,
                                  StrId::STR_HABIT_WEEKEND_DESC, StrId::STR_HABIT_LONG_DESC};
}  // namespace
ReadingHabitsActivity::ReadingHabitsActivity(GfxRenderer& r, MappedInputManager& i)
    : UiListActivity("ReadingHabits", r, i) {}
const char* ReadingHabitsActivity::headerTitle() const { return tr(STR_READING_HABITS); }
void ReadingHabitsActivity::onEnter() {
  RenderLock lock(*this);
  READING_STATS.prepareHabits();
  readable = READING_STATS.statisticsReadable;
  const auto& ledger = READING_STATS.habitLedger;
  summary = ledger.summarize(ReadingStatsStore::habitStamp().day);
  awarded = ReadingStatsStore::habitStamp().day && !ledger.clockLost ? ledger.awarded : 0;
  hidden = ledger.hidden;
  refreshRows();
  UiListActivity::onEnter();
}
void ReadingHabitsActivity::drawChrome() {
  if (tenorchrome::enabled())
    tenorchrome::drawHeader(renderer, headerTitle(), tr(STR_HOME_TAB_STATS));
  else
    UiListActivity::drawChrome();
}
void ReadingHabitsActivity::refreshRows() {
  for (size_t i = 0; i < rows.size(); ++i) {
    rows[i].label = I18N.get(names[i]);
    rows[i].actionValue = i;
    rows[i].value = (hidden & (1 << i)) ? tr(STR_HABIT_HIDDEN) : ((awarded & (1 << i)) ? tr(STR_HABIT_MATCHED) : "");
  }
}
void ReadingHabitsActivity::activateIndex(int index) {
  if (!readable || index < 0 || index >= static_cast<int>(habits::NAMES)) return;
  RenderLock lock(*this);
  const auto previous = READING_STATS.habitLedger.hidden;
  READING_STATS.habitLedger.hidden = previous ^ (1 << index);
  failed = !READING_STATS.saveToFile();
  if (failed) READING_STATS.habitLedger.hidden = previous;
  hidden = READING_STATS.habitLedger.hidden;
  refreshRows();
  requestUpdate();
}
void ReadingHabitsActivity::buildScreen(UiScreen& screen) {
  const auto& m = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(
      {static_cast<int16_t>(m.topPadding + m.headerHeight), 0, static_cast<int16_t>(m.buttonHintsHeight), 0});
  reserveFixedMenuContent(screen);
  const int explanationTop = tenorchrome::tipY(renderer) - 168;
  const int bottom = screen.body().y + screen.body().height;
  if (bottom > explanationTop - 12) screen.takeBottom(static_cast<int16_t>(bottom - explanationTop + 12));
  freeink::ui::ListProps props;
  props.items = rows.data();
  props.count = rows.size();
  props.action = ACTION_ROW;
  props.labelText = uiMenuLabelText(screen.theme());
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
void ReadingHabitsActivity::drawFooter() {
  const int selected = kepConTro(nav.selected, habits::NAMES);
  const int y = tenorchrome::tipY(renderer) - 168;
  const int width = renderer.getScreenWidth() - 48;
  renderer.drawLine(24, y - 10, renderer.getScreenWidth() - 24, y - 10, true);
  int lineY = y;
  // Menu-only, at most two wrapped lines; allocations never run in page turning.
  for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, I18N.get(descriptions[selected]), width, 2)) {
    renderer.drawText(UI_10_FONT_ID, 24, lineY, line.c_str());
    lineY += renderer.getLineHeight(UI_10_FONT_ID);
  }
  char evidence[160];
  const auto total = summary.activeMs;
  switch (selected) {
    case habits::NIGHT:
      snprintf(evidence, sizeof(evidence), tr(STR_HABIT_HOUR_EVIDENCE),
               total ? static_cast<unsigned>(100ull * summary.nightMs / total) : 0, summary.nights);
      break;
    case habits::EARLY:
      snprintf(evidence, sizeof(evidence), tr(STR_HABIT_HOUR_EVIDENCE),
               total ? static_cast<unsigned>(100ull * summary.earlyMs / total) : 0, summary.mornings);
      break;
    case habits::SHORT:
      snprintf(evidence, sizeof(evidence), tr(STR_HABIT_SHORT_EVIDENCE), summary.shortSessions, summary.sessions);
      break;
    case habits::REGULAR:
      snprintf(evidence, sizeof(evidence), tr(STR_HABIT_REGULAR_EVIDENCE), summary.recentDays,
               std::min<int>(14, summary.coverage));
      break;
    case habits::WEEKEND:
      snprintf(evidence, sizeof(evidence), tr(STR_HABIT_WEEKEND_EVIDENCE), summary.weekendMs / 60000,
               summary.weekdayMs / 60000);
      break;
    default:
      snprintf(evidence, sizeof(evidence), tr(STR_HABIT_LONG_EVIDENCE), summary.longSessions, summary.sessions);
      break;
  }
  lineY += 8;
  const char* status = !readable ? tr(STR_STATS_READ_ERROR)
                                 : ((awarded & (1 << selected)) ? tr(STR_HABIT_MATCHED) : tr(STR_HABIT_COLLECTING));
  renderer.drawText(UI_10_FONT_ID, 24, lineY, status, true, EpdFontFamily::BOLD);
  lineY += renderer.getLineHeight(UI_10_FONT_ID) + 4;
  if (readable && summary.coverage) {
    for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, evidence, width, 2)) {
      renderer.drawText(UI_10_FONT_ID, 24, lineY, line.c_str());
      lineY += renderer.getLineHeight(UI_10_FONT_ID);
    }
  }
  renderer.drawText(SMALL_FONT_ID, 24, tenorchrome::tipY(renderer) - 30, tr(STR_HABIT_PERIOD));
  tenorchrome::drawTip(renderer, failed ? tr(STR_HABIT_SAVE_FAILED) : tr(STR_HABIT_TIP));
  UiListActivity::drawFooter();
}
