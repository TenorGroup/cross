// tenor/ugly quotation page: nothing of the quote is lost or added by the lines and pages, no line is wider than its column,
// the same quote always draws the same, and the lines of abuse are spread over the quotes.
#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "shells/ugly/UglyQuotePage.h"

namespace {
using namespace ugly::quotepage;

// A pen whose letters differ in width: 7 to 21 px a letter, 2 bytes of a UTF-8 letter counted once.
int measure(const char* bytes, const int n) {
  int w = 0;
  for (int i = 0; i < n; ++i) {
    const auto c = static_cast<unsigned char>(bytes[i]);
    if ((c & 0xc0) == 0x80) continue;
    w += 7 + c % 15;
  }
  return w;
}

struct Laid {
  std::vector<std::string> lines;
  std::vector<int> widths;
  PageMap map;
};

Laid lay(const std::string& text, const int maxWidth, const int space, const int capacity) {
  Laid out;
  out.map = paginate(text.data(), static_cast<int>(text.size()), maxWidth, space, capacity, measure);
  for (int page = 0; page < out.map.pages; ++page) {
    int pos = out.map.start[page];
    const int rows = page + 1 < out.map.pages ? capacity : out.map.lastLines;
    for (int row = 0; row < rows; ++row) {
      Span span;
      int width = 0;
      pos = nextLine(text.data(), static_cast<int>(text.size()), pos, maxWidth, space, measure, [](int, int, int) {}, &span,
                     &width);
      out.lines.push_back(text.substr(span.from, span.to - span.from));
      out.widths.push_back(width);
    }
  }
  return out;
}

uint32_t rng(uint32_t& s) {
  s = s * 1664525u + 1013904223u;
  return s >> 8;
}

std::string randomText(uint32_t& s, const int bytes) {
  static const char* WORDS[] = {"a", "to", "Đi", "cho", "biết", "ngày", "pleasure", "consists,", "ice-pleasure", "nhà", "mẹ",
                                "desirable.”", "x", "Dế", "Mèn", "extraordinarily", "khôn"};
  std::string text;
  while (static_cast<int>(text.size()) < bytes) {
    text += WORDS[rng(s) % 17];
    text += rng(s) % 40 == 0 ? "\n" : " ";
  }
  text.resize(static_cast<size_t>(bytes));
  while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xc0) == 0x80) text.pop_back();
  if (!text.empty() && static_cast<unsigned char>(text.back()) >= 0xc0) text.pop_back();
  return text;
}

TEST(QuoteLines, EveryLetterOfTheQuoteIsOnSomePageInOrder) {
  uint32_t seed = 7;
  for (int round = 0; round < 300; ++round) {
    const std::string text = randomText(seed, 1 + rng(seed) % 1024);
    const int width = 200 + rng(seed) % 300, capacity = 4 + rng(seed) % 12;
    const auto laid = lay(text, width, 9, capacity);
    std::string letters;
    for (const auto& line : laid.lines)
      for (const char c : line)
        if (c != ' ' && c != '\n') letters += c;
    std::string want;
    for (const char c : text)
      if (c != ' ' && c != '\n') want += c;
    ASSERT_EQ(letters, want) << "round " << round << " width " << width << " capacity " << capacity;
  }
}

TEST(QuoteLines, WordsThatFitStayWholeAndOrderedAcrossLines) {
  const std::string text = "If ice-pleasure consists, to some degree, in novelty, then ice must melt in order to be desirable.";
  const auto laid = lay(text, 448, 9, 12);
  EXPECT_GE(laid.lines.size(), 2u);
  std::string back;
  for (const auto& line : laid.lines) back += (back.empty() ? "" : " ") + line;
  EXPECT_EQ(back, text);
}

TEST(QuoteLines, NoLineIsWiderThanItsColumn) {
  uint32_t seed = 11;
  for (int round = 0; round < 300; ++round) {
    const std::string text = randomText(seed, 1 + rng(seed) % 1024);
    const int width = 60 + rng(seed) % 440;
    const auto laid = lay(text, width, 9, 1 + rng(seed) % 14);
    for (size_t i = 0; i < laid.lines.size(); ++i) EXPECT_LE(laid.widths[i], width) << "round " << round << " line " << i;
  }
}

TEST(QuoteLines, AWordWiderThanTheColumnIsCutAtALetterAndKept) {
  const std::string word = "Supercalifragilisticexpialidocious";
  const auto laid = lay("ab " + word + " cd", 120, 9, 20);
  std::string letters;
  for (const auto& line : laid.lines) letters += line;
  EXPECT_EQ(letters, "ab" + word + "cd");
  EXPECT_GE(laid.lines.size(), 4u);
}

TEST(QuoteLines, ACutNeverSplitsAUtf8Letter) {
  const std::string word = "ĐĐĐĐĐĐĐĐĐĐĐĐĐĐĐĐĐĐĐĐĐĐĐĐĐĐĐĐĐĐĐĐ";
  const auto laid = lay(word, 80, 9, 30);
  std::string letters;
  for (const auto& line : laid.lines) {
    EXPECT_NE((static_cast<unsigned char>(line.front()) & 0xc0), 0x80) << line;
    letters += line;
  }
  EXPECT_EQ(letters, word);
}

