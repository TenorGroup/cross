#pragma once

struct ReadingStatsLayout {
  int value, periodLabel, periodValue, chartBottom, dates, meanLabel, meanValue, habitsLabel, habitsValue, note, height;
  ReadingStatsLayout(int caption, int subtitle, int body, int display) {
    value = subtitle + 4;
    periodLabel = value + display + 4;
    periodValue = periodLabel + subtitle + 4;
    chartBottom = periodValue + subtitle + 4 + 66;
    dates = chartBottom + 4;
    meanLabel = dates + caption + 4;
    meanValue = meanLabel + caption + 4;
    habitsLabel = meanValue + body + 4;
    habitsValue = habitsLabel + caption + 4;
    note = habitsValue + body + 4;
    height = note + caption;
  }
  int offset(int old) const {
    switch (old) {
      case 28: return value;
      case 80: return periodLabel;
      case 105: return periodValue;
      case 203: return chartBottom - 22;
      case 225: return chartBottom;
      case 233: return dates;
      case 270: return meanLabel;
      case 296: return meanValue;
      case 336: return habitsLabel;
      case 362: return habitsValue;
      case 402: return note;
      default: return old;
    }
  }
};

// Home owns its padding and 2 panels. Sleep retains ReadingStatsLayout above.
struct HomeReadingStatsLayout {
  int value, periodLabel, periodValue, chartTop, chartBottom, dates;
  int meanLabel, meanValue, habitsLabel, habitsValue, note;
  int overviewHeight, habitsHeight, habitsTop, height, chartHeight;
  HomeReadingStatsLayout(int caption, int subtitle, int body, bool compact = false, bool enlarged = false) {
    value = 12 + subtitle + 4;
    periodLabel = value + body + 4;
    periodValue = periodLabel + subtitle + 4;
    chartTop = periodValue + subtitle + 4;
    meanLabel = 12;
    meanValue = meanLabel + caption + 4;
    habitsLabel = meanValue + body + 4;
    habitsValue = habitsLabel + caption + 4;
    note = habitsValue + body + 4;
    habitsHeight = note + caption + 12;
    chartHeight = 66;
    const int naturalHeight = chartTop + chartHeight + 4 + caption + 12 + 12 + habitsHeight;
    const int budget = compact ? 390 : 430;
    if (!enlarged && naturalHeight > budget) chartHeight -= naturalHeight - budget;
    if (chartHeight < 1) chartHeight = 1;
    chartBottom = chartTop + chartHeight;
    dates = chartBottom + 4;
    overviewHeight = dates + caption + 12;
    habitsTop = overviewHeight + 12;
    height = habitsTop + habitsHeight;
  }
};
