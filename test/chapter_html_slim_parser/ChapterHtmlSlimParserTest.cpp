#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/ReaderSpacing.h>
#include <Epub/converters/ImageDimsProbe.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <new>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "src/ReaderFontSizes.h"
#include "src/clippings/ClippingText.h"

#define class struct
#define private public
#include "Epub/parsers/ChapterHtmlSlimParser.h"
#undef private
#undef class
#include "../../src/fontIds.h"

// Fault only explicit nothrow firmware allocations, leaving gtest and STL alone.
namespace parserAllocationFault {
size_t objectSize = 0;
bool array = false;
// Consecutive matching allocations to fail. An allocation the firmware retries after cache
// eviction (ParsedText, the word arena, the TextBlock arena) needs two to stay failed.
unsigned times = 1;
unsigned hits = 0;
void reset() { objectSize = 0; array = false; times = 1; hits = 0; }
}

void* operator new(size_t size, const std::nothrow_t&) noexcept {
  if (parserAllocationFault::objectSize == size) {
    if (--parserAllocationFault::times == 0) parserAllocationFault::objectSize = 0;
    ++parserAllocationFault::hits;
    return nullptr;
  }
  try { return ::operator new(size); } catch (...) { return nullptr; }
}
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  if (parserAllocationFault::array) {
    if (--parserAllocationFault::times == 0) parserAllocationFault::array = false;
    ++parserAllocationFault::hits;
    return nullptr;
  }
  try { return ::operator new[](size); } catch (...) { return nullptr; }
}


namespace {

void installBuiltinVariants(GfxRenderer& renderer) {
  renderer.setFontVariantProvider({&renderer, [](void* ctx, const int fontId, const float scale) {
                                     const int variant = builtinReaderFontVariant(fontId, scale);
                                     return static_cast<GfxRenderer*>(ctx)->getFontMap().count(variant) ? variant : 0;
                                   }});
}

void installSyntheticVariants(GfxRenderer& renderer) {
  renderer.setFontVariantProvider({&renderer,
                                   [](void* ctx, const int fontId, const float scale) {
                                     const int variant = fontId * 1000 + static_cast<int>(scale * 100 + 0.5f);
                                     static_cast<GfxRenderer*>(ctx)->fontMap.emplace(variant, EpdFontFamily(nullptr));
                                     return variant;
                                   },
                                   [](void* ctx, const int fontId) {
                                     static_cast<GfxRenderer*>(ctx)->fontMap.emplace(fontId, EpdFontFamily(nullptr));
                                     return true;
                                   }});
}

class ChapterHtmlSlimParserTest : public ::testing::TestWithParam<const char*> {
 protected:
  std::string filepath = "unused.xhtml";
  GfxRenderer renderer;
  CssParser cssParser{"/tmp"};
  ChapterHtmlSlimParser parser{nullptr,
                               filepath,
                               renderer,
                               0,
                               1.0f,
                               false,
                               0,
                               static_cast<uint16_t>(renderer.getScreenWidth()),
                               static_cast<uint16_t>(renderer.getScreenHeight()),
                               false,
                               false,
                               0,
                               {},
                               true,
                               "",
                               "",
                               0,
                               {},
                               nullptr,
                               &cssParser};

  void SetUp() override { parser.currentTextBlock = std::make_unique<ParsedText>(); }
  void TearDown() override { Epub::enableGifProbeForTest = false; }

  // Lays out a body fragment through the cached-page path and joins its words the way a
  // clipping does: '\n' at a paragraph start, ' ' across a source gap, nothing otherwise.
  std::string parseToParagraphText(const std::string& body) {
    const std::string html = "<html><body>" + body + "</body></html>";
    const std::string testId = std::to_string(std::hash<std::string>{}(html));
    const auto tempDir = std::filesystem::temp_directory_path();
    filepath = (tempDir / ("crosspoint-clipping-paragraphs-" + testId + ".xhtml")).string();
    {
      HalFile file;
      EXPECT_TRUE(file.open(filepath.c_str(), "wb"));
      EXPECT_EQ(file.write(html.data(), html.size()), html.size());
    }
    const auto cachePath = (tempDir / ("crosspoint-clipping-paragraphs-" + testId + ".bin")).string();
    std::string selected;
    uint32_t previousEnd = 0;
    bool paragraphStartPending = false;
    parser.completePageFn = [&](std::unique_ptr<Page> page, auto, auto, auto) {
      {
        HalFile file;
        ASSERT_TRUE(file.open(cachePath.c_str(), "wb"));
        ASSERT_TRUE(page->serialize(file));
      }
      HalFile file;
      ASSERT_TRUE(file.open(cachePath.c_str(), "rb"));
      auto cached = Page::deserialize(file);
      ASSERT_NE(cached, nullptr);
      for (const auto& element : cached->elements) {
        if (element->getTag() != TAG_PageLine) continue;
        const auto& block = *static_cast<const PageLine&>(*element).getBlock();
        for (uint16_t i = 0; i < block.wordCount(); ++i) {
          if (block.wordStartsParagraph(i)) paragraphStartPending = true;
          if (!clippingText::hasVisibleText(block.wordText(i))) continue;
          const auto range = block.wordSourceRange(i);
          const char separator = paragraphStartPending ? '\n' : range.start > previousEnd ? ' ' : '\0';
          ASSERT_TRUE(clippingText::append(selected, block.wordText(i), separator, 4096));
          paragraphStartPending = false;
          previousEnd = range.end;
        }
      }
      EXPECT_EQ(file.position(), file.size());
    };
    parser.paragraphIndentSpaces = 0;
    EXPECT_TRUE(parser.parseAndBuildPages());
    std::filesystem::remove(filepath);
    std::filesystem::remove(cachePath);
    return selected;
  }
};

TEST(ImageDimsProbe, ReadsBothGifSignaturesInFragmentedStreams) {
  for (const uint8_t version : {'7', '9'}) {
    const uint8_t header[] = {'G', 'I', 'F', '8', version, 'a', 0, 12, 2, 0};
    for (size_t split = 0; split <= sizeof(header); ++split) {
      ImageDimsProbe probe;
      probe.write(header, split);
      ImageDimensions dims{};
      EXPECT_EQ(probe.getDimensions(dims), split == sizeof(header));
      probe.write(header + split, sizeof(header) - split);
      ASSERT_TRUE(probe.getDimensions(dims));
      EXPECT_EQ(dims.width, 3072);
      EXPECT_EQ(dims.height, 2);
      EXPECT_EQ(probe.write(0), 0u);
    }
  }
}

TEST(ImageDimsProbe, RejectsInvalidGifHeadersAndDimensions) {
  const std::vector<std::vector<uint8_t>> headers = {
      {'G', 'I', 'F', '8', '8', 'a', 1, 0, 1, 0},   {'G', 'I', 'F', '8', '9', 'b', 1, 0, 1, 0},
      {'G', 'I', 'F', '8', '9', 'a', 0, 0, 1, 0},   {'G', 'I', 'F', '8', '9', 'a', 1, 0, 0, 0},
      {'G', 'I', 'F', '8', '9', 'a', 0, 128, 1, 0},
  };
  for (const auto& header : headers) {
    ImageDimsProbe probe;
    probe.write(header.data(), header.size());
    ImageDimensions dims{};
    EXPECT_FALSE(probe.getDimensions(dims));
  }
}

TEST(ImageDimsProbe, PreservesJpegAndPngDimensions) {
  const std::vector<std::vector<uint8_t>> headers = {
      {0xFF, 0xD8, 0xFF, 0xC0, 0, 17, 8, 0, 2, 0, 3},
      {0x89, 'P', 'N', 'G', 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 0, 3, 0, 0, 0, 2},
  };
  for (const auto& header : headers) {
    for (size_t split = 0; split <= header.size(); ++split) {
      ImageDimsProbe probe;
      probe.write(header.data(), split);
      probe.write(header.data() + split, header.size() - split);
      ImageDimensions dims{};
      ASSERT_TRUE(probe.getDimensions(dims));
      EXPECT_EQ(dims.width, 3);
      EXPECT_EQ(dims.height, 2);
    }
  }
}

TEST_F(ChapterHtmlSlimParserTest, StreamsGifDimensionsWithoutExtractingPayload) {
  parser.epub = std::make_shared<Epub>();
  parser.epub->imageBytes = {'G', 'I', 'F', '8', '9', 'a', 1, 0, 1, 0};
  parser.imageRendering = 0;
  const auto imagePath = std::filesystem::temp_directory_path() / "crosspoint-gif-probe-0.gif";
  std::filesystem::remove(imagePath);
  parser.imageBasePath = imagePath.string().substr(0, imagePath.string().size() - 5);
  Epub::enableGifProbeForTest = true;
  const char* attrs[] = {"src", "probe.gif", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "body", nullptr);
  ChapterHtmlSlimParser::startElement(&parser, "img", attrs);
  EXPECT_EQ(parser.epub->headerProbes, 1u);
  EXPECT_EQ(parser.epub->fullExtractions, 0u);
  EXPECT_FALSE(std::filesystem::exists(imagePath));
  std::filesystem::remove(imagePath);
}

TEST_F(ChapterHtmlSlimParserTest, AbandonedLayoutStopsExtractingLines) {
  ParsedText text;
  for (int i = 0; i < 100; ++i) text.addWord("word", EpdFontFamily::REGULAR);
  bool stop = false;
  size_t lines = 0;
  text.layoutAndExtractLines(
      renderer, 0, 80,
      [&](std::unique_ptr<TextBlock>, uint32_t) {
        ++lines;
        stop = true;
      },
      true, 0, 100, SIZE_MAX, &stop);
  EXPECT_EQ(lines, 1u);
}

class ParserStopTest : public ChapterHtmlSlimParserTest {};

TEST_P(ParserStopTest, PageCallbackStopsParsingAndClosesFile) {
  filepath = (std::filesystem::temp_directory_path() / "crosspoint-parser-stop.xhtml").string();
  std::string html = "<html><body>";
  for (int i = 0; i < 100; ++i) html += GetParam();
  html += "</body></html>";
  {
    HalFile file;
    ASSERT_TRUE(file.open(filepath.c_str(), "wb"));
    ASSERT_EQ(file.write(html.data(), html.size()), html.size());
  }
  renderer.sdFont = true;
  parser.viewportWidth = 160;
  parser.viewportHeight = 32;
  unsigned pages = 0;
  size_t fontPreparationsAtStop = 0;
  parser.completePageFn = [&](std::unique_ptr<Page> page, auto, auto, auto) {
    ASSERT_NE(page, nullptr);
    ++pages;
    fontPreparationsAtStop = renderer.fontPreparations;
    parser.requestStop();
  };
  EXPECT_FALSE(parser.parseAndBuildPages());
  EXPECT_EQ(pages, 1u);
  EXPECT_EQ(renderer.fontPreparations, fontPreparationsAtStop);
  EXPECT_EQ(parser.xmlParser_, nullptr);
  EXPECT_FALSE(parser.parseFile_.isOpen());
  std::filesystem::remove(filepath);
}

INSTANTIATE_TEST_SUITE_P(
    FlowBoundaries, ParserStopTest,
    ::testing::Values("<p>one two three four five six seven eight</p>", "one two three four five six seven eight ",
                      "<p>one two three four five six</p><hr/>",
                      "<table><tr><td>one two three four</td><td>five six seven eight</td></tr></table>"));

TEST_F(ChapterHtmlSlimParserTest, StopDuringFinalFlushDoesNotEmitAnotherPage) {
  parser.viewportWidth = 80;
  parser.viewportHeight = 32;
  for (int i = 0; i < 100; ++i) parser.currentTextBlock->addWord("word", EpdFontFamily::REGULAR);
  unsigned pages = 0;
  parser.completePageFn = [&](std::unique_ptr<Page> page, auto, auto, auto) {
    ASSERT_NE(page, nullptr);
    ++pages;
    parser.requestStop();
  };
  EXPECT_FALSE(parser.finishParse());
  EXPECT_EQ(pages, 1u);
}

TEST_F(ChapterHtmlSlimParserTest, RubySurvivesPartialParagraphExtraction) {
  ParsedText text;
  text.addWord("a", EpdFontFamily::REGULAR);
  text.addWord("b", EpdFontFamily::REGULAR);
  text.addWord("c", EpdFontFamily::REGULAR);
  text.setRubyForWordAt(2, "c");
  size_t lines = 0;
  text.layoutAndExtractLines(
      renderer, 0, 20,
      [&](std::unique_ptr<TextBlock> line, auto) {
        ++lines;
        EXPECT_EQ(line->getRubyCount(), 0u);
      },
      false);
  EXPECT_EQ(lines, 1u);
  const size_t retainedWords = text.size();
  ASSERT_GT(retainedWords, 0u);
  ASSERT_LT(retainedWords, 3u);
  text.layoutAndExtractLines(renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, auto) {
    ++lines;
    ASSERT_EQ(line->getRubyCount(), 1u);
    EXPECT_EQ(line->getRubyText(retainedWords - 1), "c");
    for (size_t i = 0; i + 1 < retainedWords; ++i) EXPECT_TRUE(line->getRubyText(i).empty());
  });
  EXPECT_EQ(lines, 2u);
}

TEST_F(ChapterHtmlSlimParserTest, UnequalTableCellsAndRubySurvivePageBreaks) {
  parser.viewportWidth = 240;
  parser.viewportHeight = 32;
  parser.tableRowCells.reserve(2);
  std::multiset<std::string> expected;
  for (int column = 0; column < 2; ++column) {
    auto cell = std::make_unique<ParsedText>();
    for (int index = 0; index < (column == 0 ? 30 : 3); ++index) {
      const auto word = std::string(column == 0 ? "left" : "right") + std::to_string(index);
      expected.insert(word);
      cell->addWord(word, EpdFontFamily::REGULAR);
    }
    if (column == 0) cell->setRubyGroupAt(0, 2, "reading");
    parser.tableRowCells.push_back(std::move(cell));
  }
  std::multiset<std::string> actual;
  unsigned pages = 0;
  unsigned rubyLines = 0;
  auto inspect = [&](std::unique_ptr<Page> page, auto, auto, auto) {
    ++pages;
    for (const auto& element : page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const auto& line = static_cast<const PageLine&>(*element);
      const auto& block = *line.getBlock();
      ASSERT_TRUE(block.valid());
      EXPECT_LE(element->yPos + 16 + block.getRubyShift(12), parser.viewportHeight);
      rubyLines += block.hasRuby();
      for (uint16_t word = 0; word < block.wordCount(); ++word) actual.insert(block.wordText(word));
    }
  };
  parser.completePageFn = inspect;
  parser.finishTableRow();
  ASSERT_NE(parser.currentPage, nullptr);
  inspect(std::move(parser.currentPage), 0, 0, 0);
  EXPECT_GT(pages, 2u);
  EXPECT_EQ(rubyLines, 1u);
  EXPECT_EQ(actual, expected);
  for (const auto& lines : parser.tableCellLines) EXPECT_TRUE(lines.empty());
}

TEST(ReaderFontSizes, BuiltinVariantsStayInFamilyAndRespectSizeBounds) {
  for (const auto& family : {
           std::array{NOTOSANS_12_FONT_ID, NOTOSANS_14_FONT_ID, NOTOSANS_16_FONT_ID, NOTOSANS_18_FONT_ID},
       }) {
    EXPECT_EQ(builtinReaderFontVariant(family[1], 1.0f), family[1]);
    EXPECT_EQ(builtinReaderFontVariant(family[1], 2.0f), family[3]);
    EXPECT_EQ(builtinReaderFontVariant(family[1], 1.17f), family[2]);
    EXPECT_EQ(builtinReaderFontVariant(family[1], 0.83f), family[0]);
    EXPECT_EQ(builtinReaderFontVariant(family[3], 0.9f), family[2]);
    EXPECT_EQ(builtinReaderFontVariant(family[0], 0.5f), family[0]);
    EXPECT_EQ(builtinReaderFontVariant(family[3], 3.0f), family[3]);
    EXPECT_EQ(builtinReaderFontVariant(family[0], 1.25f), family[1]);  // ties keep the smaller size
  }
  EXPECT_EQ(builtinReaderFontVariant(12345, 2.0f), 0);
}

