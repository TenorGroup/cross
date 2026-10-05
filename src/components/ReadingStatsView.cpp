#include "ReadingStatsView.h"
#include "ReadingStatsLayout.h"
#include "CrossPointSettings.h"
#include "components/UIScale.h"
#include "components/themes/TenorRadius.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "ReadingStatsFormat.h"
#include "ReadingStatsStore.h"
#include "fontIds.h"
#include "util/NgayGio.h"
namespace readingstatsview {
namespace {
ReadingStatsLayout layoutFor(const GfxRenderer& r, bool sleep) {
  return {r.getLineHeight(SMALL_FONT_ID), r.getLineHeight(UI_10_FONT_ID), r.getLineHeight(UI_12_FONT_ID),
          r.getLineHeight(sleep ? NOTOSANS_18_FONT_ID : UI_12_FONT_ID)};
}
}
int panelHeight(const GfxRenderer& r, int page, bool sleep) {
  const bool enlarged = normalizedUiTextSize(SETTINGS.uiTextSize) != 0;
  if (sleep) {
    if (!enlarged) return HEIGHT;
    const auto layout = layoutFor(r, true);
    return page == 0 ? layout.meanLabel : page == 1 ? layout.height - layout.meanLabel : layout.height;
  }
  const HomeReadingStatsLayout home(r.getLineHeight(SMALL_FONT_ID), r.getLineHeight(UI_10_FONT_ID),
                                    r.getLineHeight(UI_12_FONT_ID), false, enlarged);
  return page == 0 ? home.overviewHeight : page == 1 ? home.habitsHeight : home.height;
}
void draw(const GfxRenderer& r, const int top, const bool sleep, const bool compact, const int page) {
  // The badge borrows whitespace within this panel, preserving the list viewport.
  const bool enlarged = normalizedUiTextSize(SETTINGS.uiTextSize) != 0;
  const auto layout = layoutFor(r, sleep);
  const auto y = [top, compact, enlarged, page, &layout](const int offset) {
    if (enlarged) return top + layout.offset(offset) - (page == 1 ? layout.meanLabel : 0);
    return top + (compact ? offset * (HEIGHT - BADGE_HEIGHT) / HEIGHT : offset);
  };
  const HomeReadingStatsLayout home(r.getLineHeight(SMALL_FONT_ID), r.getLineHeight(UI_10_FONT_ID),
                                    r.getLineHeight(UI_12_FONT_ID), compact, enlarged);
  const int left = sleep ? 24 : 36;
  const int width = r.getScreenWidth() - 2 * left;
  const int habitsTop = top + (page == 1 ? 0 : home.habitsTop);
  const int valueY = sleep ? y(28) : top + home.value;
  const int periodLabelY = sleep ? y(80) : top + home.periodLabel;
  const int periodValueY = sleep ? y(105) : top + home.periodValue;
  const int chartBottomY = sleep ? y(225) : top + home.chartBottom;
  const int datesY = sleep ? y(233) : top + home.dates;
  const int meanLabelY = sleep ? y(270) : habitsTop + home.meanLabel;
  const int meanValueY = sleep ? y(296) : habitsTop + home.meanValue;
  const int habitsLabelY = sleep ? y(336) : habitsTop + home.habitsLabel;
  const int habitsValueY = sleep ? y(362) : habitsTop + home.habitsValue;
  const int noteY = sleep ? y(402) : habitsTop + home.note;
  if (!sleep) {
    const int radius = tenorradius::container(tenorradius::leaf(30), 12);
    if (page != 1) r.drawRoundedRect(24, top, r.getScreenWidth() - 48, home.overviewHeight, 1, radius, true);
    if (page != 0) r.drawRoundedRect(24, habitsTop, r.getScreenWidth() - 48, home.habitsHeight, 1, radius, true);
  }
  const auto cellText = [&](int font, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
    return enlarged || !sleep ? r.truncatedText(font, text, width / 2 - 12, style) : std::string(text);
  };
  if (!READING_STATS.statisticsReadable) {
    r.drawText(UI_12_FONT_ID, left, sleep ? (enlarged ? top + 24 : y(24)) : top + 24,
               (sleep ? std::string(tr(STR_STATS_READ_ERROR)) :
                r.truncatedText(UI_12_FONT_ID, tr(STR_STATS_READ_ERROR), width)).c_str());
    return;
  }
  const uint32_t today = ReadingStatsStore::currentDay();
  ngaygio::Moc date{static_cast<uint16_t>(today / 10000), static_cast<uint8_t>(today / 100 % 100),
                    static_cast<uint8_t>(today % 100), 0, 0};
  uint64_t totals[3] = {};
  uint32_t daily[7] = {};
  uint8_t dates[7] = {};
  bool known[7] = {};
  uint32_t readDays7 = 0;
  uint32_t streak = 0;
  bool streakOpen = true;
  for (int i = 0; i < 30; ++i) {
    const auto key = today ? ngaygio::maNgay(date) : 0;
    if (i < 7) dates[i] = date.ngay;
    if (today) date = ngaygio::doiSangDiaPhuong(date, -1440);
    bool read = false;
    for (const auto& d : READING_STATS.kho.cacNgay())
      if (key && d.ma == key) {
        const uint64_t ms = static_cast<uint64_t>(d.phut) * 60000 + d.leMs;
        read = ms || d.trang;
        if (i == 0) totals[0] += ms;
        if (i < 7) {
          totals[1] += ms;
          daily[i] = ms / 1000;
          known[i] = true;
          if (ms || d.trang) ++readDays7;
        }
        totals[2] += ms;
      }
    if (streakOpen && read)
      ++streak;
    else if (i > 0)
      streakOpen = false;
  }
  char text[96];
  if (page != 1) {
  r.drawText(UI_10_FONT_ID, left, sleep ? top : top + 12, cellText(UI_10_FONT_ID, tr(STR_STATS_TODAY)).c_str());
  duration(totals[0], text, sizeof(text));
  r.drawText(sleep ? NOTOSANS_18_FONT_ID : UI_12_FONT_ID, left, valueY, cellText(sleep ? NOTOSANS_18_FONT_ID : UI_12_FONT_ID, today ? text : tr(STR_STATS_UNDATED), EpdFontFamily::BOLD).c_str(), true,
             EpdFontFamily::BOLD);
  snprintf(text, sizeof(text), "%lu%s %s", static_cast<unsigned long>(streak), streak == 30 ? "+" : "",
           tr(STR_STATS_DAYS));
  const int right = r.getScreenWidth() - left;
  const auto streakLabel = cellText(UI_10_FONT_ID, tr(STR_STATS_STREAK));
  r.drawText(UI_10_FONT_ID, right - r.getTextWidth(UI_10_FONT_ID, streakLabel.c_str()), sleep ? top : top + 12, streakLabel.c_str());
  r.drawText(UI_12_FONT_ID, right - r.getTextWidth(UI_12_FONT_ID, text, EpdFontFamily::BOLD), valueY,
             today ? text : "...", true, EpdFontFamily::BOLD);
  const int half = width / 2;
  for (int i = 0; i < 2; ++i) {
    const int x = left + i * half;
    r.drawText(UI_10_FONT_ID, x, periodLabelY, cellText(UI_10_FONT_ID, i ? tr(STR_STATS_MONTH) : tr(STR_STATS_WEEK)).c_str());
    duration(totals[i + 1], text, sizeof(text));
    r.drawText(UI_10_FONT_ID, x, periodValueY,
               r.truncatedText(UI_10_FONT_ID, today ? text : "...", half - 12, EpdFontFamily::BOLD).c_str(), true,
               EpdFontFamily::BOLD);
  }
  const auto maximum = std::max<uint32_t>(1, *std::max_element(daily, daily + 7));
  if (sleep) {
    const int pitch = width / 7;
    for (int i = 0; i < 7; ++i) {
      const int d = 6 - i, x = 24 + i * pitch, bar = std::max(4, pitch - 24);
      const int height = static_cast<uint64_t>(daily[d]) * 66 / maximum;
      if (height)
        r.fillRect(x + 12, y(225) - height, bar, height);
      else
        r.drawLine(x + 12, y(225), x + 12 + bar, y(225), true);
      if (today)
        snprintf(text, sizeof(text), "%02u", dates[d]);
      else
        snprintf(text, sizeof(text), "?");
      r.drawText(SMALL_FONT_ID, x + (pitch - r.getTextWidth(SMALL_FONT_ID, text)) / 2, y(233), text);
      if (!known[d]) r.drawText(SMALL_FONT_ID, x + pitch / 2, y(203), ".");
    }
  } else {
    const int pitch = width / 7;
    const int chartHeight = home.chartHeight;
    for (int i = 0; i < 7; ++i) {
      const int d = 6 - i;
      const int center = left + width * (2 * i + 1) / 14;
      const int bar = std::min(30, pitch - 16);
      const int height = static_cast<uint64_t>(daily[d]) * chartHeight / maximum;
      if (height) {
        const int radius = std::min(tenorradius::leaf(std::min(bar, height)), std::min(bar, height) / 2);
        r.fillRoundedRect(center - bar / 2, chartBottomY - height, bar, height, radius, Color::Black);
        if (d == 0) r.drawRoundedRect(center - bar / 2 - 2, chartBottomY - height - 2,
                                      bar + 4, height + 4, 2, radius, true);
      } else if (known[d])
        r.drawLine(center - bar / 2, chartBottomY, center + bar / 2, chartBottomY, true);
      if (today) snprintf(text, sizeof(text), "%02u", dates[d]);
      else snprintf(text, sizeof(text), "?");
      const auto style = d == 0 ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
      r.drawText(SMALL_FONT_ID, center - r.getTextWidth(SMALL_FONT_ID, text, style) / 2, datesY, text, true, style);
      if (!known[d]) r.drawText(SMALL_FONT_ID, center - r.getTextWidth(SMALL_FONT_ID, ".") / 2,
                               chartBottomY - r.getLineHeight(SMALL_FONT_ID), ".");
    }
  }
  }
  if (page == 0) return;
  const auto habit = READING_STATS.habitLedger.summarize(ReadingStatsStore::habitStamp().day);
  const int xs[] = {left, left + width / 2};
  r.drawText(SMALL_FONT_ID, xs[0], meanLabelY, cellText(SMALL_FONT_ID, tr(STR_STATS_MEAN_WEEK)).c_str());
  r.drawText(SMALL_FONT_ID, xs[1], meanLabelY, cellText(SMALL_FONT_ID, tr(STR_STATS_SESSIONS_28)).c_str());
  if (readDays7)
    duration(totals[1] / readDays7, text, sizeof(text));
  else
    snprintf(text, sizeof(text), "...");
  r.drawText(UI_12_FONT_ID, xs[0], meanValueY,
             r.truncatedText(UI_12_FONT_ID, text, width / 2 - 12, EpdFontFamily::BOLD).c_str(), true,
             EpdFontFamily::BOLD);
  snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(habit.sessions));
  r.drawText(UI_12_FONT_ID, xs[1], meanValueY, habit.coverage ? text : "...", true, EpdFontFamily::BOLD);
  r.drawText(SMALL_FONT_ID, xs[0], habitsLabelY, cellText(SMALL_FONT_ID, tr(STR_STATS_NIGHT_28)).c_str());
  r.drawText(SMALL_FONT_ID, xs[1], habitsLabelY, cellText(SMALL_FONT_ID, tr(STR_STATS_SHORT_LONG)).c_str());
  snprintf(text, sizeof(text), "%u%%",
           habit.activeMs ? static_cast<unsigned>(100ull * habit.nightMs / habit.activeMs) : 0);
  r.drawText(UI_12_FONT_ID, xs[0], habitsValueY, habit.activeMs ? text : "...", true, EpdFontFamily::BOLD);
  snprintf(text, sizeof(text), "%lu / %lu", static_cast<unsigned long>(habit.shortSessions),
           static_cast<unsigned long>(habit.longSessions));
  r.drawText(UI_12_FONT_ID, xs[1], habitsValueY, habit.sessions ? text : "...", true, EpdFontFamily::BOLD);
  const auto& k = READING_STATS.kho;
  if (k.phutChuaBietNgay() || k.msChuaBietNgay() || k.trangChuaBietNgay()) {
    char value[48];
    duration(static_cast<uint64_t>(k.phutChuaBietNgay()) * 60000 + k.msChuaBietNgay(), value, sizeof(value));
    snprintf(text, sizeof(text), "%s: %s / %lu", tr(STR_STATS_UNDATED), value,
             static_cast<unsigned long>(k.trangChuaBietNgay()));
  } else
    snprintf(text, sizeof(text), "%s", tr(STR_STATS_CHART_NOTE));
  r.drawText(SMALL_FONT_ID, left, noteY, r.truncatedText(SMALL_FONT_ID, text, width).c_str());
}
void drawSleep(const GfxRenderer& r) {
  const int width = r.getScreenWidth(), height = r.getScreenHeight();
  const bool enlarged = normalizedUiTextSize(SETTINGS.uiTextSize) != 0;
  const int titleY = enlarged ? 18 : 30;
  const int dateY = enlarged ? titleY + r.getLineHeight(NOTOSANS_18_FONT_ID) + 2 : 78;
  const int ruleY = enlarged ? dateY + r.getLineHeight(UI_10_FONT_ID) + 4 : 108;
  const int panelTop = enlarged ? ruleY + 11 : 132;
  const auto sleepTitle = enlarged ? r.truncatedText(NOTOSANS_18_FONT_ID, tr(STR_SLEEP_STATS), width - 48,
                                                       EpdFontFamily::BOLD) : std::string(tr(STR_SLEEP_STATS));
  r.drawText(NOTOSANS_18_FONT_ID, 24, titleY, sleepTitle.c_str(), true, EpdFontFamily::BOLD);
  const uint32_t today = ReadingStatsStore::currentDay();
  char text[80];
  if (today)
    snprintf(text, sizeof(text), "%02lu/%02lu/%04lu", static_cast<unsigned long>(today % 100),
             static_cast<unsigned long>(today / 100 % 100), static_cast<unsigned long>(today / 10000));
  else
    snprintf(text, sizeof(text), "%s", tr(STR_STATS_UNDATED));
  r.drawText(UI_10_FONT_ID, 24, dateY, text);
  r.fillRect(24, ruleY, width - 48, 3);
  draw(r, panelTop, true);
  if (READING_STATS.statisticsReadable && !READING_STATS.activeBookPath.empty()) {
    const int bookRule = enlarged ? panelTop + panelHeight(r, -1, true) + 8 : 584;
    const int bookTitle = enlarged ? bookRule + 6 : 602;
    const int bookLabel = enlarged ? bookTitle + r.getLineHeight(UI_10_FONT_ID) + 4 : 644;
    const int bookValue = enlarged ? bookLabel + r.getLineHeight(SMALL_FONT_ID) + 4 : 669;
    r.fillRect(24, bookRule, width - 48, 2);
    const auto& title =
        READING_STATS.activeBookTitle.empty() ? READING_STATS.activeBookPath : READING_STATS.activeBookTitle;
    r.drawText(UI_10_FONT_ID, 24, bookTitle,
               r.truncatedText(UI_10_FONT_ID, title.c_str(), width - 48, EpdFontFamily::BOLD).c_str(), true,
               EpdFontFamily::BOLD);
    const auto bookTimeLabel = enlarged ? r.truncatedText(SMALL_FONT_ID, tr(STR_STATS_BOOK_TIME), width / 2 - 36)
                                        : std::string(tr(STR_STATS_BOOK_TIME));
    const auto positionLabel = enlarged ? r.truncatedText(SMALL_FONT_ID, tr(STR_STATS_POSITION), width / 2 - 24)
                                        : std::string(tr(STR_STATS_POSITION));
    r.drawText(SMALL_FONT_ID, 24, bookLabel, bookTimeLabel.c_str());
    duration(static_cast<uint64_t>(READING_STATS.activeBook.minutes) * 60000 + READING_STATS.activeBook.remainderMs,
             text, sizeof(text));
    const auto bookTime = enlarged ? r.truncatedText(UI_12_FONT_ID, text, width / 2 - 36, EpdFontFamily::BOLD)
                                   : std::string(text);
    r.drawText(UI_12_FONT_ID, 24, bookValue, bookTime.c_str(), true, EpdFontFamily::BOLD);
    r.drawText(SMALL_FONT_ID, width / 2, bookLabel, positionLabel.c_str());
    snprintf(text, sizeof(text), "%u%%", READING_STATS.activeBook.progress);
    r.drawText(UI_12_FONT_ID, width / 2, bookValue, text, true, EpdFontFamily::BOLD);
  }
  r.drawText(UI_10_FONT_ID, 24, height - 40, "tenor/cross", true, EpdFontFamily::BOLD);
}
}  // namespace readingstatsview
