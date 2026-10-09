#pragma once

#include <GfxRenderer.h>
#include <cstdio>
#include <cstring>

#include "components/TenorMenuChrome.h"
#include "components/icons/tenorHomeTabIcons.h"
#include "components/icons/tenorReaderTabIcons.h"
#include "fontIds.h"
#include "activities/X3LegacyPath.h"
#include "shells/Shell.h"

namespace x3readerchrome {
template <class Visitor>
inline int eachInfoLine(const GfxRenderer& renderer, const char* text, Visitor visit) {
  const char* cursor = text ? text : "";
  int count = 0;
  while (*cursor) {
    char line[512];
    size_t used = 0, lastSpace = 0;
    const char* next = cursor;
    while (*next && *next != '\n') {
      const char* end = next + 1;
      while (*end && (static_cast<unsigned char>(*end) & 0xc0) == 0x80) ++end;
      const size_t bytes = end - next;
      if (used + bytes >= sizeof(line)) break;
      std::memcpy(line + used, next, bytes);
      line[used + bytes] = 0;
      if (used && renderer.getTextWidth(SMALL_FONT_ID, line) > renderer.getScreenWidth() - 48) break;
      used += bytes;
      if (*next == ' ') lastSpace = used;
      next = end;
    }
    if (*next && *next != '\n' && lastSpace) used = lastSpace;
    line[used] = 0;
    visit(count++, line);
    cursor += used;
    if (*cursor == '\n') ++cursor;
  }
  return count;
}

inline void drawPath(const GfxRenderer& renderer, const tenorchrome::X3ScreenRect bar, const freeink::Icon& icon,
                     const char* path) {
  if (shell::uglyParts()) {
    uglylegacy::drawReaderPath(renderer, bar, icon, path);
    return;
  }
  tenorchrome::drawX3ActivityPath(renderer, path);
}
}  // namespace x3readerchrome