TEST_F(ChapterHtmlSlimParserTest, BlockFontScaleUsesProviderAndFallsBackToBodyFont) {
  installBuiltinVariants(renderer);
  for (const int id : {NOTOSANS_12_FONT_ID, NOTOSANS_14_FONT_ID, NOTOSANS_16_FONT_ID, NOTOSANS_18_FONT_ID}) {
    renderer.fontMap.emplace(id, EpdFontFamily(nullptr));
  }
  parser.fontId = NOTOSANS_14_FONT_ID;
  EXPECT_EQ(parser.fontIdForScale(1.0f), NOTOSANS_14_FONT_ID);
  EXPECT_EQ(parser.fontIdForScale(2.0f), NOTOSANS_18_FONT_ID);
  EXPECT_EQ(parser.fontIdForScale(1.17f), NOTOSANS_16_FONT_ID);
  EXPECT_EQ(parser.fontIdForScale(0.83f), NOTOSANS_12_FONT_ID);
  EXPECT_EQ(parser.fontIdForScale(0.96f), NOTOSANS_14_FONT_ID);
  EXPECT_EQ(parser.fontIdForScale(1.04f), NOTOSANS_14_FONT_ID);
  parser.fontId = 54321;  // family not registered with the renderer
  EXPECT_EQ(parser.fontIdForScale(2.0f), 54321);
  parser.fontId = 12345;  // SD or vector font: never rescaled
  EXPECT_EQ(parser.fontIdForScale(2.0f), 12345);
}

TEST(FontVariantProvider, NearUnitScalesKeepBaseFontWithoutResolving) {
  int calls = 0;
  FontVariantProvider provider{&calls, [](void* ctx, int, float) {
                                 ++*static_cast<int*>(ctx);
                                 return 42;
                               }};
  for (const int font : {NOTOSANS_14_FONT_ID, NOTOSANS_16_FONT_ID, 7}) {
    for (const float scale : {0.96f, 0.99f, 1.0f, 1.04f}) EXPECT_EQ(provider.resolveFont(font, scale), font);
  }
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(provider.resolveFont(7, 0.94f), 42);
  EXPECT_EQ(calls, 1);
}

TEST_F(ChapterHtmlSlimParserTest, BuiltinFontSelectionDelegatesToInstalledProvider) {
  parser.fontId = NOTOSANS_14_FONT_ID;
  int calls = 0;
  renderer.setFontVariantProvider({&calls, [](void* ctx, const int fontId, const float scale) {
                                     ++*static_cast<int*>(ctx);
                                     EXPECT_EQ(fontId, NOTOSANS_14_FONT_ID);
                                     EXPECT_FLOAT_EQ(scale, 0.8f);
                                     return 42;
                                   }});
  EXPECT_EQ(parser.fontIdForScale(0.8f), 42);
  EXPECT_EQ(calls, 1);
}

TEST_F(ChapterHtmlSlimParserTest, MissingVariantProviderFallsBackToBodyFont) {
  parser.fontId = NOTOSANS_14_FONT_ID;
  EXPECT_EQ(renderer.resolveFontVariant(parser.fontId, 2.0f), 0);
  EXPECT_EQ(parser.fontIdForScale(2.0f), parser.fontId);
  EXPECT_EQ(parser.fontIdForScale(0.96f), parser.fontId);
}

TEST_F(ChapterHtmlSlimParserTest, BlockFontScaleInheritsAndUsesHeadingDefaults) {
  parser.blockStyleStack.assign(1, BlockStyle{});
  BlockStyle heading;
  parser.applyBlockFontScale(heading, CssStyle{}, "h2");
  EXPECT_FLOAT_EQ(heading.fontScale, 1.5f);

  CssStyle css;
  css.fontSize = CssLength{0.5f, CssUnit::Em};
  css.defined.fontSize = 1;
  parser.blockStyleStack.push_back(heading);
  BlockStyle child;
  parser.applyBlockFontScale(child, css, "p");
  EXPECT_FLOAT_EQ(child.fontScale, 0.75f);

  css.fontSize = CssLength{1.2f, CssUnit::Rem};
  parser.applyBlockFontScale(child, css, "p");
  EXPECT_FLOAT_EQ(child.fontScale, 1.2f);

  BlockStyle plain;
  const BlockStyle inherited = heading.getCombinedBlockStyle(plain, BlockStyle::CombineAxis::Horizontal);
  EXPECT_FLOAT_EQ(inherited.fontScale, 1.5f);
}

TEST_F(ChapterHtmlSlimParserTest, PageImageDeserializeRejectsMissingImageBlock) {
  const auto path = std::filesystem::temp_directory_path() / "crosspoint-missing-image-cache.bin";
  {
    HalFile output;
    ASSERT_TRUE(output.open(path.c_str(), "wb"));
    const int16_t coordinates[] = {0, 0};
    output.write(coordinates, sizeof(coordinates));
  }
  HalFile input;
  ASSERT_TRUE(input.open(path.c_str(), "rb"));
  EXPECT_EQ(PageImage::deserialize(input), nullptr);
}

TEST_P(ChapterHtmlSlimParserTest, KeepsCssVerticalAlignAndInternalLinkMetadata) {
  const char* verticalAlign = GetParam();
  const char* expectedHref = "#note-target";
  const XML_Char* attributes[] = {"href", expectedHref, "style", verticalAlign, nullptr};

  ChapterHtmlSlimParser::startElement(&parser, "a", attributes);
  const uint8_t linkId = parser.currentFootnoteLinkId;
  ASSERT_NE(linkId, 0u);
  ChapterHtmlSlimParser::characterData(&parser, "1", 1);
  ChapterHtmlSlimParser::endElement(&parser, "a");

  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  const auto style = parser.currentTextBlock->getWordStyleAt(0);
  const auto expectedStyle =
      std::string(verticalAlign).find("super") != std::string::npos ? EpdFontFamily::SUP : EpdFontFamily::SUB;
  EXPECT_NE(static_cast<uint8_t>(style) & static_cast<uint8_t>(expectedStyle), 0u);

  ASSERT_EQ(parser.pendingFootnotes.size(), 1u);
  const FootnoteEntry& footnote = parser.pendingFootnotes.front().second;
  EXPECT_STREQ(footnote.href, expectedHref);
  ASSERT_EQ(parser.currentTextBlock->wordLinkIds.size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->wordLinkIds.front(), linkId);
  EXPECT_TRUE(parser.currentTextBlock->linkTargetMatches(linkId, expectedHref));
}

INSTANTIATE_TEST_SUITE_P(CssVerticalAlign, ChapterHtmlSlimParserTest,
                         ::testing::Values("vertical-align: super", "vertical-align: sub"));

TEST_F(ChapterHtmlSlimParserTest, ParagraphWithHiddenAttributeShouldBeSkipped) {
  const XML_Char* attributes[] = {"hidden", "hidden", nullptr};

  parser.beginParse();
  ChapterHtmlSlimParser::startElement(&parser, "p", attributes);
  ChapterHtmlSlimParser::characterData(&parser, "[HIDDEN]", 8);

  ASSERT_EQ(parser.partWordBufferIndex, 0);
}

TEST_F(ChapterHtmlSlimParserTest, HeaderWithHiddenAttributeShouldBeSkipped) {
  const XML_Char* attributes[] = {"hidden", "hidden", nullptr};

  parser.beginParse();
  ChapterHtmlSlimParser::startElement(&parser, "h1", attributes);
  ChapterHtmlSlimParser::characterData(&parser, "[HIDDEN]", 8);

  ASSERT_EQ(parser.partWordBufferIndex, 0);
}

TEST_F(ChapterHtmlSlimParserTest, SpanWithHiddenAttributeShouldBeSkipped) {
  const XML_Char* attributes[] = {"hidden", "hidden", nullptr};

  parser.beginParse();
  ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
  ChapterHtmlSlimParser::characterData(&parser, "Before ", 7);
  ChapterHtmlSlimParser::startElement(&parser, "span", attributes);
  ChapterHtmlSlimParser::characterData(&parser, "[HIDDEN]", 8);
  ChapterHtmlSlimParser::endElement(&parser, "span");
  ChapterHtmlSlimParser::characterData(&parser, " After ", 7);

  ASSERT_EQ(parser.currentTextBlock->size(), 2);
  ASSERT_EQ(parser.currentTextBlock->wordAt(0), "Before");
  ASSERT_EQ(parser.currentTextBlock->wordAt(1), "After");
}

TEST_F(ChapterHtmlSlimParserTest, DivWithHiddenAttributeContentShouldBeSkipped) {
  const XML_Char* attributes[] = {"hidden", "hidden", nullptr};

  parser.beginParse();
  ChapterHtmlSlimParser::startElement(&parser, "div", attributes);
  ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
  ChapterHtmlSlimParser::characterData(&parser, "[HIDDEN]", 8);

  ASSERT_EQ(parser.partWordBufferIndex, 0);
}

TEST_F(ChapterHtmlSlimParserTest, SmallCapsSpanMarksWordsWithoutChangingText) {
  const XML_Char* attributes[] = {"style", "font-variant: small-caps", nullptr};
  parser.beginParse();
  ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
  ChapterHtmlSlimParser::characterData(&parser, "Plain ", 6);
  ChapterHtmlSlimParser::startElement(&parser, "span", attributes);
  ChapterHtmlSlimParser::characterData(&parser, "Chapter", 7);
  ChapterHtmlSlimParser::endElement(&parser, "span");
  ChapterHtmlSlimParser::characterData(&parser, " after", 6);
  ChapterHtmlSlimParser::endElement(&parser, "p");

  ASSERT_EQ(parser.currentTextBlock->size(), 0u);  // flushed to the page on </p>
  ASSERT_NE(parser.currentPage, nullptr);
  const auto& block = *static_cast<const PageLine&>(*parser.currentPage->elements.front()).getBlock();
  ASSERT_EQ(block.wordCount(), 3);
  EXPECT_STREQ(block.wordText(1), "Chapter");
  EXPECT_EQ(block.wordStyle(0) & EpdFontFamily::SMALL_CAPS, 0);
  EXPECT_NE(block.wordStyle(1) & EpdFontFamily::SMALL_CAPS, 0);
  EXPECT_FALSE(block.wordHasDiscretionaryHyphen(1));  // shares the style byte's high bit
  EXPECT_EQ(block.wordStyle(2) & EpdFontFamily::SMALL_CAPS, 0);
}

TEST_F(ChapterHtmlSlimParserTest, SmallCapsMetricsArePreparedForEachInlineFont) {
  renderer.sdFont = true;
  ParsedText text;
  const uint8_t slot = text.sizeSlotFor(0.8f);
  text.setSizeSlotFontId(slot, 42);
  text.addWord("Chapter", EpdFontFamily::SMALL_CAPS);
  text.addWord("other", static_cast<EpdFontFamily::Style>(EpdFontFamily::BOLD | EpdFontFamily::SMALL_CAPS), false,
               false, 8, 0, slot);
  size_t lines = 0;
  text.layoutAndExtractLines(renderer, 0, 480, [&](std::unique_ptr<TextBlock> line, auto) {
    ++lines;
    ASSERT_EQ(line->wordCount(), 2);
    EXPECT_STREQ(line->wordText(0), "Chapter");
    EXPECT_STREQ(line->wordText(1), "other");
    EXPECT_EQ(line->wordFontId(1, 0), 42);
    EXPECT_NE(line->wordStyle(1) & EpdFontFamily::SMALL_CAPS, 0);
  });
  EXPECT_EQ(lines, 1u);
  EXPECT_EQ(renderer.coldMeasurements, 0u);
}

class ChapterPageBreakTest : public ChapterHtmlSlimParserTest {
 protected:
  unsigned pages = 0;
  void SetUp() override {
    ChapterHtmlSlimParserTest::SetUp();
    parser.completePageFn = [this](std::unique_ptr<Page>, uint16_t, uint16_t, uint32_t) { ++pages; };
    parser.beginParse();
  }
  void paragraph(const char* text, const char* style = nullptr) {
    const XML_Char* attributes[] = {"style", style, nullptr};
    ChapterHtmlSlimParser::startElement(&parser, "p", style ? attributes : nullptr);
    ChapterHtmlSlimParser::characterData(&parser, text, static_cast<int>(strlen(text)));
    ChapterHtmlSlimParser::endElement(&parser, "p");
  }
};

TEST_F(ChapterPageBreakTest, BreakBeforeStartsNewPage) {
  paragraph("one");
  paragraph("two");
  EXPECT_EQ(pages, 0u);
  paragraph("three", "page-break-before: always");
  EXPECT_EQ(pages, 1u);
}

TEST_F(ChapterPageBreakTest, BreakAfterAppliesToFollowingContent) {
  paragraph("one", "break-after: page");
  EXPECT_EQ(pages, 0u);
  paragraph("two");
  EXPECT_EQ(pages, 1u);
}

TEST_F(ChapterPageBreakTest, AvoidAndEmptyPageDoNotBreak) {
  paragraph("first", "page-break-before: always");
  paragraph("second", "page-break-before: avoid");
  EXPECT_EQ(pages, 0u);
}

TEST_F(ChapterPageBreakTest, ContainerBreaksOnceBeforeItsFirstChild) {
  paragraph("zero");
  const XML_Char* attributes[] = {"style", "page-break-before: always", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "div", attributes);
  paragraph("a");
  paragraph("b");
  ChapterHtmlSlimParser::endElement(&parser, "div");
  paragraph("c");
  EXPECT_EQ(pages, 1u);
}

class ChapterBlockDecorationTest : public ChapterHtmlSlimParserTest {
 protected:
  std::vector<std::unique_ptr<Page>> pages;
  void SetUp() override {
    ChapterHtmlSlimParserTest::SetUp();
    parser.completePageFn = [this](std::unique_ptr<Page> page, uint16_t, uint16_t, uint32_t) {
      pages.push_back(std::move(page));
    };
    parser.beginParse();
  }
  void open(const char* tag, const char* style) {
    const XML_Char* attributes[] = {"style", style, nullptr};
    ChapterHtmlSlimParser::startElement(&parser, tag, style ? attributes : nullptr);
  }
  void text(const std::string& value) {
    ChapterHtmlSlimParser::characterData(&parser, value.c_str(), static_cast<int>(value.size()));
  }
  void close(const char* tag) { ChapterHtmlSlimParser::endElement(&parser, tag); }
  static std::string words(int count) {
    std::string result;
    for (int i = 0; i < count; ++i) result += "word ";
    return result;
  }
  std::vector<const PageElement*> elementsWithTag(PageElementTag tag) {
    std::vector<const PageElement*> found;
    if (parser.currentPage) pages.push_back(std::move(parser.currentPage));
    for (const auto& page : pages) {
      for (const auto& element : page->elements) {
        if (element->getTag() == tag) found.push_back(element.get());
      }
    }
    return found;
  }
};

TEST_F(ChapterBlockDecorationTest, RepeatedSectionBreaksEachAddOneLine) {
  for (int breaks = 1; breaks <= 3; ++breaks) {
    open("p", "margin-top: 1px");
    text("Before");
    close("p");
    const int before = parser.currentPageNextY;
    for (int i = 0; i < breaks; ++i) {
      const XML_Char* attrs[] = {"class", "section-br", "style", "display: block", nullptr};
      ChapterHtmlSlimParser::startElement(&parser, "br", attrs);
      close("br");
    }
    EXPECT_EQ(parser.currentPageNextY - before, breaks * renderer.getLineHeight(0));
    open("p", "margin-top: 1px");
    text("After");
    close("p");
  }
}

TEST_F(ChapterBlockDecorationTest, InlineBreakDoesNotAddParagraphSpacing) {
  parser.extraParagraphSpacing = true;
  open("p", "margin-top: 1px; margin-bottom: 9px");
  text("First");
  open("br", nullptr);
  close("br");
  text("Second");
  close("p");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[1]->yPos - lines[0]->yPos, renderer.getLineHeight(0));
}

TEST_F(ChapterBlockDecorationTest, LeadingBreakDoesNotAddBlankSpace) {
  open("br", nullptr);
  close("br");
  open("p", nullptr);
  text("Start");
  close("p");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0]->yPos, 0);
}

TEST_F(ChapterBlockDecorationTest, BreakAfterHeadingAddsOneLine) {
  open("h2", nullptr);
  text("Heading");
  close("h2");
  const int before = parser.currentPageNextY;
  open("br", nullptr);
  close("br");
  EXPECT_EQ(parser.currentPageNextY - before, renderer.getLineHeight(0));
}

TEST_F(ChapterBlockDecorationTest, PreWrapPreservesSpacesAndLineBreaksAcrossCallbacks) {
  parser.extraParagraphSpacing = true;
  open("p", "white-space: pre-wrap; text-align: justify");
  text("  first");
  text("  word\n");
  text("second");
  close("p");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 2u);
  const auto& first = *static_cast<const PageLine*>(lines[0])->getBlock();
  ASSERT_EQ(first.wordCount(), 6);
  EXPECT_STREQ(first.wordText(0), " ");
  EXPECT_STREQ(first.wordText(1), " ");
  EXPECT_STREQ(first.wordText(2), "first");
  EXPECT_STREQ(first.wordText(3), " ");
  EXPECT_STREQ(first.wordText(4), " ");
  EXPECT_STREQ(first.wordText(5), "word");
  // Literal spaces use the font's space advance, with no inserted word gaps.
  EXPECT_EQ(first.wordXpos(2) - first.wordXpos(0), 2 * renderer.getSpaceWidth(0, EpdFontFamily::REGULAR));
  EXPECT_EQ(lines[1]->yPos - lines[0]->yPos, renderer.getLineHeight(0));
}

