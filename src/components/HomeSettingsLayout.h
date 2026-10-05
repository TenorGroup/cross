#pragma once

#include <algorithm>
#include <array>
#include <vector>

#include "activities/settings/SettingsTabs.h"
#include "themes/TenorRadius.h"

namespace homesettings {

constexpr int MAX_ROWS = settingstabs::TAB_COUNT + 1;
constexpr int FRAME_X = 16;
constexpr int FRAME_PADDING = 4;
constexpr int GROUP_GAP = 16;

inline bool readingGroup(const int id) {
  using Tab = settingstabs::Tab;
  switch (static_cast<Tab>(id)) {
    case Tab::SCREEN:
    case Tab::SLEEP:
    case Tab::READER:
    case Tab::CONTROLS:
    case Tab::MOTION: return true;
    default: return false;
  }
}

// Display rows share one navigation cursor. Actions map back to the original
// Home rows: row zero is File Transfer, then the supplied settings group order.
struct Order {
  std::array<int, MAX_ROWS> originalRows{};
  int count = 0;
  int readingCount = 0;

  int original(const int displayRow) const {
    return displayRow >= 0 && displayRow < count ? originalRows[displayRow] : -1;
  }
  int display(const int originalRow) const {
    for (int i = 0; i < count; ++i)
      if (originalRows[i] == originalRow) return i;
    return -1;
  }
};

inline Order order(const std::vector<int>& groups) {
  Order result;
  const int count = std::min(static_cast<int>(groups.size()), settingstabs::TAB_COUNT);
  for (int i = 0; i < count; ++i)
    if (readingGroup(groups[i])) result.originalRows[result.count++] = i + 1;
  result.readingCount = result.count;
  result.originalRows[result.count++] = 0;
  for (int i = 0; i < count; ++i)
    if (!readingGroup(groups[i])) result.originalRows[result.count++] = i + 1;
  return result;
}

struct Rect {
  int x = 0, y = 0, width = 0, height = 0;
};
struct Layout {
  std::array<Rect, 2> frames{}, rows{};
  int rowHeight = 0, rowGap = 0, rowPaddingY = 0, radius = 0;
  bool fits = false;
};

// Font line height comes from the live draw target. Padding and the inter-group
// gap are included before fitting rows, so the last row keeps all its hit area.
inline Layout layout(const Rect body, const Order& order, const bool touch, const int tier, const int lineHeight) {
  Layout result;
  if (order.count <= 0 || body.width <= 2 * (FRAME_X + FRAME_PADDING)) return result;
  const int counts[] = {order.readingCount, order.count - order.readingCount};
  const int blocks = (counts[0] > 0) + (counts[1] > 0);
  const int size = tier >= 0 && tier < 3 ? tier : 0;
  constexpr int BUTTON_ROWS[] = {52, 53, 51};
  result.rowGap = touch ? 6 : size == 0 ? 4 : 2;
  const int preferred = touch ? std::max(56, lineHeight + 16) : BUTTON_ROWS[size];
  const int minimum = touch ? preferred : lineHeight + 4;
  const int fixed = 2 * FRAME_PADDING * blocks + GROUP_GAP * (blocks - 1) +
                    result.rowGap * (order.count - blocks);
  result.rowHeight = std::min(preferred, (body.height - fixed) / order.count);
  if (result.rowHeight < minimum) return result;
  result.rowPaddingY = (result.rowHeight - lineHeight) / 2;
  result.radius = tenorradius::container(tenorradius::leaf(result.rowHeight), FRAME_PADDING);
  int y = body.y;
  for (int i = 0; i < 2; ++i) {
    if (counts[i] == 0) continue;
    const int height = counts[i] * result.rowHeight + (counts[i] - 1) * result.rowGap;
    result.frames[i] = {FRAME_X, y, body.width - 2 * FRAME_X, height + 2 * FRAME_PADDING};
    result.rows[i] = {FRAME_X + FRAME_PADDING, y + FRAME_PADDING,
                      body.width - 2 * (FRAME_X + FRAME_PADDING), height};
    y += height + 2 * FRAME_PADDING + GROUP_GAP;
  }
  result.fits = y - GROUP_GAP <= body.y + body.height;
  return result;
}

}  // namespace homesettings
