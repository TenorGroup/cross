#pragma once

struct ConfirmationBox {
  int x, y, width, height;
  constexpr bool contains(int px, int py) const { return px >= x && px < x + width && py >= y && py < y + height; }
};

struct ConfirmationLayout {
  ConfirmationBox action, cancel;
  int radius, messageHeight;
};

enum class ConfirmationDecision { Wait, Cancel, Accept, Move };

constexpr ConfirmationDecision confirmationDecision(const ConfirmationLayout& layout, bool touch, int selectedIndex,
                                                    bool back, bool confirm, bool move, bool tapped, int x, int y) {
  if (back) return ConfirmationDecision::Cancel;
  if (touch) {
    if (!tapped || x < 0) return ConfirmationDecision::Wait;
    if (!layout.action.contains(x, y)) return ConfirmationDecision::Cancel;
    return y >= layout.action.y + layout.messageHeight ? ConfirmationDecision::Accept : ConfirmationDecision::Wait;
  }
  if (move) return ConfirmationDecision::Move;
  if (confirm) return selectedIndex == 0 ? ConfirmationDecision::Accept : ConfirmationDecision::Cancel;
  return ConfirmationDecision::Wait;
}

constexpr ConfirmationLayout confirmationLayout(bool touch, int screenWidth, int contentTop, int contentBottom,
                                                int headerHeight) {
  if (touch) {
    return {{16, contentBottom - 184, screenWidth - 32, 112}, {16, contentBottom - 60, screenWidth - 32, 60}, 20, 52};
  }
  const int height = headerHeight + 120;
  const int x = (screenWidth - 366) / 2;
  const int y = contentTop + (contentBottom - contentTop - height) / 2;
  return {{x, y, 366, height}, {x, y + headerHeight + 60, 366, 60}, 19, headerHeight};
}