TEST_F(ChapterBlockDecorationTest, BlockEmphasisIsInheritedOverriddenAndRestored) {
  open("blockquote", "font-style: italic");
  open("p", nullptr);
  text("Inherited");
  close("p");
  open("p", "font-style: normal");
  text("Normal");
  close("p");
  open("p", nullptr);
  text("Restored");
  close("p");
  close("blockquote");
  open("p", nullptr);
  text("Outside");
  close("p");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 4u);
  for (size_t i = 0; i < lines.size(); ++i) {
    const auto& block = *static_cast<const PageLine*>(lines[i])->getBlock();
    EXPECT_EQ((block.wordStyle(0) & EpdFontFamily::ITALIC) != 0, i == 0 || i == 2);
  }
}

TEST_F(ChapterBlockDecorationTest, ExplicitFontInheritancePreservesInlineAndBlockEmphasis) {
  open("body", "font-weight:normal;font-style:normal");
  open("div", "font-weight:bold;font-style:italic");
  open("p", "font-weight:inherit;font-style:inherit");
  text("Block ");
  open("small", "font-weight:inherit;font-style:inherit");
  text("Small ");
  close("small");
  open("span", "font-weight:normal;font-style:normal");
  text("Normal ");
  open("span", "font-weight:inherit;font-style:inherit");
  text("Nested ");
  close("span");
  close("span");
  text("Restored");
  close("p");
  close("div");
  open("p", "font-weight:inherit;font-style:inherit");
  text("Outside");
  close("p");
  close("body");
  const auto lines = elementsWithTag(TAG_PageLine);
  std::vector<std::pair<std::string, uint8_t>> actual;
  for (const auto* element : lines) {
    const auto& block = *static_cast<const PageLine*>(element)->getBlock();
    for (uint16_t i = 0; i < block.wordCount(); ++i) actual.emplace_back(block.wordText(i), block.wordStyle(i));
  }
  const uint8_t emphasis = EpdFontFamily::BOLD | EpdFontFamily::ITALIC;
  const std::vector<std::pair<std::string, uint8_t>> expected = {
      {"Block", emphasis}, {"Small", emphasis}, {"Normal", 0}, {"Nested", 0}, {"Restored", emphasis}, {"Outside", 0}};
  EXPECT_EQ(actual, expected);
}

TEST_F(ChapterBlockDecorationTest, InheritReplacesEarlierDeclarationsAndAllowsLaterOverrides) {
  open("body", "font-weight:normal;font-style:normal");
  open("div", "font-weight:bold;font-style:italic");
  open("p", "font-weight:normal;font-style:normal;font-weight:inherit;font-style:inherit");
  text("Inherited");
  close("p");
  open("p", "font-weight:inherit;font-style:inherit;font-weight:normal;font-style:normal");
  text("Normal");
  close("p");
  close("div");
  open("p", "font-weight:bold;font-style:italic;font-weight:inherit;font-style:inherit");
  text("ParentNormal");
  close("p");
  close("body");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 3u);
  for (size_t i = 0; i < lines.size(); ++i) {
    const auto& block = *static_cast<const PageLine*>(lines[i])->getBlock();
    EXPECT_EQ(block.wordStyle(0), i == 0 ? EpdFontFamily::BOLD | EpdFontFamily::ITALIC : 0);
  }
}

TEST_F(ChapterBlockDecorationTest, InheritUsesSemanticParentEmphasisAndRestoresSiblingStyle) {
  open("p", nullptr);
  open("b", nullptr);
  open("i", nullptr);
  open("span", "font-weight:inherit;font-style:inherit");
  text("Inherited");
  close("span");
  close("i");
  close("b");
  text(" Outside");
  close("p");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 1u);
  const auto& block = *static_cast<const PageLine*>(lines[0])->getBlock();
  ASSERT_EQ(block.wordCount(), 2u);
  EXPECT_EQ(block.wordStyle(0), EpdFontFamily::BOLD | EpdFontFamily::ITALIC);
  EXPECT_EQ(block.wordStyle(1), EpdFontFamily::REGULAR);
}

TEST_F(ChapterBlockDecorationTest, FontInheritanceUsesWinningCssRuleBeforeInlineOverride) {
  const auto path = std::filesystem::temp_directory_path() / "crosspoint-font-inheritance.css";
  {
    HalFile output;
    ASSERT_TRUE(output.open(path.c_str(), "wb"));
    const std::string css =
        "p{font-weight:bold;font-style:italic}"
        ".inherited{font-weight:normal;font-style:normal}"
        "#inherited{font-weight:inherit;font-style:inherit}";
    ASSERT_EQ(output.write(css.data(), css.size()), css.size());
  }
  {
    HalFile input;
    ASSERT_TRUE(input.open(path.c_str(), "rb"));
    ASSERT_EQ(cssParser.loadFromStream(input), CssParser::ParseResult::Complete);
  }
  std::filesystem::remove(path);
  open("body", "font-weight:normal;font-style:normal");
  const XML_Char* inherited[] = {"id", "inherited", "class", "inherited", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "p", inherited);
  text("Normal");
  close("p");
  open("div", "font-weight:bold;font-style:italic");
  ChapterHtmlSlimParser::startElement(&parser, "p", inherited);
  text("Emphasis");
  close("p");
  const XML_Char* overridden[] = {"id", "inherited", "style", "font-weight:normal;font-style:normal", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "p", overridden);
  text("Override");
  close("p");
  close("div");
  close("body");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 3u);
  for (size_t i = 0; i < lines.size(); ++i) {
    const auto& block = *static_cast<const PageLine*>(lines[i])->getBlock();
    EXPECT_EQ(block.wordStyle(0), i == 1 ? EpdFontFamily::BOLD | EpdFontFamily::ITALIC : 0);
  }
}

TEST_F(ChapterBlockDecorationTest, PreElementPreservesNewlinesWithoutLeakingToFollowingParagraph) {
  open("pre", nullptr);
  text("one\ntwo");
  close("pre");
  open("p", nullptr);
  text("three\nfour");
  close("p");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 3u);
  const auto& last = *static_cast<const PageLine*>(lines[2])->getBlock();
  ASSERT_EQ(last.wordCount(), 2);
  EXPECT_STREQ(last.wordText(0), "three");
  EXPECT_STREQ(last.wordText(1), "four");
}

// D8: only the reader's own raster initial draws a drop cap; the CSS routes (a floated or oversized
// first span, ::first-letter) lay the letter out as ordinary text.
TEST_F(ChapterBlockDecorationTest, FloatedLeadingSpanStaysOrdinaryText) {
  open("p", nullptr);
  open("span", "float: left; font-size: 3em");
  text("T");
  close("span");
  text("he " + words(60));
  close("p");

  EXPECT_TRUE(elementsWithTag(TAG_PageDropCap).empty());
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_GT(lines.size(), 4u);
  for (const auto* line : lines) EXPECT_EQ(line->xPos, 0);
  EXPECT_STREQ(static_cast<const PageLine*>(lines[0])->getBlock()->wordText(0), "T");
}

TEST_F(ChapterBlockDecorationTest, MultiWordSpanStaysText) {
  open("p", nullptr);
  open("span", "float: left");
  text("Once upon");
  close("span");
  text(" a time");
  close("p");
  EXPECT_TRUE(elementsWithTag(TAG_PageDropCap).empty());
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_STREQ(static_cast<const PageLine*>(lines[0])->getBlock()->wordText(0), "Once");
}

TEST_F(ChapterBlockDecorationTest, FirstLetterRuleStaysOrdinaryText) {
  const auto cssPath = std::filesystem::temp_directory_path() / "crosspoint-first-letter.css";
  {
    HalFile output;
    ASSERT_TRUE(output.open(cssPath.c_str(), "wb"));
    const std::string css = "p.opening::first-letter { initial-letter: 2; }";
    output.write(css.data(), css.size());
  }
  HalFile input;
  ASSERT_TRUE(input.open(cssPath.c_str(), "rb"));
  ASSERT_EQ(cssParser.loadFromStream(input), CssParser::ParseResult::Complete);

  const XML_Char* attributes[] = {"class", "opening", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "p", attributes);
  text("\xe2\x80\x9cIt was " + words(40));
  close("p");
  open("p", nullptr);
  text("Next paragraph");
  close("p");

  EXPECT_TRUE(elementsWithTag(TAG_PageDropCap).empty());
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_FALSE(lines.empty());
  EXPECT_STREQ(static_cast<const PageLine*>(lines[0])->getBlock()->wordText(0), "\xe2\x80\x9cIt");
  std::filesystem::remove(cssPath);
}

TEST_F(ChapterBlockDecorationTest, BorderedBlockFramesItsTextAndInsetsIt) {
  open("div", "border: 2px solid black");
  open("p", nullptr);
  text("Framed text");
  close("p");
  close("div");
  open("p", nullptr);
  text("Outside");
  close("p");

  const auto boxes = elementsWithTag(TAG_PageBorderBox);
  ASSERT_EQ(boxes.size(), 1u);
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_GE(lines[0]->xPos, 6);  // 2 px border + 4 px gap
  EXPECT_GT(lines[0]->yPos, boxes[0]->yPos);
  EXPECT_EQ(lines[1]->xPos, 0);
}

TEST_F(ChapterBlockDecorationTest, EmptyBorderedBlockDrawsARule) {
  open("p", nullptr);
  text("Above");
  close("p");
  open("div", "border-top: 1px solid");
  close("div");
  open("p", nullptr);
  text("Below");
  close("p");
  const auto boxes = elementsWithTag(TAG_PageBorderBox);
  ASSERT_EQ(boxes.size(), 1u);
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_GT(boxes[0]->yPos, lines[0]->yPos);
  EXPECT_GT(lines[1]->yPos, boxes[0]->yPos);
}

TEST_F(ChapterBlockDecorationTest, ShadedBoxSplitsAcrossPages) {
  parser.viewportHeight = 100;
  open("div", "background-color: #ddd");
  for (int i = 0; i < 12; ++i) {
    open("p", nullptr);
    text("line");
    close("p");
  }
  close("div");
  EXPECT_GE(pages.size(), 1u);
  EXPECT_GE(elementsWithTag(TAG_PageBorderBox).size(), 2u);
}

class ChapterTableBorderTest : public ChapterBlockDecorationTest {
 protected:
  void table(const XML_Char** tableAttributes, const char* cellStyle) {
    ChapterHtmlSlimParser::startElement(&parser, "table", tableAttributes);
    ChapterHtmlSlimParser::startElement(&parser, "tr", nullptr);
    for (const char* cell : {"alpha", "beta"}) {
      open("td", cellStyle);
      text(cell);
      close("td");
    }
    close("tr");
    close("table");
  }
};

TEST_F(ChapterTableBorderTest, BorderAttributeFramesEveryCell) {
  const XML_Char* attributes[] = {"border", "1", nullptr};
  table(attributes, nullptr);
  EXPECT_EQ(elementsWithTag(TAG_PageBorderBox).size(), 2u);
  EXPECT_TRUE(elementsWithTag(TAG_PageHorizontalRule).empty());
}

TEST_F(ChapterTableBorderTest, CellCssBorderFramesEveryCell) {
  table(nullptr, "border: 1px solid black");
  const auto boxes = elementsWithTag(TAG_PageBorderBox);
  ASSERT_EQ(boxes.size(), 2u);
  EXPECT_EQ(boxes[0]->yPos, boxes[1]->yPos);
  EXPECT_LT(boxes[0]->xPos, boxes[1]->xPos);
}

TEST_F(ChapterTableBorderTest, UnborderedTableKeepsRowRule) {
  table(nullptr, nullptr);
  EXPECT_TRUE(elementsWithTag(TAG_PageBorderBox).empty());
  EXPECT_EQ(elementsWithTag(TAG_PageHorizontalRule).size(), 1u);
}

TEST_F(ChapterTableBorderTest, RichCellsPreserveParagraphStylesAndFollowingFlow) {
  parser.paragraphAlignment = static_cast<uint8_t>(CssTextAlign::None);  // Book's Style honors CSS alignment.
  const size_t initialDepth = parser.blockStyleStack.size();
  open("table", nullptr);
  open("tr", nullptr);
  open("td", nullptr);
  text("Firstcell");
  close("td");
  open("td", nullptr);
  open("p", "margin-left: 24px; font-style: italic; font-size: 0.75em");
  EXPECT_TRUE(parser.tableRowStacked);
  EXPECT_TRUE(parser.effectiveItalic);
  EXPECT_FLOAT_EQ(parser.currentTextBlock->getBlockStyle().fontScale, 0.75f);
  text("Entryone");
  close("p");
  open("p", "text-align: right");
  EXPECT_FALSE(parser.effectiveItalic);
  text("Entrytwo");
  close("p");
  close("td");
  close("tr");
  close("table");
  EXPECT_EQ(parser.blockStyleStack.size(), initialDepth);
  open("p", nullptr);
  text("Outside");
  close("p");

  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 4u);
  const char* expected[] = {"Firstcell", "Entryone", "Entrytwo", "Outside"};
  for (size_t i = 0; i < lines.size(); ++i) {
    EXPECT_STREQ(static_cast<const PageLine*>(lines[i])->getBlock()->wordText(0), expected[i]);
    if (i > 0) EXPECT_GT(lines[i]->yPos, lines[i - 1]->yPos);
  }
  EXPECT_GE(lines[1]->xPos, 24);
  const auto& left = *static_cast<const PageLine*>(lines[1])->getBlock();
  const auto& right = *static_cast<const PageLine*>(lines[2])->getBlock();
  EXPECT_GT(lines[2]->xPos + right.wordXpos(0), lines[1]->xPos + left.wordXpos(0));
  EXPECT_EQ(lines[3]->xPos, 0);
}

TEST_F(ChapterTableBorderTest, CellImagesUseNormalImageHandlingAndRespectDisplayNone) {
  parser.imageRendering = 1;  // Exercise the shared image fallback without a decoder.
  open("table", nullptr);
  open("tr", nullptr);
  open("td", nullptr);
  const XML_Char* hidden[] = {"style", "display:none", "alt", "hidden", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "img", hidden);
  close("img");
  EXPECT_FALSE(parser.tableRowStacked);
  const XML_Char* visible[] = {"alt", "skull", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "img", visible);
  close("img");
  EXPECT_TRUE(parser.tableRowStacked);
  close("td");
  close("tr");
  close("table");

  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 1u);
  const auto* block = static_cast<const PageLine*>(lines[0])->getBlock();
  ASSERT_EQ(block->wordCount(), 2u);
  EXPECT_STREQ(block->wordText(0), "[Image:");
  EXPECT_STREQ(block->wordText(1), "skull]");
}

class ChapterInlineSizeTest : public ChapterBlockDecorationTest {
 protected:
  void SetUp() override {
    ChapterBlockDecorationTest::SetUp();
    installBuiltinVariants(renderer);
    for (const int id : {NOTOSANS_12_FONT_ID, NOTOSANS_14_FONT_ID, NOTOSANS_16_FONT_ID, NOTOSANS_18_FONT_ID}) {
      renderer.fontMap.emplace(id, EpdFontFamily(nullptr));
    }
    parser.fontId = NOTOSANS_14_FONT_ID;
  }
  int firstLineFontId() {
    const auto lines = elementsWithTag(TAG_PageLine);
    return lines.empty() ? -1 : static_cast<const PageLine*>(lines[0])->getBlock()->getBlockStyle().fontId;
  }
};

TEST_F(ChapterInlineSizeTest, SpanHoldingWholeBlockSizesIt) {
  open("p", nullptr);
  open("span", "font-size: 2em");
  text("Chapter One");
  close("span");
  text(" ");
  close("p");
  EXPECT_EQ(firstLineFontId(), NOTOSANS_18_FONT_ID);
}

TEST_F(ChapterInlineSizeTest, ShortSizedHeadingSpanFallsBackToStyledText) {
  open("h1", "font-size: 1em; font-style: italic");
  open("span", "font-size: 2em");
  text("II");
  close("span");
  close("h1");
  open("p", nullptr);
  text("After");
  close("p");

  EXPECT_TRUE(elementsWithTag(TAG_PageDropCap).empty());
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 2u);
  const auto& heading = *static_cast<const PageLine*>(lines[0])->getBlock();
  ASSERT_EQ(heading.wordCount(), 1u);
  EXPECT_STREQ(heading.wordText(0), "II");
  EXPECT_EQ(heading.getBlockStyle().fontId, NOTOSANS_18_FONT_ID);
  EXPECT_EQ(heading.wordStyle(0), EpdFontFamily::BOLD | EpdFontFamily::ITALIC);
  const auto& following = *static_cast<const PageLine*>(lines[1])->getBlock();
  ASSERT_EQ(following.wordCount(), 1u);
  EXPECT_STREQ(following.wordText(0), "After");
  EXPECT_EQ(following.wordStyle(0), EpdFontFamily::REGULAR);
  EXPECT_EQ(following.wordFontId(0, NOTOSANS_14_FONT_ID), NOTOSANS_14_FONT_ID);
}

