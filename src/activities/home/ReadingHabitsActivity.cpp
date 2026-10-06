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
#include "shells/Shell.h"
#include "shells/ugly/UglyInk.h"
#include "shells/ugly/UglyWords.h"
namespace {
constexpr StrId names[] = {StrId::STR_HABIT_NIGHT,   StrId::STR_HABIT_SHORT,   StrId::STR_HABIT_EARLY,
                           StrId::STR_HABIT_REGULAR, StrId::STR_HABIT_WEEKEND, StrId::STR_HABIT_LONG};
constexpr StrId descriptions[] = {StrId::STR_HABIT_NIGHT_DESC,   StrId::STR_HABIT_SHORT_DESC,
                                  StrId::STR_HABIT_EARLY_DESC,   StrId::STR_HABIT_REGULAR_DESC,
                                  StrId::STR_HABIT_WEEKEND_DESC, StrId::STR_HABIT_LONG_DESC};
// Where the explanation under the list starts. The hand of tenor/ugly is taller than the small font and its tip over
// the key bar takes 2 lines, so its explanation starts higher.
int explanationTop(const GfxRenderer& r) { return tenorchrome::tipY(r) - (shell::uglyParts() ? 236 : 168); }
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
  const int top = explanationTop(renderer);
  const int bottom = screen.body().y + screen.body().height;
  if (bottom > top - 12) screen.takeBottom(static_cast<int16_t>(bottom - top + 12));
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
  const int y = explanationTop(renderer);
  const int width = renderer.getScreenWidth() - 48;
  const bool handwritten = shell::uglyParts();
  // tenor/ugly: the same explanation in hand, one size, 28 px a line; a habit not yet earned gets a jab.
  constexpr int STEP = 28;
  int lineY = y;
  if (handwritten) {
    ugly::line(renderer, 24, y - 10, renderer.getScreenWidth() - 24, y - 12, 41, 2);
    lineY += ugly::ascent(ugly::Size::S22);
    lineY += STEP * ugly::paragraph(renderer, ugly::Size::S22, 24, lineY, width, STEP, I18N.get(descriptions[selected]));
  } else {
    renderer.drawLine(24, y - 10, renderer.getScreenWidth() - 24, y - 10, true);
    // Menu-only, at most two wrapped lines; allocations never run in page turning.
    for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, I18N.get(descriptions[selected]), width, 2)) {
      renderer.drawText(UI_10_FONT_ID, 24, lineY, line.c_str());
      lineY += renderer.getLineHeight(UI_10_FONT_ID);
    }
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
  const bool matched = (awarded & (1 << selected)) != 0;
  const char* status = !readable ? tr(STR_STATS_READ_ERROR) : matched ? tr(STR_HABIT_MATCHED) : tr(STR_HABIT_COLLECTING);
  const char* tip = failed ? tr(STR_HABIT_SAVE_FAILED) : tr(STR_HABIT_TIP);
  if (handwritten) {
    lineY += 4;
    ugly::text(renderer, ugly::Size::S22, 24, lineY, readable && !matched ? ugly::words::habitNoData() : status);
    lineY += STEP;
    if (readable && summary.coverage) ugly::paragraph(renderer, ugly::Size::S22, 24, lineY, width, STEP, evidence);
    // The period sits right over the tip, however many lines the tip takes.
    const int tipLines = ugly::paragraph(renderer, ugly::Size::S22, 24, 0, width, 26, tip, false);
    ugly::text(renderer, ugly::Size::S22, 24, tenorchrome::uglyTipTop(renderer, tipLines) - 10, tr(STR_HABIT_PERIOD));
  } else {
    lineY += 8;
    renderer.drawText(UI_10_FONT_ID, 24, lineY, status, true, EpdFontFamily::BOLD);
    lineY += renderer.getLineHeight(UI_10_FONT_ID) + 4;
    if (readable && summary.coverage) {
      for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, evidence, width, 2)) {
        renderer.drawText(UI_10_FONT_ID, 24, lineY, line.c_str());
        lineY += renderer.getLineHeight(UI_10_FONT_ID);
      }
    }
    renderer.drawText(SMALL_FONT_ID, 24, tenorchrome::tipY(renderer) - 30, tr(STR_HABIT_PERIOD));
  }
  tenorchrome::drawTip(renderer, tip);
  UiListActivity::drawFooter();
}
