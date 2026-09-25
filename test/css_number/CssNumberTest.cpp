// CSS lengths parse to the float std::from_chars gives, now without libstdc++'s float parser (21 KB
// of flash). The expected values come from exact rational arithmetic (gen_vectors.py): the nearest
// float, ties to even, an error past the largest float or for a nonzero value that rounds to zero.
// Some 28.000 cases: CSS-like numbers, float midpoints and their neighbours, the ends of the float
// range, and strings longer than the digits the parser keeps.
#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "CssParser.h"

namespace {
struct Case {
  const char* text;
  bool ok;
  uint32_t bits;
};
const Case CASES[] = {
#include "CssNumberVectors.inc"
};

// A length the way a book gives one: an image height in an inline style.
bool height(const std::string& value, CssLength& out) {
  const CssStyle style = CssParser::parseInlineStyle("height: " + value);
  if (!style.defined.imageHeight) return false;
  out = style.imageHeight;
  return true;
}

uint32_t bitsOf(const float f) {
  uint32_t b;
  std::memcpy(&b, &f, sizeof(b));
  return b;
}
}  // namespace

TEST(CssNumber, LengthsAreTheNearestFloat) {
  size_t wrong = 0;
  for (const auto& c : CASES) {
    CssLength length;
    const bool ok = height(std::string(c.text) + "em", length);
    const bool right = ok == c.ok && (!ok || (bitsOf(length.value) == c.bits && length.unit == CssUnit::Em));
    if (!right && ++wrong <= 10)
      ADD_FAILURE() << c.text << ": ok " << ok << " bits " << std::hex << bitsOf(length.value) << ", want ok " << c.ok
                    << " bits " << c.bits;
  }
  EXPECT_EQ(wrong, 0u) << "of " << sizeof(CASES) / sizeof(CASES[0]);
}

TEST(CssNumber, OnlyWholeNumbersParse) {
  for (const char* text : {"", ".", "-", "--1", "1-", "1.2.3", "-.", "+-", "++1", "1+", ".-1"}) {
    CssLength length;
    EXPECT_FALSE(height(std::string(text) + "px", length)) << text;
  }
  CssLength length;
  ASSERT_TRUE(height("+1.5em", length));
  EXPECT_EQ(length.value, 1.5f);
  ASSERT_TRUE(height("+-2%", length));
  EXPECT_EQ(length.value, -2.0f);
  EXPECT_EQ(length.unit, CssUnit::Percent);
}