TEST_F(ChapterInlineSizeTest, NestedSpansCompoundTheirSizes) {
  open("p", nullptr);
  open("span", "font-size: 0.9em");
  open("span", "font-size: 0.9em");
  text("small print");
  close("span");
  close("span");
  close("p");
  EXPECT_EQ(firstLineFontId(), NOTOSANS_12_FONT_ID);
}

TEST_F(ChapterInlineSizeTest, TextAfterTheSpanKeepsBodySize) {
  open("p", nullptr);
  open("span", "font-size: 2em");
  text("Lead");
  close("span");
  text(" and the rest of the paragraph");
  close("p");
  EXPECT_EQ(firstLineFontId(), 0);
  // The lead words keep their own size inline.
  const auto& block = *static_cast<const PageLine*>(elementsWithTag(TAG_PageLine)[0])->getBlock();
  EXPECT_EQ(block.wordFontId(0, NOTOSANS_14_FONT_ID), NOTOSANS_18_FONT_ID);
  EXPECT_EQ(block.wordFontId(1, NOTOSANS_14_FONT_ID), NOTOSANS_14_FONT_ID);
}

TEST_F(ChapterInlineSizeTest, MixedSizesWithinALineSurviveTheCache) {
  open("p", nullptr);
  text("Normal ");
  open("span", "font-size: 0.8em");
  text("small");
  close("span");
  text(" then ");
  open("big", nullptr);
  text("big");
  close("big");
  close("p");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 1u);
  const auto& block = *static_cast<const PageLine*>(lines[0])->getBlock();
  ASSERT_EQ(block.wordCount(), 4);
  EXPECT_EQ(block.wordFontId(0, NOTOSANS_14_FONT_ID), NOTOSANS_14_FONT_ID);
  EXPECT_EQ(block.wordFontId(1, NOTOSANS_14_FONT_ID), NOTOSANS_12_FONT_ID);
  EXPECT_EQ(block.wordFontId(3, NOTOSANS_14_FONT_ID), NOTOSANS_16_FONT_ID);

  const auto path = std::filesystem::temp_directory_path() / "crosspoint-word-fonts.bin";
  {
    HalFile output;
    ASSERT_TRUE(output.open(path.c_str(), "wb"));
    ASSERT_TRUE(block.serialize(output));
  }
  HalFile input;
  ASSERT_TRUE(input.open(path.c_str(), "rb"));
  const auto cached = TextBlock::deserialize(input);
  ASSERT_NE(cached, nullptr);
  for (uint16_t i = 0; i < block.wordCount(); ++i) {
    EXPECT_EQ(cached->wordFontId(i, NOTOSANS_14_FONT_ID), block.wordFontId(i, NOTOSANS_14_FONT_ID));
  }
  std::filesystem::remove(path);
}

TEST_F(ChapterBlockDecorationTest, SdAndVectorFontsSizeThroughTheVariantProvider) {
  constexpr int SD_FONT = 7;  // not a built-in ladder font
  parser.fontId = SD_FONT;
  installSyntheticVariants(renderer);
  open("h1", nullptr);
  text("Title");
  close("h1");
  open("p", nullptr);
  text("Body ");
  open("span", "font-size: 0.8em");
  text("small");
  close("span");
  close("p");

  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(static_cast<const PageLine*>(lines[0])->getBlock()->getBlockStyle().fontId, SD_FONT * 1000 + 200);
  const auto& body = *static_cast<const PageLine*>(lines[1])->getBlock();
  EXPECT_EQ(body.wordFontId(0, SD_FONT), SD_FONT);
  EXPECT_EQ(body.wordFontId(1, SD_FONT), SD_FONT * 1000 + 80);
}

// A board without PSRAM (the X3) has no memory for a second font size and v1.0.76 laid headings out at the book size:
// no heading, with or without a size of its own in the book's CSS, asks the font system for a variant. The sizes of
// other text are not the heading's business.
TEST_F(ChapterBlockDecorationTest, HeadingsOfABoardWithoutRoomForSizesStayAtTheBookSizeAndAskForNoVariant) {
  constexpr int SD_FONT = 7;
  parser.fontId = SD_FONT;
  parser.setHeadingSizes(false);
  int asked = 0;
  renderer.setFontVariantProvider({&asked, [](void* ctx, const int fontId, const float scale) {
                                     ++*static_cast<int*>(ctx);
                                     return fontId * 1000 + static_cast<int>(scale * 100 + 0.5f);
                                   }});
  open("h1", nullptr);
  text("Title");
  close("h1");
  open("h2", "font-size: 2em");
  text("Section");
  close("h2");
  EXPECT_EQ(asked, 0) << "a heading asked the font system for a size";
  open("p", nullptr);
  text("Body ");
  open("span", "font-size: 0.8em");
  text("small");
  close("span");
  close("p");

  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(static_cast<const PageLine*>(lines[0])->getBlock()->getBlockStyle().fontId, 0) << "h1 left the book size";
  EXPECT_EQ(static_cast<const PageLine*>(lines[1])->getBlock()->getBlockStyle().fontId, 0) << "h2 left the book size";
  EXPECT_EQ(asked, 1) << "the small span still sizes itself";
}

// A board that has the memory gives a heading its size or does not build the chapter: never the body size because the
// heap was low at that moment, which would put a page break elsewhere than in a build with the heap to spare.
TEST_F(ChapterBlockDecorationTest, ASizeTheFontSystemRefusedStopsTheBuildInsteadOfFallingBackToTheBodySize) {
  constexpr int SD_FONT = 7;
  parser.fontId = SD_FONT;
  renderer.setFontVariantProvider({nullptr, [](void*, int, float) { return FontVariantProvider::REFUSED; }});
  open("h1", nullptr);
  text("Title");
  close("h1");
  open("p", nullptr);
  text("Body");
  close("p");

  EXPECT_TRUE(parser.hasFailed()) << "the chapter was laid out with the heading at the body size";
  for (const auto* line : elementsWithTag(TAG_PageLine))
    EXPECT_STRNE(static_cast<const PageLine*>(line)->getBlock()->wordText(0), "Title");
}

TEST_F(ChapterBlockDecorationTest, ASizeTheFontSystemHasRoomForIsUsedAndTheBuildGoesOn) {
  constexpr int SD_FONT = 7;
  parser.fontId = SD_FONT;
  installSyntheticVariants(renderer);
  open("h1", nullptr);
  text("Title");
  close("h1");
  EXPECT_FALSE(parser.hasFailed());
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(static_cast<const PageLine*>(lines[0])->getBlock()->getBlockStyle().fontId, SD_FONT * 1000 + 200);
}

TEST_F(ChapterBlockDecorationTest, UnloadableVariantFallsBackToSectionFont) {
  EXPECT_EQ(TextBlock::renderFontId(renderer, 12345, 7), 7);
  installSyntheticVariants(renderer);
  EXPECT_EQ(TextBlock::renderFontId(renderer, 12345, 7), 12345);
}

TEST_F(ChapterInlineSizeTest, UniformLinesCarryNoWordFonts) {
  open("p", nullptr);
  text("Plain text only");
  close("p");
  const auto& block = *static_cast<const PageLine*>(elementsWithTag(TAG_PageLine)[0])->getBlock();
  EXPECT_EQ(block.wordFontId(1, NOTOSANS_14_FONT_ID), NOTOSANS_14_FONT_ID);
  EXPECT_EQ(block.extraAscent(renderer, NOTOSANS_14_FONT_ID), 0);
}

TEST_F(ChapterTableBorderTest, ScansColumnWidthsFromMarkup) {
  renderer.sdFont = true;
  const auto path = std::filesystem::temp_directory_path() / "crosspoint-table-scan.xhtml";
  {
    HalFile output;
    ASSERT_TRUE(output.open(path.c_str(), "wb"));
    const std::string html =
        "<table class=\"t\"><tr><th>ID</th><td>A much longer description of the item</td></tr>"
        "<tr><td colspan=\"2\">Spanning note that must not widen anything at all</td></tr>"
        "<tr><td>7</td><td>short<br/>lines &amp; more</td></tr></table><p>after</p>";
    output.write(html.data(), html.size());
  }
  HalFile input;
  ASSERT_TRUE(input.open(path.c_str(), "rb"));
  ChapterHtmlSlimParser::TableColumnMeasure measure;
  ASSERT_TRUE(parser.measureTableColumns(input, measure));
  EXPECT_EQ(renderer.coldMeasurements, 0u);
  EXPECT_EQ(measure.columns, 2);
  EXPECT_EQ(measure.minWidth[0], 16);  // "ID"
  EXPECT_EQ(measure.minWidth[1], 88);  // "description"
  EXPECT_EQ(measure.prefWidth[1], 31 * 8 + 6 * 4);

  parser.planTableColumns(measure);
  ASSERT_EQ(parser.tableColumnCount, 2);
  EXPECT_EQ(parser.tableColumnWidths[0] + parser.tableColumnWidths[1], parser.viewportWidth);
  EXPECT_LT(parser.tableColumnWidths[0], parser.tableColumnWidths[1]);

  parser.viewportWidth = 200;  // too narrow for natural widths: column 0 keeps its word
  parser.planTableColumns(measure);
  EXPECT_EQ(parser.tableColumnWidths[0], 16 + 8);
  EXPECT_EQ(parser.tableColumnWidths[1], 200 - 24);
  std::filesystem::remove(path);
}

TEST_F(ChapterTableBorderTest, RowsUsePlannedColumnWidths) {
  ChapterHtmlSlimParser::startElement(&parser, "table", nullptr);
  parser.tableColumnWidths = {100, 380};
  parser.tableColumnCount = 2;
  ChapterHtmlSlimParser::startElement(&parser, "tr", nullptr);
  for (const char* cell : {"alpha", "beta"}) {
    open("td", nullptr);
    text(cell);
    close("td");
  }
  close("tr");
  close("table");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0]->xPos, 4);
  EXPECT_EQ(lines[1]->xPos, 104);
}

TEST_F(ChapterTableBorderTest, CssCellPaddingInsetsContentAndReservesRowHeight) {
  open("table", nullptr);
  parser.tableColumnWidths = {100, 380};
  parser.tableColumnCount = 2;
  open("tr", nullptr);
  open("td", "padding: 9px 17px 13px 11px; text-align: right");
  text("alpha");
  close("td");
  open("td", "padding: 3px 0 5px");
  text("beta");
  close("td");
  close("tr");
  close("table");
  const int rowBottom = parser.currentPageNextY;
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0]->xPos, 11);
  EXPECT_EQ(lines[1]->xPos, 100);
  EXPECT_EQ(lines[0]->yPos, 9);
  EXPECT_EQ(lines[1]->yPos, 3);
  const auto& first = *static_cast<const PageLine*>(lines[0])->getBlock();
  EXPECT_EQ(lines[0]->xPos + first.wordXpos(0) + renderer.getTextAdvanceX(0, "alpha", EpdFontFamily::REGULAR), 83);
  EXPECT_GE(rowBottom, 9 + renderer.getLineHeight(0) + 13);
}

TEST_F(ChapterTableBorderTest, PaddedRowsBreakBeforeTheBottomPaddingOverflows) {
  parser.viewportHeight = 48;
  table(nullptr, "padding: 8px");
  table(nullptr, "padding: 8px");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 4u);
  ASSERT_EQ(pages.size(), 2u);
  for (const auto* line : lines) {
    EXPECT_EQ(line->yPos, 8);
    EXPECT_LE(line->yPos + renderer.getLineHeight(0) + 8, parser.viewportHeight);
  }
}

TEST_F(ChapterTableBorderTest, StackedCellPaddingSurroundsChildParagraphs) {
  const size_t initialDepth = parser.blockStyleStack.size();
  open("table", nullptr);
  open("tr", nullptr);
  open("td", "padding: 7px 8px 9px 10px");
  open("p", nullptr);
  text("Inside");
  close("p");
  close("td");
  close("tr");
  close("table");
  EXPECT_EQ(parser.blockStyleStack.size(), initialDepth);
  const int rowBottom = parser.currentPageNextY;
  open("p", nullptr);
  text("Outside");
  close("p");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0]->xPos, 10);
  EXPECT_EQ(lines[0]->yPos, 7);
  EXPECT_GE(rowBottom, 7 + renderer.getLineHeight(0) + 9);
  EXPECT_EQ(lines[1]->xPos, 0);
}

TEST_F(ChapterTableBorderTest, LongTwoColumnFixtureKeepsColumnsAcrossPages) {
  parser.viewportWidth = 480;
  parser.viewportHeight = 64;
  const char* cells[] = {
      "This is one very long sentence. It is part of the first cell of this table. It is quite nice.",
      "This is the second cell. It will probably overflow into the next page. Lorem ipsum dolor sit amet, consetetur "
      "sadipscing elitr, sed diam nonumy eirmod tempor invidunt ut labore et dolore magna aliquyam erat, sed diam "
      "voluptua. At vero eos et accusam et justo duo dolores et ea rebum. Stet clita kasd gubergren, no sea takimata "
      "sanctus est Lorem ipsum dolor sit amet. Lorem ipsum dolor sit amet, consetetur sadipscing elitr, sed diam "
      "nonumy eirmod tempor invidunt ut labore et dolore magna aliquyam erat, sed diam voluptua. At vero eos et "
      "accusam et justo duo dolores et ea rebum. Stet clita kasd gubergren, no sea takimata sanctus est Lorem ipsum "
      "dolor sit amet."};
  std::multiset<std::string> expected[2];
  open("table", nullptr);
  parser.tableColumnWidths = {160, 320};
  parser.tableColumnCount = 2;
  open("tr", nullptr);
  for (size_t column = 0; column < 2; ++column) {
    open("td", "padding: 3px 6px");
    std::istringstream input(cells[column]);
    for (std::string word; input >> word;) expected[column].insert(word);
    // XML can split a cell across arbitrary character-data callbacks.
    const std::string content(cells[column]);
    for (size_t offset = 0; offset < content.size(); offset += 17) text(content.substr(offset, 17));
    close("td");
    EXPECT_FALSE(parser.tableRowStacked);
  }
  close("tr");
  close("table");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_GT(pages.size(), 1u);
  std::multiset<std::string> actual[2];
  for (const auto* element : lines) {
    const auto& line = *static_cast<const PageLine*>(element);
    const size_t column = line.xPos == 6 ? 0 : 1;
    EXPECT_EQ(line.xPos, column == 0 ? 6 : 166);
    EXPECT_LE(line.yPos + renderer.getLineHeight(0), parser.viewportHeight);
    for (uint16_t i = 0; i < line.getBlock()->wordCount(); ++i) {
      actual[column].insert(line.getBlock()->wordText(i));
    }
  }
  EXPECT_EQ(actual[0], expected[0]);
  EXPECT_EQ(actual[1], expected[1]);
}

TEST_F(ChapterTableBorderTest, RowBudgetFallsBackAndResetsForTheNextRow) {
  open("table", nullptr);
  open("tr", nullptr);
  for (int column = 0; column < 2; ++column) {
    open("td", nullptr);
    text(words(100));
    close("td");
  }
  EXPECT_TRUE(parser.tableRowStacked);
  close("tr");
  open("tr", nullptr);
  EXPECT_EQ(parser.tableRowTextBytes, 0u);
  for (int column = 0; column < 2; ++column) {
    open("td", nullptr);
    text(words(70));
    close("td");
    EXPECT_FALSE(parser.tableRowStacked);
  }
  close("tr");
  close("table");
  size_t count = 0;
  for (const auto* element : elementsWithTag(TAG_PageLine)) {
    count += static_cast<const PageLine*>(element)->getBlock()->wordCount();
  }
  EXPECT_EQ(count, 340u);
}

class ChapterWidowOrphanTest : public ChapterBlockDecorationTest {
 protected:
  void SetUp() override {
    ChapterBlockDecorationTest::SetUp();
    parser.viewportHeight = 5 * 16;  // five fixture lines per page
  }
  void paragraph(int wordCount) {
    open("p", nullptr);
    text(words(wordCount));
    close("p");
  }
  size_t linesOnPage(size_t index) {
    size_t count = 0;
    for (const auto& element : pages[index]->elements) count += element->getTag() == TAG_PageLine;
    return count;
  }
};

TEST_F(ChapterWidowOrphanTest, LoneFirstLineMovesToNextPage) {
  paragraph(13 * 4);  // four full lines
  paragraph(13 * 3);  // its first line would be alone at the page bottom
  ASSERT_GE(pages.size(), 1u);
  EXPECT_EQ(linesOnPage(0), 4u);
}

