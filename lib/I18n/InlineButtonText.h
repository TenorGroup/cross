#pragma once
#include <I18n.h>

#include <string>
inline std::string plainButtonText(const char* text) {
  std::string result;
  const auto* p = reinterpret_cast<const unsigned char*>(text);
  if (!p) return result;
  while (*p) {
    if (p[0] == 0xEE && p[1] == 0x84 && p[2] >= 0x80 && p[2] <= 0x88) {
      const StrId ids[] = {StrId::STR_SELECT,
                           StrId::STR_BACK,
                           StrId::STR_DIR_UP,
                           StrId::STR_DIR_DOWN,
                           StrId::STR_DIR_LEFT,
                           StrId::STR_DIR_RIGHT,
                           StrId::STR_READER_TAB_FAVORITES,
                           StrId::STR_DIR_RIGHT,
                           StrId::STR_DIR_LEFT};
      result += I18N.get(ids[p[2] - 0x80]);
      p += 3;
    } else
      result += static_cast<char>(*p++);
  }
  return result;
}
