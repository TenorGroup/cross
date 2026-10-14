#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

// The one-line view of a book search hit: the excerpt the search kept (booksearch::Result), cut to the room a row
// gives it, with the matched words in bold. Pure: the caller measures text (`width`), so the host can test it with a
// fixed-pitch face.
namespace booksearchlayout {

struct Piece {
  std::string text;
  bool bold = false;
  // A space follows this piece (the excerpt had one there, or an ellipsis stands next).
  bool gapAfter = false;
};
using Pieces = std::vector<Piece>;

namespace detail {
constexpr char kEllipsis[] = "\xE2\x80\xA6";

inline size_t codepointLength(const unsigned char lead) { return lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4; }

inline Pieces compose(const std::string& s, const std::vector<size_t>& at, const size_t a, const size_t ms,
                      const size_t me, const size_t b, const bool head, const bool tail) {
  const auto space = [&](const size_t k) { return s[at[k]] == ' '; };
  size_t from = a, before = ms, after = me, to = b;
  while (from < before && space(from)) ++from;
  const bool gapBefore = before > from && space(before - 1);
  while (before > from && space(before - 1)) --before;
  const bool gapAfter = after < to && space(after);
  while (after < to && space(after)) ++after;
  while (to > after && space(to - 1)) --to;
  Pieces out;
  const auto slice = [&](const size_t x, const size_t y) { return s.substr(at[x], at[y] - at[x]); };
  if (head) out.push_back({kEllipsis, false, true});
  if (before > from) out.push_back({slice(from, before), false, gapBefore});
  out.push_back({slice(ms, me), true, after < to ? gapAfter : tail});
  if (after < to) out.push_back({slice(after, to), false, tail});
  if (tail) out.push_back({kEllipsis, false, false});
  return out;
}

template <class Width>
int total(const Pieces& pieces, const int gap, Width& width) {
  int sum = 0;
  for (size_t i = 0; i < pieces.size(); ++i)
    sum += width(pieces[i].text, pieces[i].bold) + (pieces[i].gapAfter && i + 1 < pieces.size() ? gap : 0);
  return sum;
}
}  // namespace detail

// `width(text, bold)` is the width of the text in the row's face; `gap` the width of a space. The cut keeps the match
// whole where it can: it drops whole words from the longer side (the match stays about a third in), then the match
// itself from its end when it alone is wider than `room`. An ellipsis marks each side that lost text, or both when the
// search itself cut the excerpt (`truncated`).
template <class Width>
Pieces clipExcerpt(const std::string& excerpt, const size_t matchStart, const size_t matchLength, const bool truncated,
                   const int room, const int gap, Width width) {
  std::vector<size_t> at;
  for (size_t i = 0; i < excerpt.size();) {
    at.push_back(i);
    i += detail::codepointLength(static_cast<unsigned char>(excerpt[i]));
  }
  const size_t n = at.size();
  at.push_back(excerpt.size());
  const size_t ms = std::min(matchStart, n);
  const size_t matchEnd = std::min(ms + matchLength, n);
  size_t me = matchEnd, a = 0, b = n;
  const auto space = [&](const size_t k) { return excerpt[at[k]] == ' '; };
  for (;;) {
    auto pieces = detail::compose(excerpt, at, a, ms, me, b, a > 0 || truncated, b < n || truncated || me < matchEnd);
    if (detail::total(pieces, gap, width) <= room) return pieces;
    if (a < ms && (ms - a > (b - me) / 2 || b == me)) {
      size_t k = a + 1;
      while (k < ms && !space(k - 1)) ++k;
      a = k;
    } else if (b > me) {
      size_t k = b - 1;
      while (k > me && !space(k)) --k;
      b = k;
    } else if (me > ms + 1) {
      --me;
    } else {
      return pieces;  // one letter wider than the room: nothing more to take
    }
  }
}

}  // namespace booksearchlayout
