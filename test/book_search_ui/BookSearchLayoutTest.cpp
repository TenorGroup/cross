#include <gtest/gtest.h>

#include <string>

#include "BookSearchLayout.h"

namespace {
using booksearchlayout::Pieces;

// A fixed pitch face: 8 px a letter, 11 px for a bold one, a space 4 px.
size_t letters(const std::string& s) {
  size_t n = 0;
  for (const unsigned char c : s) n += (c & 0xC0) != 0x80;
  return n;
}
int width(const std::string& text, const bool bold) { return static_cast<int>(letters(text)) * (bold ? 11 : 8); }

Pieces clip(const std::string& excerpt, const size_t start, const size_t length, const int room,
            const bool truncated = false) {
  return booksearchlayout::clipExcerpt(excerpt, start, length, truncated, room, 4, width);
}

int total(const Pieces& pieces) {
  int sum = 0;
  for (size_t i = 0; i < pieces.size(); ++i) sum += width(pieces[i].text, pieces[i].bold) + (pieces[i].gapAfter && i + 1 < pieces.size() ? 4 : 0);
  return sum;
}

std::string bolded(const Pieces& pieces) {
  std::string out;
  for (const auto& piece : pieces)
    if (piece.bold) out += piece.text;
  return out;
}

std::string text(const Pieces& pieces) {
  std::string out;
  for (size_t i = 0; i < pieces.size(); ++i) out += pieces[i].text + (pieces[i].gapAfter && i + 1 < pieces.size() ? " " : "");
  return out;
}

// Every piece cuts on a letter boundary of the original UTF-8.
bool wellFormed(const Pieces& pieces) {
  for (const auto& piece : pieces) {
    for (size_t i = 0; i < piece.text.size();) {
      const unsigned char lead = piece.text[i];
      const size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
      if (i + length > piece.text.size()) return false;
      for (size_t k = 1; k < length; ++k)
        if ((static_cast<unsigned char>(piece.text[i + k]) & 0xC0) != 0x80) return false;
      i += length;
    }
  }
  return true;
}
}  // namespace

TEST(BookSearchLayout, WholeExcerptWhenItFits) {
  const auto pieces = clip("tranh cận đại là người mà hầu hết", 17, 5, 1000);
  EXPECT_EQ(text(pieces), "tranh cận đại là người mà hầu hết");
  EXPECT_EQ(bolded(pieces), "người");
  for (const auto& piece : pieces) EXPECT_NE(piece.text, "\xE2\x80\xA6");
}

TEST(BookSearchLayout, CutsToTheRoomAndKeepsTheMatchBold) {
  const std::string excerpt = "tranh cận đại là người mà hầu hết chúng ta đều biết";
  for (int room = 90; room < 400; room += 7) {
    const auto pieces = clip(excerpt, 17, 5, room);
    EXPECT_LE(total(pieces), room) << room;
    EXPECT_EQ(bolded(pieces), "người") << room;
    EXPECT_TRUE(wellFormed(pieces)) << room;
  }
}

TEST(BookSearchLayout, EllipsisMarksEachSideThatLostText) {
  const std::string excerpt = "tranh cận đại là người mà hầu hết chúng ta đều biết";
  const auto pieces = clip(excerpt, 17, 5, 150);
  ASSERT_GE(pieces.size(), 3u);
  EXPECT_EQ(pieces.front().text, "\xE2\x80\xA6");
  EXPECT_EQ(pieces.back().text, "\xE2\x80\xA6");
}

TEST(BookSearchLayout, NoEllipsisWhenNothingWasCutAndTheSearchKeptAll) {
  const auto pieces = clip("một người", 4, 5, 1000);
  EXPECT_EQ(text(pieces), "một người");
}

TEST(BookSearchLayout, TheSearchsOwnCutShowsBothEllipses) {
  const auto pieces = clip("một người", 4, 5, 1000, true);
  EXPECT_EQ(pieces.front().text, "\xE2\x80\xA6");
  EXPECT_EQ(pieces.back().text, "\xE2\x80\xA6");
}

TEST(BookSearchLayout, MatchWiderThanTheRoomIsCutFromItsEnd) {
  const auto pieces = clip("xin chào cả thế giới", 4, 16, 70);
  EXPECT_LE(total(pieces), 70);
  EXPECT_FALSE(bolded(pieces).empty());
  EXPECT_EQ(bolded(pieces).rfind("chào", 0), 0u);
  EXPECT_TRUE(wellFormed(pieces));
}

TEST(BookSearchLayout, MatchInsideAWordKeepsItsNeighbourLetters) {
  // "ma" inside "mà": no space between the pieces.
  const auto pieces = clip("tôi mà bạn", 4, 1, 1000);
  EXPECT_EQ(text(pieces), "tôi mà bạn");
  EXPECT_EQ(bolded(pieces), "m");
}

TEST(BookSearchLayout, EmptyRoomStillReturnsTheMatch) {
  const auto pieces = clip("một người", 4, 5, 0);
  EXPECT_FALSE(bolded(pieces).empty());
}
