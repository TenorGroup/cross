#pragma once
#include <cstdint>

namespace ugly {
namespace logic {

// Sheets of an answer sheet: question blocks of `heights` (ink, no gap) laid in order on sheets whose room is
// `firstRoom` on the first and `laterRoom` on the others, `gap` between 2 blocks, `tightGap` when a sheet needs it.
// A sheet of 1 question is never made while the questions can go otherwise (founder 06/10/2026): fewest sheets of
// 1 question first, then fewest sheets, then the normal gap, then sheets as even as can be. Fills `sheetOf` and
// `tightOf` (per sheet) and returns the count of sheets. Up to 64 blocks; more are laid greedily, one gap.
inline int layoutSheets(const int* heights, const int count, const int firstRoom, const int laterRoom, const int gap,
                        const int tightGap, int* sheetOf, uint8_t* tightOf) {
  if (count <= 0) return 1;
  constexpr int MAX = 64;
  struct Cost {
    int singles = 0, sheets = 0, tight = 0, squares = 0;
    bool operator<(const Cost& o) const {
      if (singles != o.singles) return singles < o.singles;
      if (sheets != o.sheets) return sheets < o.sheets;
      if (tight != o.tight) return tight < o.tight;
      return squares < o.squares;
    }
  };
  // 0 normal, 1 tight, -1 the blocks [from, to) do not fit the room.
  const auto fit = [&](const int from, const int to, const int room) {
    int ink = 0;
    for (int i = from; i < to; ++i) ink += heights[i];
    if (ink + (to - from - 1) * gap <= room) return 0;
    if (ink + (to - from - 1) * tightGap <= room) return 1;
    return to - from == 1 ? 0 : -1;  // a block taller than a sheet still gets a sheet of its own
  };
  if (count > MAX) {
    int sheet = 0, used = 0;
    for (int i = 0; i < count; ++i) {
      const int room = sheet ? laterRoom : firstRoom;
      if (used && used + gap + heights[i] > room) { ++sheet; used = 0; }
      used += (used ? gap : 0) + heights[i];
      sheetOf[i] = sheet;
      tightOf[sheet] = 0;
    }
    return sheet + 1;
  }
  // best[i]: blocks i.. laid from a later sheet on; cut[i] where that sheet ends.
  Cost best[MAX + 1];
  int cut[MAX + 1] = {};
  int tight[MAX + 1] = {};
  bool known[MAX + 1] = {};
  known[count] = true;
  const auto add = [&](const Cost& rest, const int from, const int to, const int how) {
    Cost c = rest;
    c.singles += to - from == 1 && count > 1;
    c.sheets += 1;
    c.tight += how;
    c.squares += (to - from) * (to - from);
    return c;
  };
  for (int i = count - 1; i >= 1; --i)
    for (int j = i + 1; j <= count; ++j) {
      const int how = fit(i, j, laterRoom);
      if (how < 0) break;
      if (!known[j]) continue;
      const Cost c = add(best[j], i, j, how);
      if (!known[i] || c < best[i]) { best[i] = c; cut[i] = j; tight[i] = how; known[i] = true; }
    }
  Cost first{};
  int firstCut = 1, firstTight = 0;
  bool found = false;
  for (int j = 1; j <= count; ++j) {
    const int how = fit(0, j, firstRoom);
    if (how < 0) break;
    if (!known[j]) continue;
    const Cost c = add(best[j], 0, j, how);
    if (!found || c < first) { first = c; firstCut = j; firstTight = how; found = true; }
  }
  int sheet = 0;
  for (int from = 0, to = firstCut, how = firstTight; from < count; ++sheet) {
    for (int i = from; i < to; ++i) sheetOf[i] = sheet;
    tightOf[sheet] = static_cast<uint8_t>(how);
    from = to;
    if (from < count) { how = tight[from]; to = cut[from]; }
  }
  return sheet;
}

}  // namespace logic
}  // namespace ugly
