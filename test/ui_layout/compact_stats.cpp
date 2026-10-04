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
  // Hours and minutes, units after the numbers, no spaces, no zero minutes.
  assert(dur(0) == "0m");
  assert(dur(1) == "1m");
  assert(dur(59) == "59m");
  assert(dur(60) == "1h");
  assert(dur(61) == "1h1m");
  assert(dur(23 * 60 + 14) == "23h14m");
  assert(dur(3 * 60 + 19) == "3h19m");
  assert(dur(120 * 60 + 5) == "120h5m");
  assert(dur(999 * 60 + 59) == "999h59m");
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
  puts("PASS: compact stat values");
  return 0;
}
