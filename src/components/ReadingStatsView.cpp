#include "ReadingStatsView.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "ReadingStatsFormat.h"
#include "ReadingStatsStore.h"
#include "fontIds.h"
#include "util/NgayGio.h"
namespace readingstatsview {
void draw(const GfxRenderer& r, const int top, const bool sleep, const bool compact) {
  // The badge borrows whitespace within this panel, preserving the list viewport.
  const auto y = [top, compact](const int offset) {
    return top + (compact ? offset * (HEIGHT - BADGE_HEIGHT) / HEIGHT : offset);
  };
  const int width = r.getScreenWidth() - 48;
  if (!READING_STATS.statisticsReadable) {
    r.drawText(UI_12_FONT_ID, 24, y(24), tr(STR_STATS_READ_ERROR));
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
  r.drawText(UI_10_FONT_ID, 24, top, tr(STR_STATS_TODAY));
  duration(totals[0], text, sizeof(text));
  r.drawText(sleep ? NOTOSANS_18_FONT_ID : UI_12_FONT_ID, 24, y(28), today ? text : tr(STR_STATS_UNDATED), true,
             EpdFontFamily::BOLD);
  snprintf(text, sizeof(text), "%lu%s %s", static_cast<unsigned long>(streak), streak == 30 ? "+" : "",
           tr(STR_STATS_DAYS));
  const int right = r.getScreenWidth() - 24;
  r.drawText(UI_10_FONT_ID, right - r.getTextWidth(UI_10_FONT_ID, tr(STR_STATS_STREAK)), top, tr(STR_STATS_STREAK));
  r.drawText(UI_12_FONT_ID, right - r.getTextWidth(UI_12_FONT_ID, text, EpdFontFamily::BOLD), y(28),
             today ? text : "...", true, EpdFontFamily::BOLD);
  const int half = width / 2;
  for (int i = 0; i < 2; ++i) {
    const int x = 24 + i * half;
    r.drawText(UI_10_FONT_ID, x, y(80), i ? tr(STR_STATS_MONTH) : tr(STR_STATS_WEEK));
    duration(totals[i + 1], text, sizeof(text));
    r.drawText(UI_10_FONT_ID, x, y(105),
               r.truncatedText(UI_10_FONT_ID, today ? text : "...", half - 12, EpdFontFamily::BOLD).c_str(), true,
               EpdFontFamily::BOLD);
  }
  const auto maximum = std::max<uint32_t>(1, *std::max_element(daily, daily + 7));
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
  const auto habit = READING_STATS.habitLedger.summarize(ReadingStatsStore::habitStamp().day);
  const int xs[] = {24, 24 + width / 2};
  r.drawText(SMALL_FONT_ID, xs[0], y(270), tr(STR_STATS_MEAN_WEEK));
  r.drawText(SMALL_FONT_ID, xs[1], y(270), tr(STR_STATS_SESSIONS_28));
  if (readDays7)
    duration(totals[1] / readDays7, text, sizeof(text));
  else
    snprintf(text, sizeof(text), "...");
  r.drawText(UI_12_FONT_ID, xs[0], y(296),
             r.truncatedText(UI_12_FONT_ID, text, width / 2 - 12, EpdFontFamily::BOLD).c_str(), true,
             EpdFontFamily::BOLD);
  snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(habit.sessions));
  r.drawText(UI_12_FONT_ID, xs[1], y(296), habit.coverage ? text : "...", true, EpdFontFamily::BOLD);
  r.drawText(SMALL_FONT_ID, xs[0], y(336), tr(STR_STATS_NIGHT_28));
  r.drawText(SMALL_FONT_ID, xs[1], y(336), tr(STR_STATS_SHORT_LONG));
  snprintf(text, sizeof(text), "%u%%",
           habit.activeMs ? static_cast<unsigned>(100ull * habit.nightMs / habit.activeMs) : 0);
  r.drawText(UI_12_FONT_ID, xs[0], y(362), habit.activeMs ? text : "...", true, EpdFontFamily::BOLD);
  snprintf(text, sizeof(text), "%lu / %lu", static_cast<unsigned long>(habit.shortSessions),
           static_cast<unsigned long>(habit.longSessions));
  r.drawText(UI_12_FONT_ID, xs[1], y(362), habit.sessions ? text : "...", true, EpdFontFamily::BOLD);
  const auto& k = READING_STATS.kho;
  if (k.phutChuaBietNgay() || k.msChuaBietNgay() || k.trangChuaBietNgay()) {
    char value[48];
    duration(static_cast<uint64_t>(k.phutChuaBietNgay()) * 60000 + k.msChuaBietNgay(), value, sizeof(value));
    snprintf(text, sizeof(text), "%s: %s / %lu", tr(STR_STATS_UNDATED), value,
             static_cast<unsigned long>(k.trangChuaBietNgay()));
  } else
    snprintf(text, sizeof(text), "%s", tr(STR_STATS_CHART_NOTE));
  r.drawText(SMALL_FONT_ID, 24, y(402), r.truncatedText(SMALL_FONT_ID, text, width).c_str());
}
void drawSleep(const GfxRenderer& r) {
  const int width = r.getScreenWidth(), height = r.getScreenHeight();
  r.drawText(NOTOSANS_18_FONT_ID, 24, 30, tr(STR_SLEEP_STATS), true, EpdFontFamily::BOLD);
  const uint32_t today = ReadingStatsStore::currentDay();
  char text[80];
  if (today)
    snprintf(text, sizeof(text), "%02lu/%02lu/%04lu", static_cast<unsigned long>(today % 100),
             static_cast<unsigned long>(today / 100 % 100), static_cast<unsigned long>(today / 10000));
  else
    snprintf(text, sizeof(text), "%s", tr(STR_STATS_UNDATED));
  r.drawText(UI_10_FONT_ID, 24, 78, text);
  r.fillRect(24, 108, width - 48, 3);
  draw(r, 132, true);
  if (READING_STATS.statisticsReadable && !READING_STATS.activeBookPath.empty()) {
    r.fillRect(24, 584, width - 48, 2);
    const auto& title =
        READING_STATS.activeBookTitle.empty() ? READING_STATS.activeBookPath : READING_STATS.activeBookTitle;
    r.drawText(UI_10_FONT_ID, 24, 602,
               r.truncatedText(UI_10_FONT_ID, title.c_str(), width - 48, EpdFontFamily::BOLD).c_str(), true,
               EpdFontFamily::BOLD);
    r.drawText(SMALL_FONT_ID, 24, 644, tr(STR_STATS_BOOK_TIME));
    duration(static_cast<uint64_t>(READING_STATS.activeBook.minutes) * 60000 + READING_STATS.activeBook.remainderMs,
             text, sizeof(text));
    r.drawText(UI_12_FONT_ID, 24, 669, text, true, EpdFontFamily::BOLD);
    r.drawText(SMALL_FONT_ID, width / 2, 644, tr(STR_STATS_POSITION));
    snprintf(text, sizeof(text), "%u%%", READING_STATS.activeBook.progress);
    r.drawText(UI_12_FONT_ID, width / 2, 669, text, true, EpdFontFamily::BOLD);
  }
  r.drawText(UI_10_FONT_ID, 24, height - 40, "tenor/cross", true, EpdFontFamily::BOLD);
}
}  // namespace readingstatsview
