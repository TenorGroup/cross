#pragma once
#include <array>
#include <cstring>
#include <string>
namespace readingexcerpt {
// Skip the first (possibly partial) sentence. Keep one complete, bounded sentence.
class Builder {
  std::array<char, 385> text{};
  size_t length = 0;
  unsigned words = 0;
  bool boundarySeen = false, complete = false, overflow = false;

 public:
  void word(const char* value) {
    if (value) word(value, strlen(value));
  }
  void line(const std::string& value) {
    size_t pos = 0;
    while (pos < value.size()) {
      while (pos < value.size() && static_cast<unsigned char>(value[pos]) <= 32) ++pos;
      const size_t start = pos;
      while (pos < value.size() && static_cast<unsigned char>(value[pos]) > 32) ++pos;
      if (pos > start) word(value.data() + start, pos - start);
    }
  }
  void word(const char* value, size_t n) {
    if (complete || !value || !n) return;
    size_t end = n;
    // Vietnamese dialogue commonly closes with a UTF-8 curly quote after punctuation.
    while (end >= 3 && (memcmp(value + end - 3, "”", 3) == 0 || memcmp(value + end - 3, "’", 3) == 0)) end -= 3;
    while (end && (value[end - 1] == '\"' || value[end - 1] == '\'' || value[end - 1] == ')')) --end;
    const bool stop = end && (value[end - 1] == '.' || value[end - 1] == '!' || value[end - 1] == '?');
    if (!boundarySeen) {
      if (stop) boundarySeen = true;
      return;
    }
    if (length + n + (length ? 1 : 0) > 384) overflow = true;
    if (!overflow) {
      if (length) text[length++] = ' ';
      memcpy(text.data() + length, value, n);
      length += n;
      text[length] = 0;
      ++words;
    }
    if (stop) {
      if (!overflow && words >= 5 && length >= 24)
        complete = true;
      else {
        length = 0;
        words = 0;
        overflow = false;
        text[0] = 0;
      }
    }
  }
  std::string result() const { return complete ? std::string(text.data(), length) : std::string{}; }
};
}  // namespace readingexcerpt
