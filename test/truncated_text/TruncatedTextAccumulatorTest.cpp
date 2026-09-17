#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "EpdFontFamily.h"
#include "Utf8.h"
#include "builtinFonts/geist_12_bold.h"
#include "builtinFonts/geist_12_regular.h"

namespace {

const char kEllipsis[] = "\xE2\x80\xA6";

struct GeistFonts {
  EpdFont regular{&geist_12_regular};
  EpdFont bold{&geist_12_bold};
  EpdFontFamily family{&regular, &bold};
};

const EpdFontFamily& geist() {
  static const GeistFonts fonts;
  return fonts.family;
}

int referenceWidth(const EpdFontFamily& family, const std::string& text, const EpdFontFamily::Style style) {
  int width = 0;
  int height = 0;
  family.getTextDimensions(text.c_str(), &width, &height, style);
  return width;
}

int incrementalWidth(const EpdFontFamily& family, const std::string& text, const EpdFontFamily::Style style) {
  auto state = family.beginTextBounds();
  const char* cursor = text.c_str();
  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&cursor)))) {
    family.appendTextBounds(state, cp, cursor, style);
  }
  return family.textBoundsWidth(state);
}

std::string referenceTruncate(const EpdFontFamily& family, const std::string& text, const int maxWidth,
                              const EpdFontFamily::Style style) {
  if (maxWidth <= 0) return "";
  if (referenceWidth(family, text, style) <= maxWidth) return text;

  std::string item = text;
  while (!item.empty() && referenceWidth(family, item + kEllipsis, style) >= maxWidth) {
    utf8RemoveLastChar(item);
  }
  return item.empty() ? kEllipsis : item + kEllipsis;
}

std::string incrementalTruncate(const EpdFontFamily& family, const std::string& text, const int maxWidth,
                                const EpdFontFamily::Style style) {
  if (maxWidth <= 0) return "";
  if (incrementalWidth(family, text, style) <= maxWidth) return text;

  auto state = family.beginTextBounds();
  const char* cursor = text.c_str();
  size_t bestBytes = 0;
  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&cursor)))) {
    family.appendTextBounds(state, cp, cursor, style);

    auto candidate = state;
    const char* ellipsisCursor = kEllipsis + 3;
    family.appendTextBounds(candidate, 0x2026, ellipsisCursor, style);
    if (family.textBoundsWidth(candidate) < maxWidth) {
      bestBytes = static_cast<size_t>(cursor - text.c_str());
    }
  }

  return text.substr(0, bestBytes) + kEllipsis;
}

std::string nfcVietnamese() {
  return std::string("S") + "\xC3\xA1" + "ch ti" + "\xE1\xBA\xbf" + "ng Vi" + "\xE1\xbb\x87" +
         "t rat dai";
}

std::string nfdVietnamese() {
  return std::string("Sa") + "\xCC\x81" + "ch tie" + "\xCC\x82" + "ng Vie" + "\xCC\xA3" +
         "t rat dai";
}

std::string longRecentTitle() {
  std::string title;
  for (int i = 0; i < 14; ++i) title += "Sach ten rat dai ";
  return title;
}

}  // namespace

TEST(TruncatedTextAccumulator, MatchesFullBoundsForUnicodeAndKerning) {
  const std::vector<std::string> fixtures = {
      "",
      "AVATAR WAVE To Yo",
      "Sach ten rat dai Sach ten rat dai ",
      nfcVietnamese(),
      nfdVietnamese(),
      longRecentTitle(),
  };

  for (const auto& text : fixtures) {
    for (const auto style : {EpdFontFamily::REGULAR, EpdFontFamily::BOLD}) {
      EXPECT_EQ(incrementalWidth(geist(), text, style), referenceWidth(geist(), text, style))
          << "style=" << static_cast<int>(style) << " bytes=" << text.size();
    }
  }
}

TEST(TruncatedTextAccumulator, MatchesReferenceForEveryWidth) {
  const std::vector<std::string> fixtures = {
      "AVATAR WAVE To Yo",
      nfcVietnamese(),
      nfdVietnamese(),
      longRecentTitle(),
  };

  for (const auto& text : fixtures) {
    for (const auto style : {EpdFontFamily::REGULAR, EpdFontFamily::BOLD}) {
      const int fullWidth = referenceWidth(geist(), text, style);
      for (int maxWidth = 1; maxWidth <= fullWidth + 2; ++maxWidth) {
        EXPECT_EQ(incrementalTruncate(geist(), text, maxWidth, style),
                  referenceTruncate(geist(), text, maxWidth, style))
            << "style=" << static_cast<int>(style) << " maxWidth=" << maxWidth << " bytes=" << text.size();
      }
    }
  }
}

TEST(TruncatedTextAccumulator, HandlesEmptyAndMinimumWidths) {
  EXPECT_EQ(incrementalTruncate(geist(), "", 1, EpdFontFamily::REGULAR), "");
  EXPECT_EQ(referenceTruncate(geist(), "", 1, EpdFontFamily::REGULAR), "");

  const std::string text = "Sach ten rat dai";
  EXPECT_EQ(incrementalTruncate(geist(), text, 1, EpdFontFamily::REGULAR),
            referenceTruncate(geist(), text, 1, EpdFontFamily::REGULAR));
}

TEST(TruncatedTextAccumulator, ExactCandidateWidthKeepsReferenceStrictness) {
  const std::string text = "AVATAR WAVE To Yo";
  std::string prefix = "AVATAR";
  const int exactCandidateWidth = referenceWidth(geist(), prefix + kEllipsis, EpdFontFamily::REGULAR);

  EXPECT_EQ(incrementalTruncate(geist(), text, exactCandidateWidth, EpdFontFamily::REGULAR),
            referenceTruncate(geist(), text, exactCandidateWidth, EpdFontFamily::REGULAR));
}

TEST(TruncatedTextAccumulator, SharedStateStillAppliesActualGeistLigatures) {
  for (const std::string text : {"fi", "fl", "office", "fifi"}) {
    EXPECT_EQ(incrementalWidth(geist(), text, EpdFontFamily::REGULAR), referenceWidth(geist(), text,
                                                                                         EpdFontFamily::REGULAR))
        << text;
  }
}

