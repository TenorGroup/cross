#include <Epub/Page.h>
#include <Epub/ReaderSpacing.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#define class struct
#define private public
#include "Epub/parsers/ChapterHtmlSlimParser.h"
#undef private
#undef class

namespace {

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
                               {},
                               true,
                               "",
                               "",
                               0,
                               {},
                               nullptr,
                               &cssParser};

  void SetUp() override { parser.currentTextBlock = std::make_unique<ParsedText>(false); }
};

TEST_F(ChapterHtmlSlimParserTest, RubySurvivesPartialParagraphExtraction) {
  ParsedText text(false);
  text.addWord("a", EpdFontFamily::REGULAR);
  text.addWord("b", EpdFontFamily::REGULAR);
  text.addWord("c", EpdFontFamily::REGULAR);
  text.setRubyForWordAt(2, "c");
  size_t lines = 0;
  text.layoutAndExtractLines(
      renderer, 0, 20,
      [&](std::unique_ptr<TextBlock> line, auto) {
        ++lines;
        EXPECT_TRUE(line->getRubyTexts().empty());
      },
      false);
  EXPECT_EQ(lines, 1u);
  const size_t retainedWords = text.size();
  ASSERT_GT(retainedWords, 0u);
  ASSERT_LT(retainedWords, 3u);
  text.layoutAndExtractLines(renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, auto) {
    ++lines;
    ASSERT_EQ(line->getRubyTexts().size(), retainedWords);
    EXPECT_EQ(line->getRubyTexts().back(), "c");
    for (size_t i = 0; i + 1 < retainedWords; ++i) EXPECT_TRUE(line->getRubyTexts()[i].empty());
  });
  EXPECT_EQ(lines, 2u);
}

TEST_F(ChapterHtmlSlimParserTest, UnequalTableCellsAndRubySurvivePageBreaks) {
  parser.viewportWidth = 240;
  parser.viewportHeight = 32;
  parser.tableRowCells.reserve(2);
  std::multiset<std::string> expected;
  for (int column = 0; column < 2; ++column) {
    auto cell = std::make_unique<ParsedText>(false);
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
  ASSERT_EQ(parser.currentTextBlock->words[0], "Before");
  ASSERT_EQ(parser.currentTextBlock->words[1], "After");
}

TEST_F(ChapterHtmlSlimParserTest, DivWithHiddenAttributeContentShouldBeSkipped) {
  const XML_Char* attributes[] = {"hidden", "hidden", nullptr};

  parser.beginParse();
  ChapterHtmlSlimParser::startElement(&parser, "div", attributes);
  ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
  ChapterHtmlSlimParser::characterData(&parser, "[HIDDEN]", 8);

  ASSERT_EQ(parser.partWordBufferIndex, 0);
}

}  // namespace

TEST_F(ChapterHtmlSlimParserTest, ForcedIndentCoexistsWithParagraphSpacingAndOnlyIndentsFirstLine) {
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  style.textAlignDefined = true;
  style.textIndentDefined = true;
  style.textIndent = 0;
  for (const bool spacing : {false, true}) {
    ParsedText text(spacing, false, false, style, 1);
    for (int i = 0; i < 15; ++i) text.addWord("word", EpdFontFamily::REGULAR);
    unsigned lines = 0;
    text.layoutAndExtractLines(renderer, 0, 100, [&](std::unique_ptr<TextBlock> line, auto) {
      ASSERT_GT(line->wordCount(), 0u);
      EXPECT_EQ(line->wordXpos(0), lines == 0 ? 12 : 0);
      ++lines;
    });
    EXPECT_GT(lines, 2u);
  }
}

TEST_F(ChapterHtmlSlimParserTest, IndentSizesOverrideCssAndParagraphGap) {
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  style.textAlignDefined = true;
  style.textIndentDefined = true;
  for (const int cssIndent : {-8, 0, 20}) {
    style.textIndent = cssIndent;
    for (const bool spacing : {false, true}) {
      for (const uint8_t mode : {0, 1, 2}) {
        ParsedText text(spacing, false, false, style, mode);
        text.addWord("word", EpdFontFamily::REGULAR);
        int expected = mode == 2 ? 24 : mode == 1 ? 12 : 0;
        text.layoutAndExtractLines(renderer, 0, 100, [&](std::unique_ptr<TextBlock> line, auto) {
          ASSERT_EQ(line->wordCount(), 1u);
          EXPECT_EQ(line->wordXpos(0), expected) << "mode=" << +mode << " css=" << cssIndent << " spacing=" << spacing;
        });
      }
    }
  }
}

