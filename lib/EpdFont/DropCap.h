#pragma once
#include <Utf8.h>

#include <cstdint>

namespace dropcap {
struct Initial {
  uint32_t codepoint = 0;
  uint8_t prefixBytes = 0;
  uint8_t endBytes = 0;
};
// Latin initials, including precomposed Vietnamese. Unsupported clusters stay intact.
inline Initial initial(const char* text) {
  if (!text) return {};
  const auto* p = reinterpret_cast<const unsigned char*>(text);
  const auto* start = p;
  for (unsigned i = 0; i < 4 && *p; ++i) {
    const auto* before = p;
    const uint32_t cp = utf8NextCodepoint(&p);
    const bool letter = (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
                        (cp >= 0xC0 && cp <= 0x24F && cp != 0xD7 && cp != 0xF7) || (cp >= 0x1E00 && cp <= 0x1EFF);
    if (letter) {
      const auto* next = p;
      if (utf8IsCombiningMark(utf8NextCodepoint(&next))) return {};
      return {cp, static_cast<uint8_t>(before - start), static_cast<uint8_t>(p - start)};
    }
    if (cp != '\"' && cp != '\'' && cp != 0x2018 && cp != 0x201C && cp != 0xAB && cp != '(') return {};
  }
  return {};
}
}  // namespace dropcap
