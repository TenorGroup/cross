#pragma once

#include <GfxRenderer.h>
#include <cstdio>
#include <cstring>

#include "components/TenorMenuChrome.h"
#include "components/icons/tenorHomeTabIcons.h"
#include "components/icons/tenorReaderTabIcons.h"
#include "fontIds.h"

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
  const int iconX = bar.x + 18;
  const int pillX = iconX + icon.w + 12;
  const int pillWidth = bar.x + bar.width - 18 - pillX;
  tenorchrome::drawBarIcon(renderer, icon.bits, icon.w, icon.h, iconX, bar.y + (bar.height - icon.h) / 2, true);
  tenorchrome::drawPillRing(renderer, pillX, bar.y + 6, pillWidth, bar.height - 12, 2, true);
  const int room = pillWidth - 24;
  const char* suffix = path ? path : "";
  const bool clipped = renderer.getTextWidth(SMALL_FONT_ID, suffix) > room || std::strlen(suffix) >= 508;
  if (clipped) {
    const int available = room - renderer.getTextWidth(SMALL_FONT_ID, "…");
    while (*suffix && (std::strlen(suffix) >= 508 || renderer.getTextWidth(SMALL_FONT_ID, suffix) > available)) {
      ++suffix;
      while (*suffix && (static_cast<unsigned char>(*suffix) & 0xc0) == 0x80) ++suffix;
    }
  }
  char label[512];
  std::snprintf(label, sizeof(label), "%s%s", clipped ? "…" : "", suffix);
  renderer.drawText(SMALL_FONT_ID, pillX + 12, bar.y + (bar.height - renderer.getLineHeight(SMALL_FONT_ID)) / 2, label);
}
}  // namespace x3readerchrome
