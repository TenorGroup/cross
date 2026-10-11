#pragma once

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

#include "components/TenorMenuChrome.h"
#include "fontIds.h"
#include "shells/Shell.h"

namespace x3activity {
inline void drawBar(const GfxRenderer& renderer, [[maybe_unused]] const tenorchrome::X3ScreenRect rect, const char* title,
                    const char* prefix = "") {
  if (shell::uglyParts()) {
    tenorchrome::drawHeader(renderer, title, prefix);
    return;
  }
  tenorchrome::drawX3ActivityPath(renderer, title);
}

class InfoPages {
 public:
  int page = 0;
  int pages = 1;

  bool turn(const int direction) {
    if (pages <= 1 || direction == 0) return false;
    page = (page + pages + direction) % pages;
    return true;
  }

  template <typename Visitor>
  static void lines(const GfxRenderer& renderer, const char* text, const int width, Visitor visit) {
    if (!text || !*text) return;
    char line[512];
    const char* cursor = text;
    while (*cursor) {
      size_t length = 0, word = 0;
      while (cursor[length] && cursor[length] != '\n') {
        size_t next = length + 1;
        while (cursor[next] && (static_cast<unsigned char>(cursor[next]) & 0xc0) == 0x80) ++next;
        if (next >= sizeof(line)) break;
        std::memcpy(line, cursor, next);
        line[next] = 0;
        if (renderer.getTextWidth(SMALL_FONT_ID, line) > width && length) break;
        length = next;
        if (cursor[length - 1] == ' ') word = length;
      }
      if (cursor[length] && cursor[length] != '\n' && word) length = word;
      std::memcpy(line, cursor, length);
      line[length] = 0;
      visit(line);
      cursor += length;
      if (*cursor == '\n') ++cursor;
    }
  }

  // The height draw() needs for these lines inside `available` and nothing more: a row per line (as many as the room
  // keeps, one row short of it when the lines run to pages), plus one spare row, which draw() keeps for the page
  // position. `paged` says whether the lines run to more than one page; when they do the spare row is the position.
  static int fit(const GfxRenderer& renderer, const int width, const int available,
                 const std::initializer_list<const char*> information, bool* paged = nullptr) {
    const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const int capacity = std::max(1, available / lineHeight - 1);
    int count = 0;
    for (const auto* text : information) lines(renderer, text, width - 48, [&](const char*) { ++count; });
    if (paged) *paged = count > capacity;
    return (std::min(count, capacity) + 1) * lineHeight;
  }

  void draw(const GfxRenderer& renderer, tenorchrome::X3ScreenRect body,
            const std::initializer_list<const char*> information) {
    const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const int capacity = std::max(1, body.height / lineHeight - 1);
    int count = 0;
    for (const auto* text : information) lines(renderer, text, body.width - 48, [&](const char*) { ++count; });
    pages = std::max(1, (count + capacity - 1) / capacity);
    page = std::min(page, pages - 1);
    int index = 0;
    for (const auto* text : information) {
      lines(renderer, text, body.width - 48, [&](const char* line) {
        if (index >= page * capacity && index < (page + 1) * capacity) {
          const tenorchrome::X3ScreenRect row{body.x, body.y + (index - page * capacity) * lineHeight,
                                             body.width, lineHeight};
          tenorchrome::drawInfoInRect(renderer, line, row, 1);
        }
        ++index;
      });
    }
    if (pages > 1) {
      char position[32];
      snprintf(position, sizeof(position), "%d/%d", page + 1, pages);
      body.y = body.bottom() - lineHeight;
      body.height = lineHeight;
      tenorchrome::drawInfoInRect(renderer, position, body, 1);
    }
  }
};
}
