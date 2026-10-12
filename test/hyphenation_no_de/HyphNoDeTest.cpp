#include <Epub/ParsedText.h>
#include <Epub/blocks/TextBlock.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <Epub/hyphenation/LanguageRegistry.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

#ifdef TENOR_HYPH_NO_DE
constexpr bool kHasGerman = false;
#else
constexpr bool kHasGerman = true;
#endif

// Stub renderer: every glyph is 8 px wide and a space is 4 px.
std::vector<std::vector<std::string>> layout(const std::vector<const char*>& words, const uint16_t width) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Justify;
  style.textIndentDefined = true;
  ParsedText text(false, true, false, style);
  for (const char* word : words) text.addWord(word, EpdFontFamily::REGULAR);
  std::vector<std::vector<std::string>> lines;
  text.layoutAndExtractLines(renderer, 0, width, [&](std::unique_ptr<TextBlock> block, auto) {
    auto& line = lines.emplace_back();
    for (uint16_t i = 0; i < block->wordCount(); ++i) line.emplace_back(block->wordText(i));
  });
  return lines;
}

}  // namespace

TEST(HyphGerman, RegistryListsGermanOnlyWhenBuiltIn) {
  size_t german = 0;
  for (const auto& entry : getLanguageEntries()) {
    if (std::string(entry.primaryTag) == "de" || std::string(entry.cliName) == "german") ++german;
  }
  EXPECT_EQ(german, kHasGerman ? 1u : 0u);
  EXPECT_EQ(getLanguageEntries().size, kHasGerman ? 11u : 10u);
  EXPECT_EQ(getLanguageHyphenatorForPrimaryTag("de") != nullptr, kHasGerman);
  EXPECT_NE(getLanguageHyphenatorForPrimaryTag("en"), nullptr);
  EXPECT_NE(getLanguageHyphenatorForPrimaryTag("fr"), nullptr);
}

TEST(HyphGerman, EveryGermanTagReachesTheSameAnswer) {
  for (const char* tag : {"de", "de-AT", "DE", "deu", "ger"}) {
    Hyphenator::setPreferredLanguage(tag);
    const auto breaks = Hyphenator::breakOffsets("Quadratkilometer", /*includeFallback=*/false);
    EXPECT_EQ(!breaks.empty(), kHasGerman) << tag;
  }
}

TEST(HyphGerman, LayoutKeepsAGermanWordWholeWithoutTheHyphenator) {
  Hyphenator::setPreferredLanguage("de");
  // Dies + space leaves 104 px on a 140 px line; Quadratkilometer is 128 px.
  const auto lines = layout({"Dies", "Quadratkilometer"}, 140);
  ASSERT_EQ(lines.size(), 2u);
  if (kHasGerman) {
    EXPECT_EQ(lines[0].size(), 2u);  // a hyphenated head of the word shares the first line
  } else {
    EXPECT_EQ(lines[0], (std::vector<std::string>{"Dies"}));
    EXPECT_EQ(lines[1], (std::vector<std::string>{"Quadratkilometer"}));
  }
}

TEST(HyphGerman, OtherLanguagesAndExplicitHyphensStillBreak) {
  Hyphenator::setPreferredLanguage("en");
  EXPECT_FALSE(Hyphenator::breakOffsets("extraordinary", false).empty());
  Hyphenator::setPreferredLanguage("de");
  // A hyphen already in the text is a break whatever the language.
  const auto explicitBreaks = Hyphenator::breakOffsets("US-Satellitensystems", false);
  ASSERT_FALSE(explicitBreaks.empty());
  EXPECT_EQ(explicitBreaks.front().byteOffset, 3u);
  EXPECT_FALSE(explicitBreaks.front().requiresInsertedHyphen);
}

TEST(HyphGerman, AnOversizedGermanWordStillWrapsWithoutHanging) {
  Hyphenator::setPreferredLanguage("de");
  // 16 glyphs = 128 px on a 64 px line: the fallback split keeps the word from overflowing.
  const auto lines = layout({"Quadratkilometer"}, 64);
  EXPECT_GE(lines.size(), 2u);
}
