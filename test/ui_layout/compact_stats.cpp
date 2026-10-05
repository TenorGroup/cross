#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "components/CompactStats.h"

static std::string dur(const uint64_t minutes) {
  char text[24];
  compactstats::duration(minutes, text, sizeof(text));
  return text;
}
static std::string dayMonth(const uint32_t day) {
  char text[24];
  compactstats::dayMonth(day, text, sizeof(text));
  return text;
}

int main() {
  // Hours and minutes, units after the numbers, space between hours and minutes, no zero minutes.
  assert(dur(0) == "0m");
  assert(dur(1) == "1m");
  assert(dur(59) == "59m");
  assert(dur(60) == "1h");
  assert(dur(654) == "10h 54m");
  assert(dur(61) == "1h 1m");
  assert(dur(23 * 60 + 14) == "23h 14m");
  assert(dur(3 * 60 + 19) == "3h 19m");
  assert(dur(120 * 60 + 5) == "120h 5m");
  assert(dur(999 * 60 + 59) == "999h 59m");
  // A short buffer cuts, never overruns.
  char tiny[4];
  compactstats::duration(23 * 60 + 14, tiny, sizeof(tiny));
  assert(std::strlen(tiny) == 3);
  // The expected day: day and month, no year, whatever the year.
  assert(dayMonth(20261227) == "27/12");
  assert(dayMonth(20270105) == "05/01");
  // Only digits are bold: numbers in bold, units and signs light.
  assert(compactstats::isNumber('0') && compactstats::isNumber('9'));
  assert(!compactstats::isNumber('h') && !compactstats::isNumber('m') && !compactstats::isNumber('%') &&
         !compactstats::isNumber('/') && !compactstats::isNumber('>'));
  std::string rendered;
  int width = 0;
  compactstats::runs(dur(654).c_str(), [&](const std::string& part, bool number) {
    rendered += part;
    width += part.size() * (number ? 12 : 7);
  });
  assert(rendered == "10h 54m");
  assert(width == 69);  // All four digits and all three unit/space characters were measured.
  puts("PASS: compact stat values and complete mixed-font runs");
  return 0;
}