TEST_F(ChapterWidowOrphanTest, LoneLastLineTakesALineAlong) {
  paragraph(13 * 5 + 3);  // six lines: the sixth would open page two alone
  paragraph(13);
  ASSERT_GE(pages.size(), 1u);
  EXPECT_EQ(linesOnPage(0), 4u);
  const auto& firstOnNext = *parser.currentPage->elements.front();
  EXPECT_EQ(firstOnNext.yPos, 0);
}

TEST_F(ChapterWidowOrphanTest, HeadingMovesWithTheParagraphAfterIt) {
  paragraph(13 * 4);  // four lines
  open("h2", nullptr);
  text("Heading");
  close("h2");        // fits as the page's fifth line
  paragraph(13 * 3);  // cannot start on this page
  ASSERT_GE(pages.size(), 1u);
  EXPECT_EQ(linesOnPage(0), 4u);
  const auto& first = *static_cast<const PageLine&>(*parser.currentPage->elements.front()).getBlock();
  EXPECT_STREQ(first.wordText(0), "Heading");
}

TEST_F(ChapterWidowOrphanTest, HeadingStaysWhenFollowedOnItsPage) {
  // Two paragraph gaps (never zero: 4 px at a 16 px line) and the 2 px by which a line's ink (18 px)
  // exceeds its 16 px box, which this firmware counts when it decides a line fits.
  parser.viewportHeight = static_cast<uint16_t>(5 * 16 + 2 * readerSpacing::paragraphGap(0, 16) + 2);
  paragraph(13 * 2);
  open("h2", nullptr);
  text("Heading");
  close("h2");
  paragraph(13 * 4);  // two of its four lines fit below the heading, two follow
  ASSERT_GE(pages.size(), 1u);
  EXPECT_EQ(linesOnPage(0), 5u);
}

TEST_F(ChapterWidowOrphanTest, TwoLineParagraphMovesWhole) {
  paragraph(13 * 4);
  paragraph(13 * 2);  // one line fits: moving just one would orphan, so both move
  ASSERT_GE(pages.size(), 1u);
  EXPECT_EQ(linesOnPage(0), 4u);
}

TEST_F(ChapterHtmlSlimParserTest, PassesIndentSettingsToNewTextBlock) {
  for (bool extraSpacing : {false, true}) {
    parser.extraParagraphSpacing = extraSpacing;
    parser.currentTextBlock.reset();
    parser.setParagraphIndentSpaces(5);
    parser.startNewTextBlock(BlockStyle());
    ASSERT_NE(parser.currentTextBlock, nullptr);
    EXPECT_EQ(parser.currentTextBlock->paragraphIndentSpaces, 5);
  }
}

TEST_F(ChapterWidowOrphanTest, CarriedReferenceKeepsItsFootnote) {
  for (const bool repeated : {false, true}) {
    SCOPED_TRACE(repeated);
    pages.clear();
    parser.currentPage.reset();
    paragraph(1);
    paragraph(1);
    parser.currentPageNextY = 32;
    parser.recentLineCount = 2;
    parser.recentLineOffsets[0] = 0;
    parser.recentLineOffsets[1] = 10;
    parser.currentPage->addFootnote("1", "#note");
    parser.currentPage->addFootnote("2", "#other");
    const int16_t carriedTop = parser.currentPage->elements.back()->yPos;
    parser.currentPage->addLink("#note", 0, carriedTop, 8, 16);
    parser.currentPage->addLink("#other", 8, 0, 8, 16);
    if (repeated) parser.currentPage->addLink("#note", 0, 0, 8, 16);
    ASSERT_TRUE(parser.canCarry(1));
    parser.breakPageCarryingLines(1, 1, 20);
    ASSERT_EQ(pages.size(), 1u);
    ASSERT_EQ(parser.currentPage->footnotes.size(), 1u);
    EXPECT_STREQ(parser.currentPage->footnotes[0].href, "#note");
    EXPECT_STREQ(parser.currentPage->footnotes[0].number, "1");
    EXPECT_EQ(pages[0]->footnotes.size(), repeated ? 2u : 1u);
    EXPECT_TRUE(std::any_of(pages[0]->footnotes.begin(), pages[0]->footnotes.end(),
                            [](const auto& note) { return strcmp(note.href, "#other") == 0; }));
    EXPECT_EQ(parser.currentPage->links[0].y, 0);
  }
}

TEST_F(ChapterPageBreakTest, TerminalBreakAfterDoesNotCreateEmptyPage) {
  paragraph("only", "page-break-after: always");
  ASSERT_TRUE(parser.finishParse());
  EXPECT_EQ(pages, 1u);
}

TEST_F(ChapterPageBreakTest, EmptyBlockDoesNotConsumePendingBreak) {
  paragraph("before", "page-break-after: always");
  paragraph("", "page-break-before: always");
  EXPECT_EQ(pages, 0u);
  paragraph("after");
  EXPECT_EQ(pages, 1u);
  ASSERT_TRUE(parser.finishParse());
  EXPECT_EQ(pages, 2u);
}

TEST_F(ChapterInlineSizeTest, NestedRemResetKeepsItsFontInsideSizedParagraph) {
  open("p", nullptr);
  open("span", "font-size: 0.8em");
  text("small ");
  open("span", "font-size: 1rem");
  text("normal");
  close("span");
  text(" small");
  close("span");
  close("p");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 1u);
  const auto& block = *static_cast<const PageLine*>(lines[0])->getBlock();
  ASSERT_EQ(block.wordCount(), 3u);
  EXPECT_EQ(block.wordFontId(0, parser.fontId), NOTOSANS_12_FONT_ID);
  EXPECT_EQ(block.wordFontId(1, parser.fontId), NOTOSANS_14_FONT_ID);
  EXPECT_EQ(block.wordFontId(2, parser.fontId), NOTOSANS_12_FONT_ID);
}

TEST_F(ChapterInlineSizeTest, DescendantFontSizeResolvesAfterAncestorSize) {
  open("body", "font-size: 0.8em");
  open("p", "font-size: 1.2rem");
  text("Absolute");
  close("p");
  open("p", "font-size: 1.2em");
  text("Relative");
  close("p");
  open("p", nullptr);
  text("Inherited");
  close("p");
  close("body");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 3u);
  const int expected[] = {NOTOSANS_16_FONT_ID, NOTOSANS_14_FONT_ID, NOTOSANS_12_FONT_ID};
  for (size_t i = 0; i < lines.size(); ++i) {
    const auto& block = *static_cast<const PageLine*>(lines[i])->getBlock();
    EXPECT_EQ(block.wordFontId(0, NOTOSANS_14_FONT_ID), expected[i]);
  }
}

TEST_F(ChapterInlineSizeTest, SpecializedInlineTagsKeepTheirFontSize) {
  for (const char* tag : {"b", "strong", "i", "em", "u", "s", "sup", "sub", "a"}) {
    SCOPED_TRACE(tag);
    open("p", nullptr);
    text("Normal ");
    const XML_Char* attributes[] = {"style", "font-size: 0.8em", "href", "#note", nullptr};
    ChapterHtmlSlimParser::startElement(&parser, tag, attributes);
    text("small");
    close(tag);
    text(" after");
    close("p");
    const auto lines = elementsWithTag(TAG_PageLine);
    const auto& block = *static_cast<const PageLine*>(lines.back())->getBlock();
    ASSERT_EQ(block.wordCount(), 3u);
    EXPECT_EQ(block.wordFontId(1, NOTOSANS_14_FONT_ID), NOTOSANS_12_FONT_ID);
    EXPECT_EQ(block.wordFontId(2, NOTOSANS_14_FONT_ID), NOTOSANS_14_FONT_ID);
  }
}

TEST_F(ChapterInlineSizeTest, EnlargedGridCellsReserveTheirHeightAcrossPages) {
  renderer.fontLineHeights[NOTOSANS_16_FONT_ID] = 32;
  renderer.fontAscenders[NOTOSANS_16_FONT_ID] = 24;
  parser.viewportHeight = 80;
  open("table", nullptr);
  for (const bool inlineSize : {false, true}) {
    open("tr", nullptr);
    open("td", inlineSize ? nullptr : "font-size: 1.2rem");
    if (inlineSize) open("span", "font-size: 1.2rem");
    text(words(18));
    if (inlineSize) close("span");
    close("td");
    open("td", nullptr);
    text("Normal");
    close("td");
    EXPECT_FALSE(parser.tableRowStacked);
    close("tr");
  }
  close("table");
  ASSERT_TRUE(parser.finishParse());
  ASSERT_GE(pages.size(), 2u);
  size_t enlargedLines = 0;
  for (const auto& page : pages) {
    std::map<int, int> bottomByColumn;
    for (const auto& element : page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const auto& line = static_cast<const PageLine&>(*element);
      const auto& block = *line.getBlock();
      const int lineFont = block.blockFontId(parser.fontId);
      const int height = renderer.getLineHeight(lineFont, parser.lineCompression) +
                         block.extraAscent(renderer, parser.fontId) +
                         block.getRubyShift(renderer.getFontAscenderSize(lineFont));
      EXPECT_GE(line.yPos, bottomByColumn[line.xPos]);
      EXPECT_LE(line.yPos + height, parser.viewportHeight);
      bottomByColumn[line.xPos] = line.yPos + height;
      if (line.xPos < parser.viewportWidth / 2) {
        ++enlargedLines;
        EXPECT_EQ(block.wordFontId(0, parser.fontId), NOTOSANS_16_FONT_ID);
      }
    }
  }
  EXPECT_GE(enlargedLines, 6u);
}

TEST_F(ChapterInlineSizeTest, GridCellsResolveBlockAndWordFonts) {
  open("table", nullptr);
  open("tr", nullptr);
  open("td", "font-size: 1.2rem");
  text("Large");
  close("td");
  open("td", nullptr);
  open("span", "font-size: 0.8em");
  text("Small");
  close("span");
  close("td");
  close("tr");
  close("table");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 2u);
  const auto& large = *static_cast<const PageLine*>(lines[0])->getBlock();
  const auto& small = *static_cast<const PageLine*>(lines[1])->getBlock();
  EXPECT_EQ(large.wordFontId(0, NOTOSANS_14_FONT_ID), NOTOSANS_16_FONT_ID);
  EXPECT_EQ(small.wordFontId(0, NOTOSANS_14_FONT_ID), NOTOSANS_12_FONT_ID);
}

TEST_F(ChapterBlockDecorationTest, BorderScopeExcludesPrecedingParentText) {
  for (const char* tag : {"div", "h2"}) {
    SCOPED_TRACE(tag);
    open("div", nullptr);
    text("Outside");
    open(tag, "border: 1px solid");
    text("Inside");
    close(tag);
    close("div");
    const auto boxes = elementsWithTag(TAG_PageBorderBox);
    const auto lines = elementsWithTag(TAG_PageLine);
    ASSERT_GE(lines.size(), 2u);
    ASSERT_FALSE(boxes.empty());
    EXPECT_GT(boxes.back()->yPos, lines[lines.size() - 2]->yPos);
    EXPECT_LT(boxes.back()->yPos, lines.back()->yPos);
  }
}

TEST_F(ChapterBlockDecorationTest, AncestorShadePaintsBeforeNestedBorders) {
  open("div", "background: #eee");
  open("p", "border: 1px solid");
  text("Inside");
  close("p");
  close("div");
  ASSERT_NE(parser.currentPage, nullptr);
  for (const bool placeholders : {false, true}) {
    renderer.boxPaints.clear();
    if (placeholders)
      parser.currentPage->renderWithImagePlaceholders(renderer, 0, 0, 0);
    else
      parser.currentPage->render(renderer, 0, 0, 0);
    ASSERT_FALSE(renderer.boxPaints.empty());
    EXPECT_EQ(renderer.boxPaints.front(), 's');
    EXPECT_NE(std::find(renderer.boxPaints.begin(), renderer.boxPaints.end(), 'b'), renderer.boxPaints.end());
    const auto firstBorder = std::find(renderer.boxPaints.begin(), renderer.boxPaints.end(), 'b');
    EXPECT_EQ(std::find(firstBorder, renderer.boxPaints.end(), 's'), renderer.boxPaints.end());
  }
}

}  // namespace

TEST(ParagraphIndentation, OverridesNonnegativeCssAndPreservesHangingIndent) {
  GfxRenderer renderer;
  for (int cssIndent : {-6, 0, 13}) {
    for (uint8_t spaces : {0, 1, 2, 5}) {
      BlockStyle style;
      style.alignment = CssTextAlign::Left;
      style.textIndentDefined = true;
      style.textIndent = cssIndent;
      ParsedText text(false, false, style, spaces);
      text.addWord("word", EpdFontFamily::REGULAR);
      bool sawLine = false;
      text.layoutAndExtractLines(renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, auto) {
        sawLine = true;
        EXPECT_EQ(line->wordXpos(0), cssIndent < 0 ? cssIndent : 4 * spaces);
      });
      EXPECT_TRUE(sawLine);
    }
  }
  for (uint8_t spaces : {0, 2}) {
    BlockStyle style;
    style.alignment = CssTextAlign::Left;
    ParsedText text(false, false, style, spaces);
    text.addWord("word", EpdFontFamily::REGULAR);
    text.layoutAndExtractLines(
        renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, auto) { EXPECT_EQ(line->wordXpos(0), 4 * spaces); });
  }
}

TEST(ParagraphIndentation, PreservesAlignmentEligibilityAndScaledSpaceRounding) {
  GfxRenderer renderer;
  for (const auto alignment : {CssTextAlign::Left, CssTextAlign::Center}) {
    for (uint8_t spaces : {0, 2}) {
      BlockStyle style;
      style.alignment = alignment;
      style.textIndentDefined = true;
      style.textIndent = 0;
      ParsedText text(false, false, style, spaces);
      text.addWord("word", EpdFontFamily::REGULAR);
      text.layoutAndExtractLines(renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, auto) {
        if (alignment == CssTextAlign::Left)
          EXPECT_EQ(line->wordXpos(0), 4 * spaces);
        else
          EXPECT_EQ(line->wordXpos(0), 84);
      });
    }
  }
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  ParsedText text(false, false, style, 2);
  text.addWord("word", EpdFontFamily::REGULAR);
  text.layoutAndExtractLines(
      renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, auto) { EXPECT_EQ(line->wordXpos(0), 6); }, true, 0, 75);
}

TEST(ParagraphIndentation, ReducesOnlyFirstLineAvailableWidth) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  for (uint8_t spaces : {1, 2, 5}) {
    ParsedText text(false, false, style, spaces);
    text.addWord("ab", EpdFontFamily::REGULAR);
    text.addWord("cd", EpdFontFamily::REGULAR);
    unsigned lines = 0;
    text.layoutAndExtractLines(renderer, 0, 40, [&](std::unique_ptr<TextBlock>, auto) { ++lines; });
    EXPECT_EQ(lines, spaces == 1 ? 1u : 2u);
  }
}

TEST(TextSpacingLayout, TrackingSeparatesCjkTokensAndScalesWordSpaces) {
  GfxRenderer renderer;
  for (bool hyphenation : {false, true}) {
    BlockStyle style;
    style.alignment = CssTextAlign::Left;
    style.textIndentDefined = true;
    ParsedText text(hyphenation, false, style, 0);
    text.addWord("一二三", EpdFontFamily::REGULAR);
    text.addWord("四五", EpdFontFamily::REGULAR);
    unsigned lines = 0;
    text.layoutAndExtractLines(
        renderer, 0, 200,
        [&](std::unique_ptr<TextBlock> line, auto) {
          ++lines;
          ASSERT_EQ(line->wordCount(), 5);
          EXPECT_EQ(line->wordXpos(0), 0);
          EXPECT_EQ(line->wordXpos(1), 7);  // 8 px glyph, -1 px tracking
          EXPECT_EQ(line->wordXpos(2), 14);
          // 8 px glyph plus 150% of a 4 px space, and the tracking on both sides of the space: the reader
          // of v1.0.76 spaced every glyph advance, the space's neighbours included (ParsedText::spaceGap).
          EXPECT_EQ(line->wordXpos(3), 26);
          EXPECT_EQ(line->wordXpos(4), 33);
        },
        true, -1, 150);
    EXPECT_EQ(lines, 1u);
  }
  EXPECT_EQ(renderer.getTextAdvanceX(0, "ab", EpdFontFamily::REGULAR), 16);
  EXPECT_EQ(renderer.getSpaceWidth(0, EpdFontFamily::REGULAR), 4);
}

