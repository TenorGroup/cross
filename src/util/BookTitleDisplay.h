#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

// The name of a book as the screens write it: the part before ": " when what follows is a subtitle.
// Only for drawing. The stored title (the OPF, Recent, Stats, Quotes, the file name) stays whole.
namespace booktitle {

inline std::string_view shortName(std::string_view title) {
  const auto colon = title.find(':');
  if (colon == std::string_view::npos || colon + 1 == title.size() ||
      (title[colon + 1] != ' ' && title[colon + 1] != '\t') ||
      title.find(':', colon + 1) != std::string_view::npos) return title;
  // A digit anywhere keeps the title whole: volume numbers, a year, a number as the first word.
  for (const unsigned char c : title) if (c >= '0' && c <= '9') return title;
  // Volume and part markers keep it whole too, Roman numerals included.
  for (const auto marker : {"Tập ", "tập ", "TẬP ", "Tap ", "tap ", "Phần ", "phần ", "PHẦN ",
                            "Part ", "part ", "PART ", "Vol ", "vol ", "Vol. ", "vol. ", "Volume ", "volume ", "#"})
    if (title.find(marker) != std::string_view::npos) return title;
  auto prefix = title.substr(0, colon);
  const auto first = prefix.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) return title;
  prefix = prefix.substr(first, prefix.find_last_not_of(" \t\r\n") - first + 1);
  unsigned letters = 0;
  for (size_t i = 0; i < prefix.size(); ++i) {
    const auto c = static_cast<unsigned char>(prefix[i]);
    if ((c & 0xc0) == 0x80 || c == ' ' || c == '\t') continue;
    // Combining marks U+0300..U+036F do not make a short NFD name longer.
    if (i + 1 < prefix.size() && (c == 0xcc ||
        (c == 0xcd && static_cast<unsigned char>(prefix[i + 1]) <= 0xaf))) continue;
    ++letters;
  }
  if (letters < 3) return title;
  unsigned words = 0;
  bool inWord = false;
  for (const unsigned char c : title.substr(colon + 1)) {
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') inWord = false;
    else if (!inWord && ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 0x80)) {
      ++words;
      inWord = true;
    }
  }
  return words >= 5 ? prefix : title;
}

// shortName plus a closing note in brackets the title ends on, so "X: ... (tenor version)" and "X: ..." differ.
inline std::string display(std::string_view title) {
  const auto shown = shortName(title);
  if (shown.size() == title.size()) return std::string(title);
  const auto end = title.find_last_not_of(" \t\r\n");
  const auto open = end == std::string_view::npos || title[end] != ')' ? std::string_view::npos : title.rfind('(', end);
  const auto cutAt = static_cast<size_t>(shown.data() - title.data()) + shown.size();
  if (open == std::string_view::npos || open < cutAt) return std::string(shown);
  return std::string(shown) + " " + std::string(title.substr(open, end - open + 1));
}

}  // namespace booktitle
