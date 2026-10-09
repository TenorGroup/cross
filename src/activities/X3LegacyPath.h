#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "components/TenorMenuChrome.h"
#include "components/icons/tenorHomeTabIcons.h"
#include "fontIds.h"

namespace uglylegacy {
inline void drawFormPath(const GfxRenderer& renderer, const tenorchrome::X3ScreenRect rect, const char* title,
                    const char* prefix = "") {
  title = title ? title : "";
  prefix = prefix ? prefix : "";
  const size_t prefixLength = std::strlen(prefix), titleLength = std::strlen(title);
  const size_t separator = prefixLength ? 1 : 0;
  const size_t total = prefixLength + separator + titleLength;
  const auto byteAt = [&](size_t index) {
    return index < prefixLength ? prefix[index] : index < prefixLength + separator ? '/' :
        title[index - prefixLength - separator];
  };
  char path[512];
  size_t start = total >= sizeof(path) ? total - sizeof(path) + 1 : 0;
  while (start < total && (static_cast<unsigned char>(byteAt(start)) & 0xc0) == 0x80) ++start;
  size_t length = total - start;
  for (size_t index = 0; index < length; ++index) path[index] = byteAt(start + index);
  path[length] = 0;
  while (length && renderer.getTextWidth(UI_12_FONT_ID, path) > rect.width - 80) {
    size_t next = 1;
    while (next < length && (static_cast<unsigned char>(path[next]) & 0xc0) == 0x80) ++next;
    std::memmove(path, path + next, length - next + 1);
    length -= next;
  }
  const int height = renderer.getLineHeight(UI_12_FONT_ID) + 16;
  const int width = std::min(rect.width - 48, renderer.getTextWidth(UI_12_FONT_ID, path) + 32);
  const int top = rect.y + (rect.height - height) / 2;
  tenorchrome::drawPillRing(renderer, 24, top, width, height, 2, false);
  renderer.drawText(UI_12_FONT_ID, 40, top + 8, path);
}

inline void drawSettingsPath(const GfxRenderer& renderer, const char* title,
                     const freeink::Icon& icon = icon_tenor_home_settings_40) {
  const auto bar = tenorchrome::x3ScreenLayout(renderer).barRect;
  const int iconLeft = 16;
  const int pillLeft = iconLeft + icon.w + 12;
  const int pillMaxWidth = renderer.getScreenWidth() - pillLeft - 16;
  const int pillTop = bar.y + tenorchrome::BAR_TAB_INSET;
  const int pillHeight = bar.height - 2 * tenorchrome::BAR_TAB_INSET;
  const auto clip = renderer.getClipRect();
  renderer.setClipRect(bar.x, bar.y, bar.width, bar.height);
  tenorchrome::drawBarIcon(renderer, icon.bits, icon.w, icon.h, iconLeft,
                           bar.y + (bar.height - icon.h) / 2, false);
  tenorchrome::drawPathPill(renderer, SMALL_FONT_ID, title, pillLeft, pillTop, pillMaxWidth, pillHeight, 14, 14, false);
  renderer.setClipRect(clip[0], clip[1], clip[2], clip[3]);
}

inline void drawReaderPath(const GfxRenderer& renderer, const tenorchrome::X3ScreenRect bar, const freeink::Icon& icon,
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
}