TEST(TextSpacingLayout, WordSpacingChangesWrapThreshold) {
  GfxRenderer renderer;
  for (uint8_t percent : {50, 100, 125, 200}) {
    BlockStyle style;
    style.alignment = CssTextAlign::Left;
    style.textIndentDefined = true;
    ParsedText text(false, false, style, 0);
    text.addWord("ab", EpdFontFamily::REGULAR);
    text.addWord("cd", EpdFontFamily::REGULAR);
    unsigned lines = 0;
    text.layoutAndExtractLines(renderer, 0, 36, [&](std::unique_ptr<TextBlock>, auto) { ++lines; }, true, 0, percent);
    EXPECT_EQ(lines, percent > 100 ? 2u : 1u);  // 16 + 16 + scaled 4 px space
  }
}

TEST(TextSpacingLayout, CachedPageRestoresSpacing) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  style.textIndentDefined = true;
  ParsedText text(false, false, style);
  text.addWord("一二三", EpdFontFamily::REGULAR);
  text.addWord("四五", EpdFontFamily::REGULAR);
  const auto path = (std::filesystem::temp_directory_path() / "crosspoint-text-spacing.bin").string();
  unsigned lines = 0;
  text.layoutAndExtractLines(
      renderer, 0, 200,
      [&](std::unique_ptr<TextBlock> line, auto) {
        ++lines;
        Page page;
        page.elements.push_back(std::make_unique<PageLine>(std::move(line), 4, 12));
        const auto* original = static_cast<const PageLine&>(*page.elements[0]).getBlock();
        {
          HalFile file;
          ASSERT_TRUE(file.open(path.c_str(), "wb"));
          ASSERT_TRUE(page.serialize(file));
        }
        HalFile file;
        ASSERT_TRUE(file.open(path.c_str(), "rb"));
        auto cachedPage = Page::deserialize(file);
        ASSERT_NE(cachedPage, nullptr);
        ASSERT_EQ(cachedPage->elements.size(), 1);
        const auto* cached = static_cast<const PageLine&>(*cachedPage->elements[0]).getBlock();
        ASSERT_NE(cached, nullptr);
        EXPECT_EQ(cached->getBlockStyle().characterSpacing, -2);
        ASSERT_EQ(cached->wordCount(), 5);
        EXPECT_EQ(cached->wordXpos(3) - cached->wordXpos(2), 6);  // 8 + half-width space, less the tracking on both sides
        EXPECT_EQ(file.position(), file.size());
        ASSERT_EQ(cached->wordCount(), original->wordCount());
        for (uint16_t i = 0; i < original->wordCount(); ++i) EXPECT_EQ(cached->wordXpos(i), original->wordXpos(i));
      },
      true, -2, 50);
  EXPECT_EQ(lines, 1u);
  std::filesystem::remove(path);
}

TEST_F(ChapterHtmlSlimParserTest, ParserAppliesTextSpacingToParagraphs) {
  parser.setTextSpacing(-1, 150);
  parser.beginParse();
  ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
  const std::string text = "\xe4\xb8\x80\xe4\xba\x8c\xe4\xb8\x89 \xe5\x9b\x9b\xe4\xba\x94";  // 一二三 四五
  ChapterHtmlSlimParser::characterData(&parser, text.c_str(), static_cast<int>(text.size()));
  ChapterHtmlSlimParser::endElement(&parser, "p");
  parser.makePages();
  ASSERT_NE(parser.currentPage, nullptr);
  unsigned lines = 0;
  for (const auto& element : parser.currentPage->elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto& block = *static_cast<const PageLine&>(*element).getBlock();
    ++lines;
    ASSERT_EQ(block.wordCount(), 5);
    EXPECT_EQ(block.getBlockStyle().characterSpacing, -1);
    EXPECT_EQ(block.wordXpos(1) - block.wordXpos(0), 7);   // 8 px glyph, -1 px tracking
    EXPECT_EQ(block.wordXpos(3) - block.wordXpos(2), 12);  // glyph plus 150% of a 4 px space, less 1 px tracking each side
  }
  EXPECT_EQ(lines, 1u);
}

TEST_F(ChapterHtmlSlimParserTest, NfdWordAcrossInlineStyleKeepsSourceRange) {
  parser.beginParse();
  ChapterHtmlSlimParser::startElement(&parser, "body", nullptr);
  ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
  const std::string prefix = "Cafe\xCC\x81";
  ChapterHtmlSlimParser::characterData(&parser, prefix.c_str(), static_cast<int>(prefix.size()));
  ChapterHtmlSlimParser::startElement(&parser, "b", nullptr);
  ChapterHtmlSlimParser::characterData(&parser, "s", 1);
  ChapterHtmlSlimParser::endElement(&parser, "b");
  ChapterHtmlSlimParser::endElement(&parser, "p");
  parser.makePages();
  ASSERT_NE(parser.currentPage, nullptr);
  std::string selected;
  uint32_t previousEnd = 0;
  unsigned words = 0;
  for (const auto& element : parser.currentPage->elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto& block = *static_cast<const PageLine&>(*element).getBlock();
    for (uint16_t i = 0; i < block.wordCount(); ++i) {
      const auto range = block.wordSourceRange(i);
      EXPECT_EQ(range.start, words == 0 ? 0u : 5u);
      EXPECT_EQ(range.end, words == 0 ? 5u : 6u);
      ASSERT_TRUE(clippingText::append(selected, block.wordText(i), range.start > previousEnd ? ' ' : '\0', 4096,
                                       block.wordHasDiscretionaryHyphen(i)));
      previousEnd = range.end;
      ++words;
    }
  }
  EXPECT_EQ(words, 2u);
  EXPECT_EQ(selected, "Caf\xC3\xA9s");
}

class ClippingParagraphTest : public ChapterHtmlSlimParserTest {};

TEST_P(ClippingParagraphTest, CachedBlocksSeparateAdjacentParagraphs) {
  EXPECT_EQ(parseToParagraphText(GetParam()), "one\ntwo");
}

TEST_F(ChapterHtmlSlimParserTest, TextBetweenBlocksStaysInItsOwnParagraph) {
  EXPECT_EQ(parseToParagraphText("<p>one</p>two<div>three</div>"), "one\ntwo\nthree");
}

INSTANTIATE_TEST_SUITE_P(ClippingBoundaries, ClippingParagraphTest,
                         ::testing::Values("<p>one</p><p>two</p>", "<p>one</p>\n  <p>two</p>",
                                           "<div>one</div><div>two</div>", "<div>one</div>\n  <div>two</div>",
                                           "<p>one<br/>two</p>", "<p>o<b>ne</b></p><p>two</p>",
                                           "<p>one</p><p>&#160;two</p>", "<p>one</p><p>&#8239;two</p>",
                                           "one<div>two</div>", "<div>one<div>two</div></div>",
                                           "<div>one<p>two</p></div>", "<div>one<h3>two</h3></div>"));

TEST(TextSpacingLayout, ParagraphMarkerSurvivesBidiAndOnlyMarksFirstExtractedLine) {
  GfxRenderer renderer;
  for (const bool focus : {false, true}) {
    BlockStyle style;
    style.isRtl = true;
    style.directionDefined = true;
    style.alignment = CssTextAlign::Right;
    ParsedText text(false, focus, style, 0);
    text.addWord("אחד", EpdFontFamily::REGULAR);
    text.addWord("alpha", EpdFontFamily::REGULAR);
    text.addWord("beta", EpdFontFamily::REGULAR);
    text.addWord("שני", EpdFontFamily::REGULAR);
    unsigned lines = 0;
    unsigned starts = 0;
    auto inspect = [&](std::unique_ptr<TextBlock> block, auto) {
      for (uint16_t i = 0; i < block->wordCount(); ++i) {
        if (!block->wordStartsParagraph(i)) continue;
        EXPECT_EQ(lines, 0u);
        EXPECT_STREQ(block->wordText(i), "אחד");
        ++starts;
      }
      ++lines;
    };
    text.layoutAndExtractLines(renderer, 0, 100, inspect, false);
    ASSERT_GT(lines, 0u);
    ASSERT_GT(text.size(), 0u);
    text.layoutAndExtractLines(renderer, 0, 100, inspect);
    EXPECT_EQ(starts, 1u);
    EXPECT_GT(lines, 1u);
  }
}

TEST(KoreanLayout, HangulWordsStayWholeAndWrapAtSpaces) {
  GfxRenderer renderer;
  {
    BlockStyle style;
    style.alignment = CssTextAlign::Left;
    style.textIndentDefined = true;
    ParsedText text(false, false, style, 0);
    text.addWord("가나다", EpdFontFamily::REGULAR);
    text.addWord("라마", EpdFontFamily::REGULAR);
    text.addWord("3개를", EpdFontFamily::REGULAR);
    text.addWord("iPhone을", EpdFontFamily::REGULAR);
    std::vector<std::vector<std::string>> lines;
    text.layoutAndExtractLines(renderer, 0, 60, [&](std::unique_ptr<TextBlock> line, auto) {
      auto& words = lines.emplace_back();
      for (uint16_t i = 0; i < line->wordCount(); ++i) words.emplace_back(line->wordText(i));
    });
    // 가나다 라마 is 24 + 4 + 16 px; adding 3개를 would need 72 px, and no break exists inside it.
    const std::vector<std::vector<std::string>> expected{{"가나다", "라마"}, {"3개를"}, {"iPhone을"}};
    EXPECT_EQ(lines, expected);
  }
}

TEST(KoreanLayout, JustifiedHangulStretchesOnlyWordSpaces) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Justify;
  style.textIndentDefined = true;
  ParsedText text(false, false, style, 0);
  for (const char* word : {"가나", "다라", "마바", "사아"}) text.addWord(word, EpdFontFamily::REGULAR);
  unsigned lines = 0;
  text.layoutAndExtractLines(renderer, 0, 60, [&](std::unique_ptr<TextBlock> line, auto) {
    if (lines++ != 0) return;
    // 3 x 16 px words + 2 x 4 px spaces leave 4 px, split across the two spaces only.
    ASSERT_EQ(line->wordCount(), 3);
    EXPECT_EQ(line->wordXpos(0), 0);
    EXPECT_EQ(line->wordXpos(1), 22);
    EXPECT_EQ(line->wordXpos(2), 44);
  });
  EXPECT_EQ(lines, 2u);
}

TEST(KoreanLayout, HangulGluedAcrossInlineStyleIsUnbreakable) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Justify;
  style.textIndentDefined = true;
  ParsedText text(false, false, style);
  text.addWord("가나", EpdFontFamily::REGULAR);
  text.addWord("한국", EpdFontFamily::REGULAR);
  text.addWord("어", EpdFontFamily::BOLD, false, /*attachToPrevious=*/true);
  std::vector<std::vector<std::string>> lines;
  text.layoutAndExtractLines(renderer, 0, 40, [&](std::unique_ptr<TextBlock> line, auto) {
    auto& words = lines.emplace_back();
    for (uint16_t i = 0; i < line->wordCount(); ++i) words.emplace_back(line->wordText(i));
  });
  // 가나 한국 fits in 36 px, but 어 is glued to 한국, so the whole word moves down.
  const std::vector<std::vector<std::string>> expected{{"가나"}, {"한국", "어"}};
  EXPECT_EQ(lines, expected);
}

TEST(TextDecorationPosition, StrikeUsesGlyphBodyAndFollowsScriptScaling) {
  GfxRenderer renderer;
  constexpr int top = 20;
  for (const auto script : {EpdFontFamily::REGULAR, EpdFontFamily::SUP, EpdFontFamily::SUB}) {
    ParsedText text(false, false, BlockStyle{}, 0);
    text.addWord("example", static_cast<EpdFontFamily::Style>(EpdFontFamily::STRIKETHROUGH | script));
    text.layoutAndExtractLines(renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, uint32_t) {
      renderer.drawnLineCount = 0;
      line->render(renderer, 0, 0, top);
      ASSERT_EQ(renderer.drawnLineCount, 1);
      // Fixture baseline is top + 12; glyph body is 10 pixels, halved for scripts.
      const int expected = script == EpdFontFamily::SUP ? 26 : script == EpdFontFamily::SUB ? 33 : 27;
      EXPECT_EQ(renderer.lastLineY, expected);
    });
  }
}

TEST(TextDecorationPosition, UnderlineStaysBelowBaseline) {
  GfxRenderer renderer;
  ParsedText text(false, false, BlockStyle{}, 0);
  text.addWord("example", EpdFontFamily::UNDERLINE);
  text.layoutAndExtractLines(renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, uint32_t) {
    line->render(renderer, 0, 0, 20);
    EXPECT_EQ(renderer.lastLineY, 34);
  });
}

namespace {
// Xep 6 tu, moi tu mot dong (khung rong 40 px, tu "Alpha" rong 40), dem so dong o trang dau.
unsigned linesOnFirstPage(ChapterHtmlSlimParser& parser, const float compression, const int viewportHeight) {
  parser.lineCompression = compression;
  parser.viewportHeight = viewportHeight;
  parser.viewportWidth = 40;
  parser.currentPage.reset();
  parser.currentTextBlock.reset();
  parser.khoiTruocDaXepTrang = false;  // goi lai trong cung bai kiem: khong cong khoang ngan doan o dau
  parser.blockStyleStack.push_back(BlockStyle());
  unsigned firstPageLines = 0;
  bool first = true;
  parser.completePageFn = [&](std::unique_ptr<Page> page, auto, auto, auto) {
    if (first) firstPageLines = page->elements.size();
    first = false;
  };
  ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
  for (int i = 0; i < 6; ++i) parser.currentTextBlock->addWord("Alpha", EpdFontFamily::REGULAR);
  parser.makePages();
  return firstPageLines;
}
}  // namespace


TEST_F(ChapterHtmlSlimParserTest, PlainLineBreaksDoNotChargeLeadingGapTwice) {
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  style.textAlignDefined = true;
  style.textIndentDefined = true;
  style.textIndent = 12;
  ParsedText text(false, false, style, readerSpacing::indentSpaces(1));
  text.addWord("a", EpdFontFamily::REGULAR);
  text.addWord("b", EpdFontFamily::REGULAR);
  text.addWord("c", EpdFontFamily::REGULAR);

  std::vector<unsigned> lineWordCounts;
  ASSERT_TRUE(text.layoutAndExtractLines(renderer, 0, 20, [&](std::unique_ptr<TextBlock> line, auto) {
    lineWordCounts.push_back(line->wordCount());
  }));
  ASSERT_EQ(lineWordCounts.size(), 2u);
  EXPECT_EQ(lineWordCounts[0], 1u);
  EXPECT_EQ(lineWordCounts[1], 2u);
}

TEST_F(ChapterHtmlSlimParserTest, PlainLineBreaksMatchUnannotatedRubyPath) {
  // Empty annotations exercise the established DP path with zero ruby overhang.
  // Compare every break and token after fallback hyphenation, including spacing
  // that can make a partial line wider than the complete suffix.
  uint32_t state = 0x109;
  for (unsigned sample = 0; sample < 1200; ++sample) {
    auto next = [&]() { state = state * 1664525u + 1013904223u; return state; };
    BlockStyle style;
    style.alignment = CssTextAlign::Left;
    style.textAlignDefined = true;
    const int spacing = static_cast<int>(next() % 17) - 8;
    const uint8_t indent = next() % 3;
    const uint8_t wordSpacing = next() % 5;
    style.characterSpacing = static_cast<int8_t>(spacing);
    // Word text lives in a move-only arena, so the annotated copy is built from the same draws.
    ParsedText plain(false, false, style, readerSpacing::indentSpaces(indent));
    ParsedText annotated(false, false, style, readerSpacing::indentSpaces(indent));
    plain.wordSpacingPercent = annotated.wordSpacingPercent =
        static_cast<uint8_t>(readerSpacing::wordPermille(wordSpacing) / 10);
    const size_t count = 2 + next() % 40;
    for (size_t i = 0; i < count; ++i) {
      const std::string word(1 + next() % 14, static_cast<char>('a' + next() % 26));
      const auto wordStyle = static_cast<EpdFontFamily::Style>(next() % 4);
      plain.addWord(word, wordStyle);
      annotated.addWord(word, wordStyle);
      plain.wordContinues[i] = annotated.wordContinues[i] = (next() & 8) != 0;
      plain.wordNoSpaceBefore[i] = annotated.wordNoSpaceBefore[i] = (next() & 8) != 0;
    }
    annotated.rubyTexts.resize(count);
    auto plainWidths = plain.calculateWordWidths(renderer, 0);
    auto annotatedWidths = annotated.calculateWordWidths(renderer, 0);
    const int pageWidth = sample % 11 == 0 ? 100000 : 8 + next() % 300;
    SCOPED_TRACE(::testing::Message() << "sample=" << sample << " width=" << pageWidth
                                    << " letterSpacing=" << spacing);
    const auto expected = annotated.computeLineBreaks(renderer, 0, pageWidth, annotatedWidths,
                                                       annotated.wordContinues, annotated.wordNoSpaceBefore);
    const auto actual = plain.computeLineBreaks(renderer, 0, pageWidth, plainWidths,
                                               plain.wordContinues, plain.wordNoSpaceBefore);
    ASSERT_EQ(actual, expected);
    ASSERT_EQ(plain.words.size(), annotated.words.size());
    for (size_t i = 0; i < plain.words.size(); ++i) ASSERT_EQ(plain.wordAt(i), annotated.wordAt(i));
    ASSERT_EQ(plainWidths, annotatedWidths);
  }
}

