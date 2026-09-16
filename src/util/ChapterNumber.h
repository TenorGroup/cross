#pragma once
#include <cstdint>
#include <cstring>
namespace chapterNumber {
inline uint32_t parse(const char* title) {
  const char* p = title;
  while (*p == ' ' || *p == '\t' || *p == '\n') ++p;
  const char* prefixes[] = {"Chương", "chương", "CHƯƠNG", "Chapter", "chapter", "CHAPTER"};
  const char* number = p;
  for (const char* prefix : prefixes) {
    const char* found = std::strstr(p, prefix);
    if (found && (found == p || found[-1] == ' ' || found[-1] == '-' || found[-1] == ':')) {
      const char* candidate = found + std::strlen(prefix);
      if (*candidate != ' ' && *candidate != '\t') continue;
      number = candidate;
      while (*number == ' ' || *number == '\t') ++number;
      break;
    }
  }
  if (*number < '0' || *number > '9') return 0;
  uint32_t value = 0;
  while (*number >= '0' && *number <= '9') {
    value = value * 10 + (*number++ - '0');
    if (value > 999999) return 0;
  }
  if (*number && *number != ' ' && *number != '\t' && *number != ':' && *number != '.' && *number != '-' &&
      *number != ')')
    return 0;
  return value;
}
}  // namespace chapterNumber
