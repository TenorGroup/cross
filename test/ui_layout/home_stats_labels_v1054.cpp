#include <cassert>
#include <cstdio>

#include "components/HomeExcerptStyle.h"

int main() {
  HomeStatsInput in;
  in.top = 44;
  in.bottom = 632;
  in.labelLineHeight = 31;
  in.valueLineHeight = 38;
  in.valueTail = 7;
  in.rows = (1u << HOME_STAT_COUNT) - 1;
  in.labelHeights[HOME_STAT_TOTAL] = 62;
  in.valueHeights[HOME_STAT_AVERAGE] = 62;
  in.valueTails[HOME_STAT_AVERAGE] = 6;
  const auto column = homeStatsLayout(in);
  assert(column.rows == in.rows);
  assert(column.valueY[HOME_STAT_TOTAL] - column.labelY[HOME_STAT_TOTAL] == 62);
  assert(column.valueY[HOME_STAT_TURNS] + 38 - 7 == in.bottom);
  for (int row = 0; row + 1 < HOME_STAT_COUNT; ++row) {
    const int height = in.valueHeights[row] ? in.valueHeights[row] : in.valueLineHeight;
    assert(column.valueY[row] + height <= column.labelY[row + 1]);
  }
  puts("PASS: wrapped labels and values leave six complete rows with the last ink on the cover edge");
}