TEST_F(ChapterHtmlSlimParserTest, TextRunningIntoABlockIsNotGluedToIt) {
  for (const char* tag : {"div", "h3", "p"}) {
    parser.beginParse();
    parser.currentTextBlock = std::make_unique<ParsedText>();
    ChapterHtmlSlimParser::startElement(&parser, "div", nullptr);
    ChapterHtmlSlimParser::characterData(&parser, "two", 3);
    ChapterHtmlSlimParser::startElement(&parser, tag, nullptr);
    ChapterHtmlSlimParser::characterData(&parser, "three", 5);
    ASSERT_EQ(std::string(parser.partWordBuffer, parser.partWordBufferIndex), "three") << tag;
    ASSERT_EQ(parser.currentTextBlock->size(), 0u) << tag;
  }
}

TEST_F(ChapterHtmlSlimParserTest, ForcedIndentCoexistsWithParagraphSpacingAndOnlyIndentsFirstLine) {
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  style.textAlignDefined = true;
  style.textIndentDefined = true;
  style.textIndent = 0;
  ParsedText text(false, false, style, readerSpacing::indentSpaces(1));
  for (int i = 0; i < 15; ++i) text.addWord("word", EpdFontFamily::REGULAR);
  unsigned lines = 0;
  text.layoutAndExtractLines(renderer, 0, 100, [&](std::unique_ptr<TextBlock> line, auto) {
    ASSERT_GT(line->wordCount(), 0u);
    EXPECT_EQ(line->wordXpos(0), lines == 0 ? 12 : 0);
    ++lines;
  });
  EXPECT_GT(lines, 2u);
}

TEST_F(ChapterHtmlSlimParserTest, IndentSizesOverrideCssAndParagraphGap) {
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  style.textAlignDefined = true;
  style.textIndentDefined = true;
  for (const int cssIndent : {-8, 0, 20}) {
    style.textIndent = cssIndent;
    for (const uint8_t mode : {0, 1, 2}) {
      ParsedText text(false, false, style, readerSpacing::indentSpaces(mode));
      text.addWord("word", EpdFontFamily::REGULAR);
      // A hanging (negative) CSS indent stays; the forced width replaces any other.
      int expected = cssIndent < 0 ? cssIndent : mode == 2 ? 24 : mode == 1 ? 12 : 0;
      text.layoutAndExtractLines(renderer, 0, 100, [&](std::unique_ptr<TextBlock> line, auto) {
        ASSERT_EQ(line->wordCount(), 1u);
        EXPECT_EQ(line->wordXpos(0), expected) << "mode=" << +mode << " css=" << cssIndent;
      });
    }
  }
}

TEST_F(ChapterHtmlSlimParserTest, IndentPreservesCenteredTextAndUsesRtlLeadingEdge) {
  BlockStyle style;
  style.textAlignDefined = true;
  style.directionDefined = true;
  style.alignment = CssTextAlign::Center;
  for (const uint8_t mode : {0, 1, 2}) {
    ParsedText text(false, false, style, readerSpacing::indentSpaces(mode));
    text.addWord("word", EpdFontFamily::REGULAR);
    text.layoutAndExtractLines(renderer, 0, 100,
                               [&](std::unique_ptr<TextBlock> line, auto) { EXPECT_EQ(line->wordXpos(0), 34); });
  }
  style.isRtl = true;
  style.alignment = CssTextAlign::Right;
  ParsedText text(false, false, style, readerSpacing::indentSpaces(1));
  text.addWord("word", EpdFontFamily::REGULAR);
  text.layoutAndExtractLines(renderer, 0, 100, [&](std::unique_ptr<TextBlock> line, auto) {
    EXPECT_EQ(line->wordXpos(0) + renderer.getTextWidth(0, "word", EpdFontFamily::REGULAR), 88);
  });
}

TEST_F(ChapterHtmlSlimParserTest, ParserPropagatesIndentChoiceToNewParagraphs) {
  parser.currentTextBlock.reset();
  parser.setParagraphIndentSpaces(readerSpacing::indentSpaces(1));
  parser.extraParagraphSpacing = readerSpacing::WIDE;
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  style.textAlignDefined = true;
  parser.startNewTextBlock(style);
  ASSERT_NE(parser.currentTextBlock, nullptr);
  parser.currentTextBlock->addWord("word", EpdFontFamily::REGULAR);
  parser.currentTextBlock->layoutAndExtractLines(
      renderer, 0, 100, [&](std::unique_ptr<TextBlock> line, auto) { EXPECT_EQ(line->wordXpos(0), 12); });
}

TEST_F(ChapterHtmlSlimParserTest, OpeningParagraphUsesOneLargeInitialInsteadOfWordPrefixes) {
  parser.dropCapMode = readerSpacing::DROP_CAP_LARGE;
  parser.viewportWidth = 160;
  parser.currentTextBlock.reset();
  parser.blockStyleStack.push_back(BlockStyle());
  ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
  for (int i = 0; i < 20; ++i) parser.currentTextBlock->addWord(i == 0 ? "Alpha" : "word", EpdFontFamily::REGULAR);
  parser.makePages();
  ASSERT_TRUE(parser.currentPage);
  ASSERT_GE(parser.currentPage->elements.size(), 3u);
  const auto& first = *static_cast<PageLine&>(*parser.currentPage->elements[0]).getBlock();
  EXPECT_GT(first.getDropCapHeight(), 0);
  for (const auto& element : parser.currentPage->elements) {
    const auto& block = *static_cast<PageLine&>(*element).getBlock();
    for (uint16_t i = 0; i < block.wordCount(); ++i) EXPECT_EQ(block.focusBoundary(i), 0);
  }
  const auto& second = *static_cast<PageLine&>(*parser.currentPage->elements[1]).getBlock();
  const auto& third = *static_cast<PageLine&>(*parser.currentPage->elements[2]).getBlock();
  EXPECT_GT(second.wordXpos(0), third.wordXpos(0));
}

TEST_F(ChapterHtmlSlimParserTest, DropCapDoesNotRepeatAndTocResetsIt) {
  parser.dropCapMode = readerSpacing::DROP_CAP_LARGE;
  parser.currentTextBlock.reset();
  parser.blockStyleStack.push_back(BlockStyle());
  auto paragraph = [&](const char* tag, const char* text) {
    ChapterHtmlSlimParser::startElement(&parser, tag, nullptr);
    ChapterHtmlSlimParser::characterData(&parser, text, strlen(text));
    ChapterHtmlSlimParser::endElement(&parser, tag);
  };
  paragraph("h1", "Chapter One");
  paragraph("p", "");
  paragraph("p", "Alpha");
  paragraph("p", "Beta");
  ASSERT_EQ(parser.currentPage->elements.size(), 3u);
  auto block = [&](int i) -> const TextBlock& {
    return *static_cast<PageLine&>(*parser.currentPage->elements[i]).getBlock();
  };
  EXPECT_EQ(block(0).getDropCapHeight(), 0);
  EXPECT_GT(block(1).getDropCapHeight(), 0);
  EXPECT_EQ(block(2).getDropCapHeight(), 0);
  EXPECT_GE(parser.currentPage->elements[2]->yPos - parser.currentPage->elements[1]->yPos, 32);
  parser.tocAnchors = std::make_shared<const std::vector<uint32_t>>(1, ChapterHtmlSlimParser::anchorKey("next", 4));
  parser.pendingAnchorId = "next";
  parser.completePageFn = [](auto, auto, auto, auto) {};
  paragraph("p", "Gamma");
  ASSERT_EQ(parser.currentPage->elements.size(), 1u);
  EXPECT_GT(block(0).getDropCapHeight(), 0);
}

// Luat day trang do bang MUC (ascender 12 + descender 6 = 18), khong do bang o dong.
// Gian dong hep (o 16 < muc 18): dong thu ba o y = 32 co o vua khung 49 nhung muc cham 50, phai sang trang.
TEST_F(ChapterHtmlSlimParserTest, LastLineBreaksWhenInkOverflowsEvenIfLineBoxFits) {
  EXPECT_EQ(linesOnFirstPage(parser, 1.0f, 49), 2u);
  EXPECT_EQ(linesOnFirstPage(parser, 1.0f, 50), 3u);
}

TEST_F(ChapterHtmlSlimParserTest, LastLineStaysWhenInkFitsEvenIfLineBoxOverflows) {
  EXPECT_EQ(linesOnFirstPage(parser, 1.5f, 42), 2u);
  EXPECT_EQ(linesOnFirstPage(parser, 1.5f, 41), 1u);
}

TEST_F(ChapterHtmlSlimParserTest, DropCapKeepsTwoLinesOnSamePage) {
  parser.dropCapMode = readerSpacing::DROP_CAP_LARGE;
  parser.viewportHeight = 64;
  parser.viewportWidth = 160;
  parser.currentPage = std::make_unique<Page>();
  parser.currentPageNextY = 48;
  parser.currentTextBlock.reset();
  parser.blockStyleStack.push_back(BlockStyle());
  unsigned completed = 0;
  parser.completePageFn = [&](auto, auto, auto, auto) { ++completed; };
  ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
  for (int i = 0; i < 6; ++i) parser.currentTextBlock->addWord("Alpha", EpdFontFamily::REGULAR);
  parser.makePages();
  EXPECT_EQ(completed, 1u);
  ASSERT_TRUE(parser.currentPage);
  EXPECT_EQ(parser.currentPage->elements.front()->yPos, 0);
}

TEST_F(ChapterHtmlSlimParserTest, DropCapStreamingRetainsSecondLineInsetAndWholeText) {
  ParsedText text;
  text.enableDropCap(28);
  text.addWord("Alpha", EpdFontFamily::REGULAR);
  text.addWord("word", EpdFontFamily::REGULAR);
  text.addWord("word", EpdFontFamily::REGULAR);
  std::vector<std::unique_ptr<TextBlock>> lines;
  auto emit = [&](auto line, auto) { lines.push_back(std::move(line)); };
  text.layoutAndExtractLines(renderer, 0, 100, emit, false);
  ASSERT_EQ(lines.size(), 1u);
  for (int i = 0; i < 10; ++i) text.addWord("word", EpdFontFamily::REGULAR);
  text.layoutAndExtractLines(renderer, 0, 100, emit);
  ASSERT_GE(lines.size(), 3u);
  EXPECT_GT(lines[1]->wordXpos(0), lines[2]->wordXpos(0));
  size_t words = 0, caps = 0;
  for (auto& line : lines) {
    words += line->wordCount();
    caps += line->getDropCapHeight() > 0;
  }
  EXPECT_EQ(words, 13u);
  EXPECT_EQ(caps, 1u);
}

TEST_F(ChapterHtmlSlimParserTest, DropCapNormalizesVietnameseAndLeavesUnsupportedClusterWhole) {
  ParsedText text;
  text.enableDropCap(28);
  text.addWord("A\u0302\u0301n", EpdFontFamily::REGULAR);
  text.layoutAndExtractLines(renderer, 0, 200, [&](auto line, auto) {
    EXPECT_STREQ(line->wordText(0), "Ấn");
    EXPECT_GT(line->getDropCapHeight(), 0);
  });
  EXPECT_EQ(dropcap::initial("\"Đêm").codepoint, 0x110u);
  EXPECT_EQ(dropcap::initial("123").codepoint, 0u);
  EXPECT_EQ(dropcap::initial("日").codepoint, 0u);
  EXPECT_EQ(dropcap::initial("A\u035c").codepoint, 0u);
}

TEST_F(ChapterHtmlSlimParserTest, LetterSpacingChangesLineBreaksAndIsCarriedIntoEachLine) {
  BlockStyle style;
  style.textAlignDefined = true;
  style.alignment = CssTextAlign::Left;
  for (const int8_t spacing : {-1, 0, 1}) {
    ParsedText text(false, false, style, 0);
    text.addWord("aaaaa", EpdFontFamily::REGULAR);
    text.addWord("bbbbb", EpdFontFamily::REGULAR);
    size_t count = 0;
    text.layoutAndExtractLines(
        renderer, 0, 84,
        [&](std::unique_ptr<TextBlock> line, auto) {
          ++count;
          EXPECT_EQ(line->getBlockStyle().characterSpacing, spacing);
          if (line->wordCount() == 2) EXPECT_EQ(line->wordXpos(1), 44 + 6 * spacing);
        },
        true, spacing, 100);
    EXPECT_EQ(count, spacing == 1 ? 2u : 1u);
  }
}

TEST_F(ChapterHtmlSlimParserTest, ParagraphModesAddThreeDistinctGaps) {
  // The parser now consumes a resolved gap in pixels, so the three legacy modes
  // are compared through readerSpacing::paragraphGap instead of raw ordinals.
  constexpr int kLineHeight = 16;
  auto paragraph = [&](const char* word) {
    parser.currentTextBlock = std::make_unique<ParsedText>();
    parser.currentTextBlock->addWord(word, EpdFontFamily::REGULAR);
    parser.makePages();
  };
  for (const uint8_t mode : {readerSpacing::LEVEL_DEFAULT, readerSpacing::WIDE, readerSpacing::VERY_WIDE}) {
    parser.extraParagraphSpacing = mode;
    parser.currentPageNextY = 0;
    parser.currentPage.reset();
    parser.khoiTruocDaXepTrang = false;
    paragraph("word");
    // The gap belongs to the boundary BETWEEN two paragraphs, so the opening
    // paragraph must not be preceded by one.
    EXPECT_EQ(parser.currentPageNextY, kLineHeight);
    paragraph("word");
    // ...and the second paragraph is advanced by the line height plus the gap
    // of this mode, measured from the end of the first paragraph's line.
    EXPECT_EQ(parser.currentPageNextY, 2 * kLineHeight + readerSpacing::paragraphGap(mode, kLineHeight));
  }
  // The three legacy modes must stay distinct from one another.
  EXPECT_LT(readerSpacing::paragraphGap(readerSpacing::LEVEL_DEFAULT, kLineHeight),
            readerSpacing::paragraphGap(readerSpacing::WIDE, kLineHeight));
  EXPECT_LT(readerSpacing::paragraphGap(readerSpacing::WIDE, kLineHeight),
            readerSpacing::paragraphGap(readerSpacing::VERY_WIDE, kLineHeight));
}

TEST_F(ChapterHtmlSlimParserTest, SoftFlushedParagraphMatchesOnePassLayout) {
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  style.textAlignDefined = true;
  style.marginTop = 7;
  auto layout = [&](const bool softFlush) {
    parser.viewportWidth = 100;
    parser.viewportHeight = 30000;
    parser.extraParagraphSpacing = readerSpacing::WIDE;
    parser.embeddedStyle = false;
    parser.currentPage.reset();
    parser.currentPageNextY = 0;
    parser.khoiTruocDaXepTrang = true;  // a paragraph came before, so this one opens with the gap
    parser.wordsExtractedInBlock = 0;
    parser.currentTextBlock = std::make_unique<ParsedText>(false, false, style, readerSpacing::indentSpaces(1));
    for (int i = 0; i < 760; ++i) parser.currentTextBlock->addWord("word", EpdFontFamily::REGULAR);
    if (softFlush) {
      parser.flushTextIfOverBudget();
      EXPECT_LT(parser.currentTextBlock->size(), 760u);  // the flush did lay out a chunk
    }
    for (int i = 0; i < 20; ++i) parser.currentTextBlock->addWord("word", EpdFontFamily::REGULAR);
    parser.makePages();
    std::vector<std::pair<int, int>> lines;  // y of each line, x of its first word
    for (const auto& element : parser.currentPage->elements) {
      const auto* line = static_cast<const PageLine*>(element.get());
      lines.emplace_back(line->yPos, line->getBlock()->wordXpos(0));
    }
    return lines;
  };
  const auto onePass = layout(false);
  const auto flushed = layout(true);
  ASSERT_GT(onePass.size(), 100u);
  EXPECT_EQ(onePass.front().second, 12);  // the first line keeps its indent
  ASSERT_EQ(flushed.size(), onePass.size());
  size_t firstDiff = 0;
  while (firstDiff < onePass.size() && flushed[firstDiff] == onePass[firstDiff]) ++firstDiff;
  ASSERT_EQ(firstDiff, onePass.size()) << "line " << firstDiff << ": y " << flushed[firstDiff].first << " vs "
                                       << onePass[firstDiff].first << ", x " << flushed[firstDiff].second << " vs "
                                       << onePass[firstDiff].second;
}

