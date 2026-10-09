#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "activities/reader/ReaderStatusLayout.h"

int main() {
  for (int width : {480, 528, 800, 920}) {
    int previousEnd = 0;
    for (int index = 0; index < 3; ++index) {
      const auto lane = readerstatus::cell(width, index);
      assert(lane.x >= previousEnd && lane.x + lane.width <= width - 12);
      for (int text = 0; text <= lane.width; ++text) {
        const int x = readerstatus::textX(lane, index, text);
        assert(x >= lane.x && x + text <= lane.x + lane.width);
      }
      previousEnd = lane.x + lane.width;
    }
  }
  for (int width : {480, 528, 801}) {
    for (int left : {0, 24, 62, 100, 180, 500}) {
      for (int right : {0, 24, 62, 100, 180, 500}) {
        const auto lane = readerstatus::chapterCell(width, left, right);
        assert(lane.width == std::max(0, width - 24 - 2 * std::max(left, right) - 16));
        assert(std::abs(lane.x * 2 + lane.width - width) <= 1);
        if (!lane.width) continue;
        assert(lane.x - (12 + left) >= 8);
        assert(width - 12 - right - (lane.x + lane.width) >= 8);
        if (left <= 62 && right <= 62) assert(lane.width > readerstatus::cell(width, 1).width);
        for (int measured = 0; measured <= lane.width; ++measured) {
          const int x = readerstatus::textX(lane, 1, measured);
          assert(x >= lane.x && x + measured <= lane.x + lane.width);
          assert(std::abs(x * 2 + measured - width) <= 2);
        }
      }
    }
  }
  char text[32];
  readerstatus::duration(text, sizeof(text), false, 1); assert(!strcmp(text, "-"));
  readerstatus::duration(text, sizeof(text), true, 0); assert(!strcmp(text, "0m"));
  readerstatus::duration(text, sizeof(text), true, 61); assert(!strcmp(text, "2m"));
  readerstatus::duration(text, sizeof(text), true, 3601); assert(!strcmp(text, "1h 01m"));
  readerstatus::duration(text, sizeof(text), true, UINT32_MAX); assert(strlen(text) < sizeof(text));
  puts("reader_status_layout:GREEN (3 slots, chapter space, gaps, alignment, duration rounding and overflow)");
}
