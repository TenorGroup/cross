#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

enum class HomeRecentStyle : unsigned char { Stats, LargeCover, Shelf };
struct HomeRecentRect { int x = 0, y = 0, w = 0, h = 0; };
struct HomeRecentLayout {
  HomeRecentStyle style = HomeRecentStyle::Stats;
  HomeRecentRect cover{}, text{}, snapshot{};
  std::array<HomeRecentRect, 3> shelf{};
  std::array<int, 3> shelfBooks{-1, -1, -1};
  int shelfCount = 0, shelfHeadingY = 0, shelfLabelHeight = 0, shelfStep = 0;
  // Left edge of a shelf cell: the 3 cells keep 24 px from each screen edge and share the width between.
  int shelfCellX(int slot) const { return 24 + slot * shelfStep; }
  int titleY = 0, titleLines = 2, authorY = 0, authorLines = 1;
  int progressY = 0, progressHeight = 0, excerptY = 0, ruleY = 0, rowY = 0;
};

inline std::pair<int, int> homeCoverFit(int w, int h, int sw, int sh) {
  if (sw <= 0 || sh <= 0) { sw = 298; sh = 450; }
  w = std::max(1, w); h = std::max(1, h);
  if (int64_t(w) * sh > int64_t(h) * sw) w = std::max(1, int(int64_t(h) * sw / sh));
  else h = std::max(1, int(int64_t(w) * sh / sw));
  return {w, h};
}

// The Large cover's neighbours stand outside the main cover, bottoms level with it and cut by the screen edge. The
// same width of each shows: the left one ends 16 px clear of the main cover, the right one starts that far from the
// right edge (an odd leftover width makes that gap 17 px). A book with no size of its own is fitted as 298 x 450.
inline constexpr int kHomeLargeCoverW = 261;
inline HomeRecentRect homePeekRect(int width, const HomeRecentRect& main, bool left, int sourceW, int sourceH) {
  constexpr int gap = 16;
  const auto fit = homeCoverFit(kHomeLargeCoverW, std::min(408, main.h), sourceW, sourceH);
  const int shown = main.x - gap;
  const int x = left ? shown - fit.first : width - shown;
  return {x, main.y + main.h - fit.second, fit.first, fit.second};
}

// The book behind shelf slot `slot`: the others in Recent order (the shown one left out), 3 cells a page.
// `first` is where the page starts in that ring, so a page step moves it by 3 and wraps round.
inline int homeShelfBook(int shown, int books, int first, int slot) {
  const int others = books - 1;
  if (others <= 0 || slot < 0 || slot >= std::min(3, others)) return -1;
  const int at = ((first + slot) % others + others) % others;
  return at < shown ? at : at + 1;
}

// Where the shelf page starts after a page step (+1 next, -1 back): 3 further along the ring of the others. With
// 3 others or fewer the shelf has one page and stays where it is.
inline int homeShelfPage(int first, int books, int direction) {
  const int others = books - 1;
  return others <= 3 ? first : ((first + 3 * direction) % others + others) % others;
}

// All Recent style decisions and book-to-slot mapping live here. Button readers keep their card.
inline HomeRecentLayout homeRecentLayout(bool touch, unsigned char value, int width, int top, int bottom,
                                         int titleHeight, int bodyHeight, int captionHeight, int excerptHeight,
                                         int shown, int books, int sourceW, int sourceH, int shelfFirst = 0) {
  HomeRecentLayout f;
  f.style = touch && value <= 2 ? static_cast<HomeRecentStyle>(value) : HomeRecentStyle::Stats;
  if (f.style == HomeRecentStyle::Stats) return f;
  f.ruleY = bottom - bodyHeight - 16;
  f.rowY = f.ruleY + 8;
  f.snapshot = {24, top, width - 48, bottom - top};
  f.progressHeight = 10 + 12 + bodyHeight;
  f.text = {24, top, width - 48, 0};
  if (f.style == HomeRecentStyle::LargeCover) {
    f.snapshot = {0, top, width, bottom - top};  // the neighbours' covers reach the screen edges
    const int textHeight = 2 * titleHeight + 8 + bodyHeight + 12 + f.progressHeight;
    auto size = homeCoverFit(kHomeLargeCoverW, std::min(408, f.ruleY - 16 - top - 16 - textHeight), sourceW, sourceH);
    f.cover = {(width - size.first) / 2, top, size.first, size.second};
    f.titleY = f.cover.y + f.cover.h + 16;
    f.authorY = f.titleY + 2 * titleHeight + 8;
    f.progressY = f.authorY + bodyHeight + 12;
  } else {
    for (int slot = 0; slot < 3; ++slot) {
      const int book = homeShelfBook(shown, books, shelfFirst, slot);
      if (book < 0) break;
      f.shelfBooks[f.shelfCount++] = book;
    }
    f.shelfLabelHeight = 8 + 6 + 6 + 2 * captionHeight;
    const int shelfH = std::min(193, bottom - 8 - 432 - f.shelfLabelHeight);
    const int shelfW = std::min(128, shelfH * 298 / 450);
    const int shelfTop = bottom - 8 - f.shelfLabelHeight - shelfH;
    f.shelfHeadingY = shelfTop - bodyHeight - 16;
    const int upperBottom = f.shelfCount ? f.shelfHeadingY - 16 : bottom - 8;
    const int upperHeight = upperBottom - top - excerptHeight - 16;
    auto size = homeCoverFit(180, std::min(272, upperHeight), sourceW, sourceH);
    f.cover = {24, top, size.first, size.second};
    f.text = {224, top, width - 248, 0};
    f.titleY = top;
    f.authorLines = 2;
    f.titleLines = std::clamp((upperHeight - 2 * bodyHeight - 8 - 12 - f.progressHeight) / titleHeight, 1, 4);
    f.authorY = top + f.titleLines * titleHeight + 8;
    f.progressY = f.authorY + 2 * bodyHeight + 12;
    f.excerptY = top + std::max(size.second, f.progressY + f.progressHeight - top) + 16;
    f.ruleY = upperBottom;
    f.shelfStep = (width - 48 - 128) / 2;
    for (int i = 0; i < f.shelfCount; ++i)
      f.shelf[i] = {f.shelfCellX(i) + (128 - shelfW) / 2, shelfTop, shelfW, shelfH};
  }
  return f;
}
