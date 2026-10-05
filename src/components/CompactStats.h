#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

// The Recent card's stat values, short enough for a narrow column: "23h 14m", "3h 19m", "27/12". One
// place for the format, so every surface that shows the column writes the same figures.
namespace compactstats {

// Hours and minutes of `minutes`: "0m", "59m", "1h", "23h 14m". A whole hour drops its minutes.
inline void duration(const uint64_t minutes, char* text, const size_t size) {
  const unsigned long long hours = minutes / 60, rest = minutes % 60;
  if (hours == 0)
    snprintf(text, size, "%llum", rest);
  else if (rest == 0)
    snprintf(text, size, "%lluh", hours);
  else
    snprintf(text, size, "%lluh %llum", hours, rest);
}

// Day code YYYYMMDD as day and month, "27/12": the year is the reader's today or next, never needed.
inline void dayMonth(const uint32_t day, char* text, const size_t size) {
  snprintf(text, size, "%02u/%02u", static_cast<unsigned>(day % 100), static_cast<unsigned>(day / 100 % 100));
}

// Numbers are set in bold, their units and signs (h, m, %, /) in the regular face of the same size.
inline bool isNumber(const char c) { return c >= '0' && c <= '9'; }

// Use the same runs for measurement and drawing so bold digits and regular units share one width.
template <typename Visit>
inline void runs(const char* value, Visit visit) {
  while (*value) {
    const bool number = isNumber(*value);
    const char* end = value + 1;
    while (*end && isNumber(*end) == number) ++end;
    visit(std::string(value, end), number);
    value = end;
  }
}

}  // namespace compactstats