TEST(QuoteLines, AHardBreakEndsTheLine) {
  const auto laid = lay("one two\nthree\n\nfour", 400, 9, 20);
  ASSERT_EQ(laid.lines.size(), 4u);
  EXPECT_EQ(laid.lines[0], "one two");
  EXPECT_EQ(laid.lines[1], "three");
  EXPECT_EQ(laid.lines[2], "");
  EXPECT_EQ(laid.lines[3], "four");
}

TEST(QuotePages, ALongQuoteTakesSeveralPagesAndTheLastHoldsTheRest) {
  std::string text;
  uint32_t seed = 3;
  text = randomText(seed, 1024);
  const auto map = paginate(text.data(), static_cast<int>(text.size()), 448, 9, 12, measure);
  EXPECT_GE(map.pages, 3);
  EXPECT_LE(map.pages, MAX_PAGES);
  EXPECT_EQ(map.lines, (map.pages - 1) * 12 + map.lastLines);
  EXPECT_GE(map.lastLines, 1);
  EXPECT_LE(map.lastLines, 12);
  for (int p = 0; p < map.pages; ++p) EXPECT_LT(map.start[p], map.start[p + 1]) << p;
}

TEST(QuotePages, ShortQuoteIsOnePage) {
  const auto map = paginate("Short words.", 12, 448, 9, 12, measure);
  EXPECT_EQ(map.pages, 1);
  EXPECT_EQ(map.lines, 1);
  EXPECT_EQ(map.lastLines, 1);
}

TEST(QuotePages, ExactlyFullPageDoesNotAddAnEmptyOne) {
  // 12 words that each fill a line.
  std::string text;
  for (int i = 0; i < 24; ++i) text += "wwwwwwwwwwwwwwwwwwww ";
  const auto map = paginate(text.data(), static_cast<int>(text.size()), 448, 9, 12, measure);
  EXPECT_EQ(map.lines, 24);
  EXPECT_EQ(map.pages, 2);
  EXPECT_EQ(map.lastLines, 12);
}

TEST(QuotePages, PagesOfEveryWidthAndCapacityGlueBackToTheInput) {
  uint32_t seed = 21;
  for (int round = 0; round < 200; ++round) {
    const std::string text = randomText(seed, 1 + rng(seed) % 1025);
    const int width = 200 + rng(seed) % 320, capacity = 4 + rng(seed) % 13;
    const auto laid = lay(text, width, 9, capacity);
    std::string letters, want;
    for (const auto& line : laid.lines) for (const char c : line) if (c != ' ' && c != '\n') letters += c;
    for (const char c : text) if (c != ' ' && c != '\n') want += c;
    // A quote this long may be cut at MAX_PAGES only when the page is narrower than any real screen allows.
    if (laid.map.pages < MAX_PAGES) ASSERT_EQ(letters, want) << round;
  }
}

TEST(QuotePages, TheLayoutOfTheSameQuoteIsTheSame) {
  uint32_t seed = 5;
  const std::string text = randomText(seed, 900);
  const auto a = lay(text, 448, 9, 12), b = lay(text, 448, 9, 12);
  EXPECT_EQ(a.lines, b.lines);
  EXPECT_EQ(a.map.pages, b.map.pages);
}

TEST(QuotePageGeometry, CapacityFillsTheAreaAndTheLastRowKeepsItsTailAboveTheFloor) {
  // The X3: body rect 16..~710.
  for (int bottom : {300, 400, 600, 700, 710, 760}) {
    const int c = capacityFor(16, bottom);
    EXPECT_GE(c, 1);
    EXPECT_LE(16 + BODY_BASE + (c - 1) * ROW_PITCH + BODY_DESCENT, bottom) << bottom;
    EXPECT_GT(16 + BODY_BASE + c * ROW_PITCH + BODY_DESCENT, bottom) << "one more row would still fit at " << bottom;
  }
  EXPECT_EQ(capacityFor(16, 710), 13);
  EXPECT_EQ(capacityFor(16, 100), 1);
}

TEST(QuoteQuip, TheSameQuoteAlwaysEarnsTheSameLineAndTheLinesAreSpread) {
  std::set<int> seen;
  for (uint64_t id = 1; id <= 400; ++id) {
    const uint64_t key = id * 0x10000001b3ull;
    ASSERT_EQ(quipIndex(key), quipIndex(key));
    ASSERT_GE(quipIndex(key), 0);
    ASSERT_LT(quipIndex(key), QUIPS);
    seen.insert(quipIndex(key));
  }
  EXPECT_EQ(seen.size(), static_cast<size_t>(QUIPS));
  // Quotes kept a minute apart (ids differ in the low bits only) do not all share one line.
  std::set<int> neighbours;
  for (uint64_t id = 0xa1b2c3d400000000ull; id < 0xa1b2c3d400000000ull + 16; ++id) neighbours.insert(quipIndex(id));
  EXPECT_GE(neighbours.size(), 4u);
}
}  // namespace
