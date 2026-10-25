#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "Print.h"
#include "TxtToHtml.h"

namespace {

class StringPrint : public Print {
 public:
  std::string str;
  size_t write(uint8_t b) override {
    str.push_back(static_cast<char>(b));
    return 1;
  }
  size_t write(const uint8_t* buffer, size_t size) override {
    str.append(reinterpret_cast<const char*>(buffer), size);
    return size;
  }
};

const std::string kHeader =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<!-- TXT_CACHE_VERSION: 1 -->\n"
    "<!DOCTYPE html>\n<html>\n<head><title>test</title></head>\n<body>\n";
const std::string kFooter = "\n</body>\n</html>\n";

std::string convert(std::string_view content) {
  StringPrint out;
  EXPECT_TRUE(TxtToHtml::stream("test.txt", content, out));
  return out.str;
}

TEST(TxtToHtmlTest, LeadingSpacesIndentation) {
  EXPECT_EQ(convert("  two spaces"), kHeader + "&#160;&#160;two spaces" + kFooter);
  EXPECT_EQ(convert("    four spaces"), kHeader + "&#160;&#160;&#160;&#160;four spaces" + kFooter);
  EXPECT_EQ(convert("Line 1\n   three spaces"), kHeader + "Line 1<br />&#160;&#160;&#160;three spaces" + kFooter);
}

TEST(TxtToHtmlTest, MidLineConsecutiveSpaces) {
  EXPECT_EQ(convert("One space"), kHeader + "One space" + kFooter);
  EXPECT_EQ(convert("Two  spaces"), kHeader + "Two&#160; spaces" + kFooter);
  EXPECT_EQ(convert("Three   spaces"), kHeader + "Three&#160;&#160; spaces" + kFooter);
  EXPECT_EQ(convert("Four    spaces"), kHeader + "Four&#160;&#160;&#160; spaces" + kFooter);
}

TEST(TxtToHtmlTest, PreservesCacheVersionTagsForBothFormats) {
  StringPrint out;
  ASSERT_TRUE(TxtToHtml::stream("test.MD", "text", out));
  EXPECT_EQ(out.str,
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!-- MD_CACHE_VERSION: 1 -->\n"
            "<!DOCTYPE html>\n<html>\n<head><title>test</title></head>\n<body>\ntext" +
                kFooter);
  EXPECT_NE(out.str.find(TxtToHtml::cacheVersionTag("test.MD")), std::string::npos);
  EXPECT_NE(convert("text").find(TxtToHtml::cacheVersionTag("test.txt")), std::string::npos);
}

// The parser reads the file as UTF-8 and stops on a byte that is not: a TXT in another encoding (CP1258, TCVN3) or
// with one stray byte must still open, the bad bytes shown as U+FFFD (one per maximal invalid piece, as browsers do).
const std::string FFFD = "\xEF\xBF\xBD";

TEST(TxtToHtmlTest, ValidUtf8PassesUnchanged) {
  const std::string text = "Đọc sách: chương 1, \xE2\x82\xAC \xF0\x9F\x93\x96 \xEF\xBF\xBD";
  EXPECT_EQ(convert(text), kHeader + text + kFooter);
}

TEST(TxtToHtmlTest, InvalidBytesBecomeTheReplacementCharacter) {
  EXPECT_EQ(convert("a\x80z"), kHeader + "a" + FFFD + "z" + kFooter);
  EXPECT_EQ(convert("\xFF\xFEx"), kHeader + FFFD + FFFD + "x" + kFooter);
  EXPECT_EQ(convert("a\xC0\x80z"), kHeader + "a" + FFFD + FFFD + "z" + kFooter);       // overlong NUL
  EXPECT_EQ(convert("a\xED\xA0\x80z"), kHeader + "a" + FFFD + FFFD + FFFD + "z" + kFooter);  // a surrogate
  EXPECT_EQ(convert("a\xF5\x80\x80\x80z"), kHeader + "a" + FFFD + FFFD + FFFD + FFFD + "z" + kFooter);
  EXPECT_EQ(convert("a\xEF\xBF\xBEz"), kHeader + "a" + FFFD + "z" + kFooter);  // U+FFFE is no XML character
}

TEST(TxtToHtmlTest, ASequenceCutShortIsOneReplacementCharacter) {
  EXPECT_EQ(convert("ab\xE2\x82z"), kHeader + "ab" + FFFD + "z" + kFooter);
  EXPECT_EQ(convert("ab\xE2\x82"), kHeader + "ab" + FFFD + kFooter);
  EXPECT_EQ(convert("ab\xC3\n"), kHeader + "ab" + FFFD + "<br />" + kFooter);
}

TEST(TxtToHtmlTest, AnInvalidByteCountsAsTextForTheSpacesAroundIt) {
  EXPECT_EQ(convert("  \x80  b"), kHeader + "&#160;&#160;" + FFFD + "&#160; b" + kFooter);
}

// The converter reads 8 KB at a time; a character or a line end split by the edge is not two pieces.
TEST(TxtToHtmlTest, ACharacterOrLineEndSplitByTheReadBufferIsKept) {
  const std::string head(8191, 'a');
  EXPECT_EQ(convert(head + "\xC3\xA9z"), kHeader + head + "\xC3\xA9z" + kFooter);
  EXPECT_EQ(convert(head + "\xC3z"), kHeader + head + FFFD + "z" + kFooter);
  EXPECT_EQ(convert(head + "\r\nb"), kHeader + head + "<br />b" + kFooter);
}

// Old Mac files end their lines with CR alone; CR LF is one line end.
TEST(TxtToHtmlTest, ACarriageReturnAloneEndsALine) {
  EXPECT_EQ(convert("a\rb"), kHeader + "a<br />b" + kFooter);
  EXPECT_EQ(convert("a\r\nb"), kHeader + "a<br />b" + kFooter);
  EXPECT_EQ(convert("a\r\rb"), kHeader + "a<br /><br />b" + kFooter);
  EXPECT_EQ(convert("a\r\n\r\nb"), kHeader + "a<br /><br />b" + kFooter);
  EXPECT_EQ(convert("a\nb"), kHeader + "a<br />b" + kFooter);
  EXPECT_EQ(convert("  a\r  b"), kHeader + "&#160;&#160;a<br />&#160;&#160;b" + kFooter);
}

}  // namespace
