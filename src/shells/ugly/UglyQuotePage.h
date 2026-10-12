#pragma once
// The quotation page of tenor/ugly, as decisions only: how the quote breaks into lines and pages in the pen's own widths,
// where each part of the page stands, and which line of abuse a quote earns. Integers only and no hardware, so a host test
// runs it as it is. The drawing that uses it is QuoteDetailActivity.
#include <cstddef>
#include <cstdint>

namespace ugly::quotepage {

// Where the parts of the page stand, from the top of the area the page owns (the body rect of the screen).
inline constexpr int QUIP_BASE = 64;     // first baseline of the line of abuse (size 22)
inline constexpr int QUIP_PITCH = 30;    // two lines of it at most
inline constexpr int RULE_Y = 108;       // the shaky rule under the line of abuse
inline constexpr int MARK_BASE = 154;    // the opening quotation mark (size 38)
inline constexpr int BODY_BASE = 188;    // first baseline of the quote (size 30)
inline constexpr int ROW_PITCH = 40;     // baseline to baseline of the quote
inline constexpr int BODY_ASCENT = 28;   // size 30, as UglyInk's ASCENT
inline constexpr int BODY_DESCENT = 12;  // the tail of a g or a y, with the turned hand's lift
inline constexpr int COLUMN_X = 48;
inline constexpr int COLUMN_INSET = 80;  // the column is the screen width less this
// Air between the last line of the quote and the first fact under it, on top of the rows' own spacing.
inline constexpr int META_GAP = 22;
inline constexpr int MAX_PAGES = 32;

struct Span {
  uint16_t from = 0, to = 0;  // bytes of one line in the text, no blank at either end
};

// One line of `text` (n bytes) from byte `pos`: whole words one after another with `space` between them, a hard break at a
// newline, a word wider than `maxWidth` cut at a letter (so nothing is lost). measure(const char* bytes, int length) is the
// width of those bytes in the pen; visit(from, length, x) is called for every piece of a word as it is placed, x counted from
// the left of the column. Returns where the next line starts (n when the text is done); `out` is the line's bytes and
// `width` (when given) its width.
template <class Measure, class Visit>
int nextLine(const char* text, const int n, int pos, const int maxWidth, const int space, Measure measure, Visit visit,
             Span* out = nullptr, int* width = nullptr) {
  while (pos < n && text[pos] == ' ') ++pos;
  int x = 0, from = pos, to = pos;
  bool any = false;
  while (pos < n) {
    if (text[pos] == '\n') {
      ++pos;  // the break ends this line, empty when nothing came before it
      break;
    }
    int end = pos;
    while (end < n && text[end] != ' ' && text[end] != '\n') ++end;
    int w = measure(text + pos, end - pos);
    bool cutWord = false;
    if (!any && w > maxWidth) {
      // The longest run of letters that fits, at least one.
      int cut = pos;
      for (int at = pos; at < end;) {
        int next = at + 1;
        while (next < end && (static_cast<unsigned char>(text[next]) & 0xc0) == 0x80) ++next;
        if (at > pos && measure(text + pos, next - pos) > maxWidth) break;
        cut = next;
        at = next;
      }
      cutWord = end != cut;
      end = cut;
      w = measure(text + pos, end - pos);
    }
    const int gap = any ? space : 0;
    if (any && x + gap + w > maxWidth) break;  // the word starts the next line
    if (!any) from = pos;
    visit(pos, end - pos, x + gap);
    x += gap + w;
    to = end;
    any = true;
    pos = end;
    if (cutWord) break;  // the rest of the word starts the next line, with no blank before it
    while (pos < n && text[pos] == ' ') ++pos;
  }
  if (out) *out = {static_cast<uint16_t>(from), static_cast<uint16_t>(to)};
  if (width) *width = x;
  return pos;
}

// The pages of a quote: the byte where each starts, every page `capacity` lines but the last.
struct PageMap {
  uint16_t start[MAX_PAGES + 1] = {};
  int pages = 1;
  int lines = 0;      // in all
  int lastLines = 0;  // on the last page
};

template <class Measure>
PageMap paginate(const char* text, const int n, const int maxWidth, const int space, const int capacity, Measure measure) {
  PageMap map;
  const int per = capacity < 1 ? 1 : capacity;
  int pos = 0, onPage = 0;
  map.pages = 0;
  while (true) {
    if (onPage == 0) {
      if (map.pages == MAX_PAGES) break;  // a quote this long is cut here (1024 bytes never get so far)
      map.start[map.pages++] = static_cast<uint16_t>(pos);
    }
    pos = nextLine(text, n, pos, maxWidth, space, measure, [](int, int, int) {});
    ++map.lines;
    ++onPage;
    if (pos >= n) break;
    if (onPage == per) onPage = 0;
  }
  if (map.pages == 0) map.pages = 1;
  map.start[map.pages] = static_cast<uint16_t>(pos);
  map.lastLines = onPage;
  return map;
}

// Rows of the quote that fit between the first baseline and `areaBottom` (screen rows), a tail of ink included.
inline int capacityFor(const int areaTop, const int areaBottom) {
  const int room = areaBottom - BODY_DESCENT - (areaTop + BODY_BASE);
  return room < 0 ? 1 : room / ROW_PITCH + 1;
}

// The 8 lines of abuse a quote can earn; the same quote always gets the same one.
inline constexpr int QUIPS = 8;
inline int quipIndex(const uint64_t id) {
  uint64_t h = id + 0x9e3779b97f4a7c15ull;
  h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ull;
  h = (h ^ (h >> 27)) * 0x94d049bb133111ebull;
  h ^= h >> 31;
  return static_cast<int>(h % QUIPS);
}

}  // namespace ugly::quotepage
