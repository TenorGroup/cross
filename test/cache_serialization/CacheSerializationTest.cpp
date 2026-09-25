#include <Epub/Page.h>
#include <Serialization.h>
#include <HalMemory.h>
#include <gtest/gtest.h>
#include <cstring>

namespace {
Page textPage() {
  Page page;
  auto block = std::make_unique<TextBlock>(
      std::vector<std::string>{"hello", "world"}, std::vector<int16_t>{0, 40},
      std::vector<EpdFontFamily::Style>{EpdFontFamily::REGULAR, EpdFontFamily::REGULAR},
      std::vector<uint8_t>{}, std::vector<uint16_t>{}, BlockStyle(), std::vector<std::string>{"ruby", ""});
  page.elements.push_back(std::make_unique<PageLine>(std::move(block), 10, 20));
  page.elements.push_back(std::make_unique<PageHorizontalRule>(80, 1, 0, 40));
  page.elements.push_back(std::make_unique<PageImage>(std::make_unique<ImageBlock>("/cache/picture.png", "Images/picture.png", 100, 120), 0, 60));
  page.addFootnote("1", "chapter.xhtml#note");
  page.addLink("chapter2.xhtml", 2, 3, 40, 10);
  return page;
}
HalFile validFile() {
  HalFile file;
  EXPECT_TRUE(textPage().serialize(file));
  file.offset = 0;
  return file;
}
}

TEST(CacheSerialization, RoundTripRetainsTextRubyAndLinks) {
  auto file = validFile();
  auto page = Page::deserialize(file);
  ASSERT_NE(page, nullptr);
  ASSERT_EQ(page->elements.size(), 3u);
  const auto* text = static_cast<PageLine*>(page->elements[0].get())->getBlock();
  ASSERT_EQ(text->wordCount(), 2);
  EXPECT_STREQ(text->wordText(0), "hello");
  ASSERT_EQ(text->getRubyTexts().size(), 2u);
  EXPECT_EQ(text->getRubyTexts()[0], "ruby");
  EXPECT_EQ(page->footnotes.size(), 1u);
  EXPECT_EQ(page->links.size(), 1u);
}

TEST(CacheSerialization, TruncationAtEveryByteRejectsPage) {
  const auto valid = validFile().bytes;
  for (size_t cut = 0; cut < valid.size(); ++cut) {
    HalFile file;
    file.bytes.assign(valid.begin(), valid.begin() + cut);
    EXPECT_EQ(Page::deserialize(file), nullptr) << "cut=" << cut;
  }
}

TEST(CacheSerialization, NegativeReadAtEveryFieldRejectsPage) {
  auto complete = validFile();
  ASSERT_NE(Page::deserialize(complete), nullptr);
  for (size_t field = 0; field < complete.calls; ++field) {
    auto file = validFile();
    file.failAtCall = field;
    EXPECT_EQ(Page::deserialize(file), nullptr) << "read call=" << field;
  }
}

TEST(CacheSerialization, MissingStringBodyNeverAllocatesClaimedLength) {
  HalFile file;
  const uint32_t length = 1024 * 1024;
  serialization::writePod(file, length);
  file.offset = 0;
  std::string result;
  serialization::readString(file, result);
  EXPECT_TRUE(result.empty());
  EXPECT_LT(result.capacity(), 1024u);
}

TEST(CacheSerialization, ShortWriteOfAnyFieldFailsWholePage) {
  // POD headers and style used to ignore a short write, allowing a corrupt page commit.
  HalFile file;
  file.writeLimit = 0;
  Page page;
  page.elements.push_back(std::make_unique<PageHorizontalRule>(80, 1, 0, 40));
  EXPECT_FALSE(page.serialize(file));
}

TEST(CacheSerialization, OversizedRubyRejectsWholePageWithoutTruncation) {
  auto page = textPage();
  auto oversized = std::make_unique<TextBlock>(
      std::vector<std::string>{"base"}, std::vector<int16_t>{0},
      std::vector<EpdFontFamily::Style>{EpdFontFamily::REGULAR},
      std::vector<uint8_t>{}, std::vector<uint16_t>{}, BlockStyle(),
      std::vector<std::string>{std::string(2049, 'r')});
  page.elements[0] = std::make_unique<PageLine>(std::move(oversized), 0, 0);
  HalFile file;
  ASSERT_TRUE(page.serialize(file));
  file.offset = 0;
  EXPECT_EQ(Page::deserialize(file), nullptr);
}

