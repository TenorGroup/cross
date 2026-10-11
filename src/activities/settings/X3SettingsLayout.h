#pragma once

#include <GfxRenderer.h>

#include <algorithm>
#include <cstring>

#include "components/TenorMenuChrome.h"
#include "components/icons/tenorHomeTabIcons.h"
#include "components/icons/tenorSettingsIcons.h"
#include "fontIds.h"
#include "shells/Shell.h"

namespace x3settings {
template <typename Screen> inline bool beginScreen(Screen& screen, const GfxRenderer& renderer, const char* tip = nullptr,
                                                  const char* info = nullptr, const char* infoBelow = nullptr) {
  if (!tenorchrome::x3ScreenLayoutEnabled()) return false;
  const auto layout = tenorchrome::x3ScreenLayout(renderer, tip ? tenorchrome::tipHeight(renderer, tip) : 0);
  auto body = layout.bodyRect;
  if (info) body.height = std::max(0, body.height - tenorchrome::tipHeight(renderer, info) - 8);
  if (info && infoBelow) body.height = std::max(0, body.height - tenorchrome::tipHeight(renderer, infoBelow) - 8);
  screen.setContentMarginFromScreen({static_cast<int16_t>(body.y),
                                    static_cast<int16_t>(renderer.getScreenWidth() - body.x - body.width),
                                    static_cast<int16_t>(renderer.getScreenHeight() - body.bottom()),
                                    static_cast<int16_t>(body.x)});
  return true;
}

inline void drawPath(const GfxRenderer& renderer, const char* title,
                     const freeink::Icon& icon = icon_tenor_home_settings_40) {
  if (shell::uglyParts()) {
    tenorchrome::drawHeader(renderer, title);
    return;
  }
  tenorchrome::drawX3ActivityPath(renderer, title);
}

inline tenorchrome::X3ScreenRect infoRect(const GfxRenderer& renderer, const char* info, const char* tip = nullptr,
                                         const char* infoBelow = nullptr) {
  auto body = tenorchrome::x3ScreenLayout(renderer, tip ? tenorchrome::tipHeight(renderer, tip) : 0).bodyRect;
  if (infoBelow) body.height = std::max(0, body.height - tenorchrome::tipHeight(renderer, infoBelow) - 8);
  const int height = std::min(body.height, tenorchrome::tipHeight(renderer, info));
  body.y = body.bottom() - height;
  body.height = height;
  return body;
}

inline const char* nextInfoLine(const GfxRenderer& renderer, const char* cursor, const int width, char (&line)[256]) {
  while (*cursor == ' ' || *cursor == '\r') ++cursor;
  const char* end = cursor;
  const char* space = nullptr;
  while (*end && *end != '\n') {
    const char* next = end + 1;
    while ((static_cast<unsigned char>(*next) & 0xC0) == 0x80) ++next;
    const size_t length = next - cursor;
    if (length >= sizeof(line)) break;
    std::memcpy(line, cursor, length);
    line[length] = 0;
    if (renderer.getTextWidth(SMALL_FONT_ID, line) > width && end > cursor) break;
    if (*end == ' ') space = end;
    end = next;
  }
  const bool wrapped = *end && *end != '\n';
  if (wrapped && space && space > cursor) end = space;
  const size_t length = end - cursor;
  std::memcpy(line, cursor, length);
  line[length] = 0;
  if (*end == '\n' || *end == ' ') ++end;
  return end;
}

inline int infoPageCount(const GfxRenderer& renderer, const char* text, const tenorchrome::X3ScreenRect rect) {
  const int rows = std::max(1, rect.height / renderer.getLineHeight(SMALL_FONT_ID));
  const int width = std::max(1, std::min(rect.width - 36, renderer.getScreenWidth() - 48));
  int count = 0;
  char line[256];
  for (const char* cursor = text ? text : ""; *cursor; ++count)
    cursor = nextInfoLine(renderer, cursor, width, line);
  return std::max(1, (count + rows - 1) / rows);
}

inline void drawInfoPage(const GfxRenderer& renderer, const char* text, const tenorchrome::X3ScreenRect rect, int page) {
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int width = std::max(1, std::min(rect.width - 36, renderer.getScreenWidth() - 48));
  const int rows = std::max(1, rect.height / lineHeight);
  page = std::clamp(page, 0, infoPageCount(renderer, text, rect) - 1);
  int index = 0;
  char line[256];
  for (const char* cursor = text ? text : ""; *cursor && index < (page + 1) * rows; ++index) {
    cursor = nextInfoLine(renderer, cursor, width, line);
    if (index < page * rows) continue;
    auto slot = rect;
    slot.y += (index - page * rows) * lineHeight;
    slot.height = lineHeight;
    tenorchrome::drawInfoInRect(renderer, line, slot, 1);
  }
}
}
