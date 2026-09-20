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