TEST(CacheSerialization, CountExceedingLargestHeapBlockRejectsPage) {
  Page page;
  for (int i = 0; i < 40; ++i) page.elements.push_back(std::make_unique<PageHorizontalRule>(80, 1, 0, i));
  HalFile file;
  ASSERT_TRUE(page.serialize(file));
  file.offset = 0;
  const size_t saved = HalMemory::largestBlockBytes;
  HalMemory::largestBlockBytes = 128;
  const auto result = Page::deserialize(file);
  HalMemory::largestBlockBytes = saved;
  EXPECT_EQ(result, nullptr);
}

TEST(CacheSerialization, InvalidFocusAndBoolMetadataRejectsPage) {
  auto file = validFile();
  // count(2), tag(1), x/y(4), wordCount(2), focus(1)
  file.bytes[9] = 2;
  EXPECT_EQ(Page::deserialize(file), nullptr);
}

// A chapter build wrote each line of text as one write per field and one per word for its empty
// ruby lengths, each through the storage lock: some 700 small writes a page (X3 r40, 15 ms a page).
// A line now takes a handful of writes, and it reads back field for field.
TEST(CacheSerialization, TextLineGoesOutInAFewWritesAndReadsBackWhole) {
  std::vector<std::string> words;
  std::vector<int16_t> xs;
  for (int i = 0; i < 40; ++i) {
    words.push_back("w" + std::to_string(i));
    xs.push_back(static_cast<int16_t>(i * 12));
  }
  BlockStyle style;
  style.alignment = CssTextAlign::Right;
  style.textAlignDefined = true;
  style.marginTop = 1; style.marginBottom = 2; style.marginLeft = 3; style.marginRight = 4;
  style.paddingTop = 5; style.paddingBottom = 6; style.paddingLeft = 7; style.paddingRight = 8;
  style.textIndent = -9; style.textIndentDefined = true; style.isRtl = true; style.directionDefined = true;
  Page page;
  page.elements.push_back(std::make_unique<PageLine>(
      std::make_unique<TextBlock>(words, xs, std::vector<EpdFontFamily::Style>(40, EpdFontFamily::BOLD),
                                  std::vector<uint8_t>{}, std::vector<uint16_t>{}, style),
      10, 20));
  HalFile file;
  ASSERT_TRUE(page.serialize(file));
  EXPECT_LE(file.writeCalls, 16u) << "a line of 40 words took " << file.writeCalls << " writes";
  file.offset = 0;
  const auto back = Page::deserialize(file);
  ASSERT_NE(back, nullptr);
  EXPECT_EQ(file.offset, file.bytes.size());
  const auto* text = static_cast<PageLine*>(back->elements[0].get())->getBlock();
  ASSERT_EQ(text->wordCount(), 40);
  EXPECT_STREQ(text->wordText(39), "w39");
  EXPECT_TRUE(text->getRubyTexts().empty());
  const auto& got = text->getBlockStyle();
  EXPECT_EQ(got.alignment, CssTextAlign::Right);
  EXPECT_TRUE(got.textAlignDefined);
  EXPECT_EQ(got.marginTop, 1); EXPECT_EQ(got.marginBottom, 2); EXPECT_EQ(got.marginLeft, 3);
  EXPECT_EQ(got.marginRight, 4); EXPECT_EQ(got.paddingTop, 5); EXPECT_EQ(got.paddingBottom, 6);
  EXPECT_EQ(got.paddingLeft, 7); EXPECT_EQ(got.paddingRight, 8); EXPECT_EQ(got.textIndent, -9);
  EXPECT_TRUE(got.textIndentDefined);
  EXPECT_TRUE(got.isRtl);
  EXPECT_TRUE(got.directionDefined);
}

TEST(CacheSerialization, ShortWriteAtEveryFieldFailsWholePage) {
  const Page page = textPage();
  HalFile complete;
  ASSERT_TRUE(page.serialize(complete));
  for (size_t field = 0; field < complete.writeCalls; ++field) {
    HalFile file;
    file.failAtWriteCall = field;
    EXPECT_FALSE(page.serialize(file)) << "write call=" << field;
  }
}
