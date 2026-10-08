#include <cassert>
#include <cstdio>
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
  char text[32];
  readerstatus::duration(text, sizeof(text), false, 1); assert(!strcmp(text, "-"));
  readerstatus::duration(text, sizeof(text), true, 0); assert(!strcmp(text, "0m"));
  readerstatus::duration(text, sizeof(text), true, 61); assert(!strcmp(text, "2m"));
  readerstatus::duration(text, sizeof(text), true, 3601); assert(!strcmp(text, "1h 01m"));
  readerstatus::duration(text, sizeof(text), true, UINT32_MAX); assert(strlen(text) < sizeof(text));
  puts("reader_status_layout:GREEN (3 disjoint slots, alignment, duration rounding and overflow)");
}
