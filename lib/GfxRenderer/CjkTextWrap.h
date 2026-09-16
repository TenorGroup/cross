#pragma once

#include <Utf8.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace cjkTextWrap {
inline bool containsCjk(const char* text) {
  if (!text) return false;
  auto p = reinterpret_cast<const unsigned char*>(text);
  while (*p) {
    if (utf8IsCjkCodepoint(utf8NextCodepoint(&p))) return true;
  }
  return false;
}

inline bool opening(const uint32_t cp) {
  return cp == 0x300C || cp == 0x300E || cp == 0xFF08 || cp == 0x300A || cp == 0x201C || cp == 0x2018;
}
inline bool closing(const uint32_t cp) {
  switch (cp) {
    case 0x3002:
    case 0xFF0C:
    case 0x3001:
    case 0xFF1B:
    case 0xFF1A:
    case 0xFF01:
    case 0xFF1F:
    case 0x300D:
    case 0x300F:
    case 0xFF09:
    case 0x300B:
    case 0x2026:
    case 0x201D:
    case 0x2019:
      return true;
    default:
      return false;
  }
}

// Called only for CJK-bearing UI text. Measurement uses the renderer's complete
// candidate line, including its font fallback and kerning. No per-glyph estimates.
// Emergency breaks split an overlong unbreakable token at a codepoint boundary.
template <typename Measure, typename Truncate>
std::vector<std::string> wrap(const char* input, const int maxWidth, const int maxLines, Measure measure,
                              Truncate truncate) {
  std::vector<std::string> lines;
  if (!input || maxWidth <= 0 || maxLines <= 0) return lines;
  const std::string_view text(input);
  lines.reserve(std::min(maxLines, 16));
  std::string candidate;
  candidate.reserve(std::min<size_t>(text.size(), 256));
  size_t start = 0;
  while (start < text.size() && static_cast<int>(lines.size()) < maxLines) {
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t')) ++start;
    if (start == text.size()) break;
    if (static_cast<int>(lines.size()) == maxLines - 1) {
      const size_t nl = text.find('\n', start);
      candidate.assign(text.substr(start, nl == std::string_view::npos ? text.size() - start : nl - start));
      if (nl != std::string_view::npos && nl + 1 < text.size()) candidate += "...";
      lines.push_back(truncate(candidate.c_str(), maxWidth));
      break;
    }
    size_t cursor = start;
    size_t fit = start;
    size_t legal = start;
    size_t next = start;
    bool overflow = false;
    while (cursor < text.size() && text[cursor] != '\n') {
      auto p = reinterpret_cast<const unsigned char*>(text.data() + cursor);
      const uint32_t cp = utf8NextCodepoint(&p);
      next = reinterpret_cast<const char*>(p) - text.data();
      candidate.assign(text.substr(start, next - start));
      if (measure(candidate.c_str()) > maxWidth) {
        overflow = true;
        break;
      }
      fit = next;
      const uint32_t after = *p ? utf8NextCodepoint(&p) : 0;
      const bool boundary = cp == ' ' || cp == '\t' || after == ' ' || after == '\t' || utf8IsCjkBreakable(cp) ||
                            utf8IsCjkBreakable(after);
      if (boundary && !opening(cp) && !closing(after) && !utf8IsCombiningMark(after)) legal = next;
      cursor = next;
    }
    const size_t cut = !overflow ? cursor : legal > start ? legal : fit > start ? fit : next;
    candidate.assign(text.substr(start, cut - start));
    while (!candidate.empty() && (candidate.back() == ' ' || candidate.back() == '\t')) candidate.pop_back();
    lines.push_back(measure(candidate.c_str()) <= maxWidth ? candidate : truncate(candidate.c_str(), maxWidth));
    start = cut;
    if (!overflow && start < text.size() && text[start] == '\n') ++start;
  }
  return lines;
}
}  // namespace cjkTextWrap