TEST_F(ChapterHtmlSlimParserTest, ForcedTocAnchorStartsFreshPageWithoutGapAndKeepsAnchorOffsetAndDropCap) {
  const auto fixturePath = std::filesystem::temp_directory_path() / "crosspoint-forced-toc-anchor.xhtml";
  {
    std::ofstream fixture(fixturePath);
    ASSERT_TRUE(fixture.is_open());
    fixture << R"(<?xml version="1.0" encoding="UTF-8"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>Forced anchor</title></head><body><p>Alpha</p><p id="toc-next">Beta</p></body></html>)";
  }

  GfxRenderer parserRenderer;
  CssParser parserCss{"/tmp"};
  std::string filepath = fixturePath.string();
  std::vector<uint32_t> pageOffsets;
  std::vector<int16_t> lineY;
  std::vector<uint16_t> dropCapHeights;
  ChapterHtmlSlimParser actualParser{
      nullptr,
      filepath,
      parserRenderer,
      0,
      1.0f,
      readerSpacing::WIDE,
      static_cast<uint8_t>(CssTextAlign::Left),
      static_cast<uint16_t>(parserRenderer.getScreenWidth()),
      static_cast<uint16_t>(parserRenderer.getScreenHeight()),
      false,
      false,
      readerSpacing::DROP_CAP_LARGE,
      [&](std::unique_ptr<Page> page, uint16_t, uint16_t, uint32_t visibleOffset) {
        ASSERT_NE(page, nullptr);
        pageOffsets.push_back(visibleOffset);
        for (const auto& element : page->elements) {
          if (element->getTag() != TAG_PageLine) continue;
          const auto& pageLine = static_cast<const PageLine&>(*element);
          lineY.push_back(pageLine.yPos);
          dropCapHeights.push_back(pageLine.getBlock()->getDropCapHeight());
        }
      },
      true,
      "",
      "",
      0,
      std::make_shared<const std::vector<uint32_t>>(1, ChapterHtmlSlimParser::anchorKey("toc-next", 8)),
      nullptr,
      &parserCss};
  actualParser.setParagraphIndentSpaces(readerSpacing::indentSpaces(2));

  ASSERT_TRUE(actualParser.parseAndBuildPages());
  std::error_code removeError;
  std::filesystem::remove(fixturePath, removeError);

  ASSERT_EQ(pageOffsets, (std::vector<uint32_t>{0, 5}));
  ASSERT_EQ(lineY, (std::vector<int16_t>{0, 0}));
  ASSERT_EQ(dropCapHeights, (std::vector<uint16_t>{28, 28}));
  ASSERT_EQ(actualParser.getAnchors().size(), 1u);
  EXPECT_EQ(actualParser.getAnchors()[0].first, "toc-next");
  EXPECT_EQ(actualParser.getAnchors()[0].second, 1u);
}

TEST_F(ChapterHtmlSlimParserTest, AllocationFailureRejectsChapterAndRetryConservesEveryWord) {
  const auto path = std::filesystem::temp_directory_path() / "crosspoint-parser-oom.xhtml";
  std::vector<std::string> expected;
  {
    std::ofstream output(path);
    output << "<html><body>";
    for (int i = 0; i < 100; ++i) {
      expected.push_back("word" + std::to_string(i));
      output << "<p>" << expected.back() << "</p>";
    }
    output << "</body></html>";
  }
  const std::string inputPath = path.string();
  // The zero-size case targets the next nothrow array allocation: the word arena's chunk or the
  // TextBlock arena. Both, and ParsedText, are retried once after cache eviction, so they fail twice.
  for (const size_t allocationSize : {sizeof(TextBlock), sizeof(PageLine), sizeof(Page), sizeof(ParsedText), size_t{0}}) {
    const unsigned failures = allocationSize == 0 || allocationSize == sizeof(ParsedText) ? 2u : 1u;
    for (const unsigned failAfterPage : {2u, 48u}) {
    SCOPED_TRACE(allocationSize);
    for (const bool inject : {true, false}) {
      parserAllocationFault::reset();
      std::vector<std::string> actual;
      unsigned emitted = 0;
      ChapterHtmlSlimParser attempt{nullptr, inputPath, renderer, 0, 1.0f, 0, 0, 160, 40,
          false, false, 0, [&](std::unique_ptr<Page> page, auto, auto, auto) {
            ++emitted;
            if (page) for (const auto& element : page->elements) {
              if (element->getTag() != TAG_PageLine) continue;
              const auto& block = *static_cast<const PageLine&>(*element).getBlock();
              for (uint16_t i = 0; i < block.wordCount(); ++i) actual.push_back(block.wordText(i));
            }
            if (inject && emitted == failAfterPage) {
              parserAllocationFault::objectSize = allocationSize;
              parserAllocationFault::array = allocationSize == 0;
              parserAllocationFault::times = failures;
            }
          }, false, "", ""};
      const bool success = attempt.parseAndBuildPages();
      EXPECT_EQ(success, !inject);
      if (inject) {
        EXPECT_EQ(parserAllocationFault::hits, failures);
        const unsigned beforeFinish = emitted;
        EXPECT_FALSE(attempt.finishParse());
        EXPECT_EQ(emitted, beforeFinish);
      } else {
        EXPECT_EQ(actual, expected);
      }
      parserAllocationFault::reset();
    }
    }
  }
  std::filesystem::remove(path);
}

TEST_F(ChapterHtmlSlimParserTest, FailedTextBlockKeepsLayoutFailureSticky) {
  for (const bool focus : {false, true}) {
    ParsedText text(false, focus);
    text.addWord("conservation", EpdFontFamily::REGULAR);
    parserAllocationFault::reset();
    parserAllocationFault::array = true;
    parserAllocationFault::times = 2;  // the TextBlock arena is retried once after cache eviction
    unsigned emitted = 0;
    EXPECT_FALSE(text.layoutAndExtractLines(renderer, 0, 200, [&](auto, auto) { ++emitted; }));
    EXPECT_EQ(parserAllocationFault::hits, 2u);
    EXPECT_EQ(emitted, 0u);
    EXPECT_EQ(text.size(), 1u);
    EXPECT_FALSE(text.layoutAndExtractLines(renderer, 0, 200, [&](auto, auto) { ++emitted; }));
    EXPECT_FALSE(text.addWord("later", EpdFontFamily::REGULAR));
    parserAllocationFault::reset();
  }
}

TEST_F(ChapterHtmlSlimParserTest, OversizedRubyRejectsBuildAndPreservesSource) {
  const auto path = std::filesystem::temp_directory_path() / "crosspoint-parser-ruby-bound.xhtml";
  std::string manyTokens;
  for (unsigned i = 0; i < 257; ++i) manyTokens += "x ";
  std::string manyCjk;
  for (unsigned i = 0; i < 257; ++i) manyCjk += "漢";
  const std::vector<std::string> groups = {
      "base<rt>" + std::string(2049, 'a') + "</rt>",
      "base<rt>" + std::string(1200, 'a') + "</rt><rt>" + std::string(1200, 'b') + "</rt>",
      manyTokens + "<rt>reading</rt>",
      manyCjk + "<rt>reading</rt>",
      std::string(8193, 'x') + "<rt>reading</rt>"};
  const std::string inputPath = path.string();
  for (const auto& group : groups) {
    const std::string source = "<html><body><p><ruby>" + group + "</ruby></p></body></html>";
    { std::ofstream output(path); output << source; }
    ChapterHtmlSlimParser attempt{nullptr, inputPath, renderer, 0, 1.0f, 0, 0, 160, 40,
        false, false, 0, [](auto, auto, auto, auto) {}, false, "", ""};
    EXPECT_FALSE(attempt.parseAndBuildPages()) << group.size();
    EXPECT_FALSE(attempt.finishParse());
    std::ifstream input(path);
    const std::string retained{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    EXPECT_EQ(retained, source);
  }
  std::filesystem::remove(path);
}

TEST_F(ChapterHtmlSlimParserTest, SinkFailureRejectsPageCountAndFinalTail) {
  const auto path = std::filesystem::temp_directory_path() / "crosspoint-parser-sink-failure.xhtml";
  {
    std::ofstream output(path);
    output << "<html><body><p>one</p><p>two</p><p>three</p></body></html>trailing";
  }
  const std::string inputPath = path.string();
  for (const unsigned rejectAt : {1u, 2u}) {
    unsigned accepted = 0;
    unsigned offered = 0;
    ChapterHtmlSlimParser* active = nullptr;
    ChapterHtmlSlimParser attempt{nullptr, inputPath, renderer, 0, 1.0f, 0, 0, 160, 40,
        false, false, 0, [&](auto, auto, auto, auto) {
          if (++offered == rejectAt) active->failBuild();
          else ++accepted;
        }, false, "", ""};
    active = &attempt;
    EXPECT_FALSE(attempt.parseAndBuildPages());
    EXPECT_TRUE(attempt.hasFailed());
    EXPECT_EQ(attempt.completedPageCount, accepted);
    EXPECT_FALSE(attempt.finishParse());
    EXPECT_EQ(offered, rejectAt);
  }
  std::filesystem::remove(path);
}

TEST_F(ChapterHtmlSlimParserTest, ReadFailureIsStickyAndShortReadsConserveText) {
  const auto path = std::filesystem::temp_directory_path() / "crosspoint-parser-read-failure.xhtml";
  { std::ofstream output(path); output << "<html><body><p>one two three</p></body></html>"; }
  const std::string inputPath = path.string();
  for (const int result : {-1, 0, 1025}) {
    ChapterHtmlSlimParser attempt{nullptr, inputPath, renderer, 0, 1.0f, 0, 0, 160, 40,
        false, false, 0, [](auto, auto, auto, auto) { FAIL() << "Read failure emitted a page"; }, false, "", ""};
    ASSERT_TRUE(attempt.beginParse());
    HalFile::readResultOverride = result;
    EXPECT_EQ(attempt.parseStep(), ChapterHtmlSlimParser::ParseStatus::Error);
    EXPECT_TRUE(attempt.hasFailed());
    EXPECT_FALSE(attempt.finishParse());
  }
  std::vector<std::string> words;
  ChapterHtmlSlimParser attempt{nullptr, inputPath, renderer, 0, 1.0f, 0, 0, 160, 40,
      false, false, 0, [&](std::unique_ptr<Page> page, auto, auto, auto) {
        for (const auto& element : page->elements) {
          if (element->getTag() != TAG_PageLine) continue;
          const auto& block = *static_cast<const PageLine&>(*element).getBlock();
          for (uint16_t i = 0; i < block.wordCount(); ++i) words.emplace_back(block.wordText(i));
        }
      }, false, "", ""};
  HalFile::maxReadBytes = 3;
  EXPECT_TRUE(attempt.parseAndBuildPages());
  HalFile::maxReadBytes = SIZE_MAX;
  EXPECT_EQ(words, (std::vector<std::string>{"one", "two", "three"}));
  std::filesystem::remove(path);
}

TEST_F(ChapterHtmlSlimParserTest, RubyAtCapacityKeepsVietnameseCjkAndContinuation) {
  const auto path = std::filesystem::temp_directory_path() / "crosspoint-parser-ruby-capacity.xhtml";
  std::string cjk;
  for (unsigned i = 0; i < 256; ++i) cjk += "漢";
  const std::string source = "<html><body><p><ruby>" + cjk + "<rt>" + std::string(2048, 'a') +
      "</rt></ruby></p><p><ruby>Việt<rt>đọc</rt>Nam<rt>nam</rt></ruby></p></body></html>";
  { std::ofstream output(path); output << source; }
  const std::string inputPath = path.string();
  unsigned words = 0;
  unsigned annotationBytes = 0;
  ChapterHtmlSlimParser attempt{nullptr, inputPath, renderer, 0, 1.0f, 0, 0, 160, 40,
      false, false, 0, [&](std::unique_ptr<Page> page, auto, auto, auto) {
        for (const auto& element : page->elements) {
          if (element->getTag() != TAG_PageLine) continue;
          const auto& block = *static_cast<const PageLine&>(*element).getBlock();
          words += block.wordCount();
          for (uint16_t i = 0; i < block.wordCount(); ++i) annotationBytes += block.getRubyText(i).size();
        }
      }, false, "", ""};
  EXPECT_TRUE(attempt.parseAndBuildPages());
  EXPECT_EQ(words, 258u);
  EXPECT_EQ(annotationBytes, 2048u + std::string("đọcnam").size());
  std::filesystem::remove(path);
}

TEST_F(ChapterHtmlSlimParserTest, ConsecutiveRubyGroupsBoundRetainedTokensAndConserveText) {
  for (const bool css : {false, true}) {
  for (const bool hyphenation : {false, true}) {
  for (const std::string base : {std::string("漢"), std::string("word")}) {
    const auto path = std::filesystem::temp_directory_path() / "crosspoint-parser-many-ruby.xhtml";
    { std::ofstream output(path); output << "<html><body><p>";
      for (unsigned i = 0; i < 2000; ++i) output << "<ruby>" << base << "<rt>reading</rt></ruby>";
      output << "</p></body></html>";
    }
    const std::string inputPath = path.string();
    unsigned words = 0;
    unsigned annotations = 0;
    ChapterHtmlSlimParser attempt{nullptr, inputPath, renderer, 0, 1.0f, 0, 0, 160, 40,
        hyphenation, false, 0, [&](std::unique_ptr<Page> page, auto, auto, auto) {
          for (const auto& element : page->elements) {
            if (element->getTag() != TAG_PageLine) continue;
            const auto& block = *static_cast<const PageLine&>(*element).getBlock();
            words += block.wordCount();
            for (uint16_t i = 0; i < block.wordCount(); ++i) annotations += block.getRubyText(i) == "reading";
          }
        }, css, "", ""};
    ASSERT_TRUE(attempt.beginParse());
    size_t peakWords = 0;
    for (;;) {
      const auto state = attempt.parseStep();
      ASSERT_NE(state, ChapterHtmlSlimParser::ParseStatus::Error);
      if (attempt.currentTextBlock) peakWords = std::max(peakWords, attempt.currentTextBlock->size());
      if (state == ChapterHtmlSlimParser::ParseStatus::Done) break;
    }
    EXPECT_LE(peakWords, 1024u) << base;
    std::cout << "ruby sequence css=" << css << " hyphenation=" << hyphenation << " base=" << base << " peak retained words=" << peakWords << "\n";
    EXPECT_TRUE(attempt.finishParse());
    EXPECT_EQ(words, 2000u);
    EXPECT_EQ(annotations, 2000u);
    std::filesystem::remove(path);
  }
  }
  }
}

TEST_F(ChapterHtmlSlimParserTest, ManyLegalRubyAnnotationsBoundAggregateBytes) {
  const auto path = std::filesystem::temp_directory_path() / "crosspoint-parser-ruby-bytes.xhtml";
  { std::ofstream output(path); output << "<html><body><p>";
    for (unsigned i = 0; i < 100; ++i) output << "<ruby>漢<rt>" << std::string(2048, 'a') << "</rt></ruby>";
    output << "</p></body></html>";
  }
  const std::string inputPath = path.string();
  unsigned words = 0;
  unsigned annotationBytes = 0;
  ChapterHtmlSlimParser attempt{nullptr, inputPath, renderer, 0, 1.0f, 0, 0, 160, 40,
      false, false, 0, [&](std::unique_ptr<Page> page, auto, auto, auto) {
        for (const auto& element : page->elements) {
          if (element->getTag() != TAG_PageLine) continue;
          const auto& block = *static_cast<const PageLine&>(*element).getBlock();
          words += block.wordCount();
          for (uint16_t i = 0; i < block.wordCount(); ++i) annotationBytes += block.getRubyText(i).size();
        }
      }, false, "", ""};
  ASSERT_TRUE(attempt.beginParse());
  size_t peakAnnotationBytes = 0;
  for (;;) {
    const auto state = attempt.parseStep();
    ASSERT_NE(state, ChapterHtmlSlimParser::ParseStatus::Error);
    if (attempt.currentTextBlock) {
      size_t bytes = 0;
      for (const auto& ruby : attempt.currentTextBlock->rubyTexts) bytes += ruby.size();
      EXPECT_EQ(attempt.currentTextBlock->getRubyTextBytes(), bytes);
      peakAnnotationBytes = std::max(peakAnnotationBytes, bytes);
    }
    if (state == ChapterHtmlSlimParser::ParseStatus::Done) break;
  }
  EXPECT_LE(peakAnnotationBytes, 4096u);
  EXPECT_TRUE(attempt.finishParse());
  EXPECT_EQ(words, 100u);
  EXPECT_EQ(annotationBytes, 204800u);
  std::cout << "many legal ruby groups peak retained annotation bytes=" << peakAnnotationBytes << "\n";
  std::filesystem::remove(path);
}
