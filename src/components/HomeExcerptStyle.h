#pragma once
#include <Utf8.h>

inline bool homeExcerptUsesUiFont(const char* text) {
  const auto* cursor = reinterpret_cast<const unsigned char*>(text);
  while (*cursor) {
    if (utf8IsCjkCodepoint(utf8NextCodepoint(&cursor))) return true;
  }
  return false;
}