TEST_F(ChapterHtmlSlimParserTest, IndentPreservesCenteredTextAndUsesRtlLeadingEdge) {
  BlockStyle style;
  style.textAlignDefined = true;
  style.directionDefined = true;
  style.alignment = CssTextAlign::Center;
  for (const uint8_t mode : {0, 1, 2}) {
    ParsedText text(true, false, false, style, mode);
    text.addWord("word", EpdFontFamily::REGULAR);
    text.layoutAndExtractLines(renderer, 0, 100,
                               [&](std::unique_ptr<TextBlock> line, auto) { EXPECT_EQ(line->wordXpos(0), 34); });
  }
  style.isRtl = true;
  style.alignment = CssTextAlign::Right;
  ParsedText text(true, false, false, style, 1);
  text.addWord("word", EpdFontFamily::REGULAR);
  text.layoutAndExtractLines(renderer, 0, 100, [&](std::unique_ptr<TextBlock> line, auto) {
    EXPECT_EQ(line->wordXpos(0) + renderer.getTextWidth(0, "word", EpdFontFamily::REGULAR), 88);
  });
}

TEST_F(ChapterHtmlSlimParserTest, ParserPropagatesIndentChoiceToNewParagraphs) {
  parser.currentTextBlock.reset();
  parser.paragraphIndent = 1;
  parser.extraParagraphSpacing = true;
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
  EXPECT_NE(first.wordStyle(0) & 128, 0);
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
  parser.tocAnchors.push_back("next");
  parser.pendingAnchorId = "next";
  parser.completePageFn = [](auto, auto, auto, auto) {};
  paragraph("p", "Gamma");
  ASSERT_EQ(parser.currentPage->elements.size(), 1u);
  EXPECT_GT(block(0).getDropCapHeight(), 0);
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

// Luat day trang do bang MUC (ascender 12 + descender 6 = 18), khong do bang o dong.
// Gian dong hep (o 16 < muc 18): dong thu ba o y = 32 co o vua khung 49 nhung muc cham 50, phai sang trang.
TEST_F(ChapterHtmlSlimParserTest, LastLineBreaksWhenInkOverflowsEvenIfLineBoxFits) {
  EXPECT_EQ(linesOnFirstPage(parser, 1.0f, 49), 2u);
  EXPECT_EQ(linesOnFirstPage(parser, 1.0f, 50), 3u);
}

// Gian dong rong (o 24 > muc 18): dong thu hai o y = 24 co o tran khung 42 nhung muc vua, duoc o lai.
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
  ParsedText text(false);
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
  ParsedText text(false);
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
    ParsedText text(true, false, false, style, 0, spacing);
    text.addWord("aaaaa", EpdFontFamily::REGULAR);
    text.addWord("bbbbb", EpdFontFamily::REGULAR);
    size_t count = 0;
    text.layoutAndExtractLines(renderer, 0, 84, [&](std::unique_ptr<TextBlock> line, auto) {
      ++count;
      EXPECT_EQ(line->getLetterSpacing(), spacing);
      if (line->wordCount() == 2) EXPECT_EQ(line->wordXpos(1), 44 + 6 * spacing);
    });
    EXPECT_EQ(count, spacing == 1 ? 2u : 1u);
  }
}

TEST_F(ChapterHtmlSlimParserTest, ParagraphModesAddThreeDistinctGaps) {
  // The parser now consumes a resolved gap in pixels, so the three legacy modes
  // are compared through readerSpacing::paragraphGap instead of raw ordinals.
  constexpr int kLineHeight = 16;
  auto paragraph = [&](const char* word) {
    parser.currentTextBlock = std::make_unique<ParsedText>(true);
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
      {"toc-next"},
      nullptr,
      &parserCss,
      2,
      0,
      0};

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
