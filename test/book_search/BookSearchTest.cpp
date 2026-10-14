#include <BookSearch.h>
#include <Utf8.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <Epub/Page.h>
#include <GfxRenderer.h>
#define class struct
#define private public
#include <Epub/parsers/ChapterHtmlSlimParser.h>
#undef private
#undef class

namespace {
struct Search {
  booksearch::Workspace work{};
  booksearch::Results results{};
  booksearch::Scanner scanner{work, results};
  explicit Search(const char* query) { EXPECT_EQ(scanner.begin(query, 4), booksearch::Status::Ready); }
  void html(const std::string& value, size_t chunk = 1024) {
    for (size_t at = 0; at < value.size() && !results.full(); at += chunk) {
      ASSERT_TRUE(scanner.feed(value.data() + at, std::min(chunk, value.size() - at)));
    }
    ASSERT_TRUE(scanner.finish());
  }
};

TEST(BookSearch, FoldsBothSidesAndPreservesOriginalText) {
  for (const char* query : {"nguoi", "Người", "NGƯỜI", "ngu\u031bo\u031b\u0300i"}) {
    Search search(query);
    search.html("<html><body>người Người NGƯỜI ngu\u031bo\u031b\u0300i</body></html>", 1);
    ASSERT_EQ(search.results.count, 4u);
    EXPECT_EQ(search.results.items[0].visibleOffset, 0u);
    EXPECT_EQ(search.results.items[1].visibleOffset, 6u);
    EXPECT_EQ(search.results.items[2].visibleOffset, 12u);
    EXPECT_EQ(search.results.items[3].visibleOffset, 18u);
    EXPECT_NE(std::string(search.results.items[0].excerpt).find("người"), std::string::npos);
  }
}

TEST(BookSearch, StrokeDAndWhitespaceAcrossFeedsAndInlineTags) {
  Search search("Đ   nguoi");
  search.html("<html><body>đ \n\t <b>ng</b>ười d <i>NGƯ</i>ỜI</body></html>", 2);
  ASSERT_EQ(search.results.count, 2u);
  EXPECT_EQ(search.results.items[0].visibleOffset, 0u);
  EXPECT_EQ(search.results.items[1].visibleOffset, 11u);
  Search d("d");
  d.html("<body>d đ D Đ</body>");
  EXPECT_EQ(d.results.count, 4u);
}

TEST(BookSearch, EmptyQueryAndWholeBookLimit) {
  Search empty("");
  empty.html("<body>hello</body>");
  EXPECT_EQ(empty.results.count, 0u);
  EXPECT_FALSE(empty.results.full());
  Search blanks(" \t\n");
  blanks.html("<body>hello</body>");
  EXPECT_EQ(blanks.results.count, 0u);
  Search search("a");
  std::string chapter = "<body>";
  for (unsigned i = 0; i < 120; ++i) chapter += "a ";
  chapter += "</body>";
  search.html(chapter);
  ASSERT_EQ(search.results.count, 120u);
  ASSERT_EQ(search.scanner.begin("a", 5), booksearch::Status::Ready);
  search.html(chapter);
  EXPECT_EQ(search.results.count, 200u);
  EXPECT_TRUE(search.results.full());
  EXPECT_EQ(search.results.items[119].spine, 4u);
  EXPECT_EQ(search.results.items[120].spine, 5u);
}

TEST(BookSearch, OverlappingMatchesAndExcerptCodepointRanges) {
  Search search("ana");
  search.html("<body>mở đầu banana có người đang đọc sách rất hay hôm nay và ngày mai nữa</body>", 3);
  ASSERT_EQ(search.results.count, 2u);
  for (size_t i = 0; i < search.results.count; ++i) {
    const auto& result = search.results.items[i];
    const auto* cp = reinterpret_cast<const unsigned char*>(result.excerpt);
    for (unsigned j = 0; j < result.matchStart; ++j) utf8NextCodepoint(&cp);
    std::string marked;
    for (unsigned j = 0; j < result.matchLength; ++j) utf8AppendCodepoint(utf8NextCodepoint(&cp), marked);
    EXPECT_EQ(marked, "ana");
    EXPECT_GT(std::string(result.excerpt).size(), marked.size());
  }
}

TEST(BookSearch, WhitespaceRunKeepsSourceOffsetAndWholeWordSnippetEdges) {
  Search search("nguoi doc");
  search.html("<body>lời mở đầu cùng những câu ngắn người" + std::string(5000, ' ') +
              "đọc những trang sách vào mỗi buổi sáng với rất nhiều điều mới mẻ hôm nay</body>", 7);
  ASSERT_EQ(search.results.count, 1u);
  const auto& result = search.results.items[0];
  EXPECT_EQ(result.visibleOffset, 31u);
  const auto* cp = reinterpret_cast<const unsigned char*>(result.excerpt);
  for (unsigned i = 0; i < result.matchStart; ++i) utf8NextCodepoint(&cp);
  std::string marked;
  for (unsigned i = 0; i < result.matchLength; ++i) utf8AppendCodepoint(utf8NextCodepoint(&cp), marked);
  EXPECT_EQ(marked, "người đọc");
  EXPECT_FALSE(result.excerptTruncated);
  EXPECT_EQ(std::string(result.excerpt).find("  "), std::string::npos);
}

TEST(BookSearch, CombiningMarksAfterLastMatchedLetterStayHighlighted) {
  Search search("nguoi");
  search.html("<body>ngu\u031bo\u031bi\u0300 đang đọc</body>", 1);
  ASSERT_EQ(search.results.count, 1u);
  EXPECT_EQ(search.results.items[0].matchLength, 8u);
}

TEST(BookSearch, CountMatchesRealChapterParserForThreeFixtures) {
  const std::vector<std::string> fixtures = {
      "<html><head><title>người</title></head><body>  A &amp; B <script>người</script><style>x</style>"
      "<p>Người <b>ng</b>ười</p></body></html>",
      "<html><body>\n<rp>(người)</rp><p>ngu\u031bo\u031b\u0300i   người</p><p hidden='hidden'>xyz</p>"
      "<p>NGƯỜI</p></body></html>",
      "<html><body><p>đường &amp; đường\n\t <i>Người</i></p><p><span>ng</span>ười</p></body></html>"};
  for (const auto& html : fixtures) {
    GfxRenderer renderer;
    CssParser css{"unused"};
    const auto file = std::filesystem::path(std::getenv("TMPDIR")) / "book-search-offset.xhtml";
    std::ofstream(file) << html;
    std::string path = file.string();
    ChapterHtmlSlimParser parser{nullptr, path, renderer, 0, 1.0f, false, 0, 528, 792,
                                 false, false, {}, true, "", "", 0, {}, nullptr, &css};
    parser.completePageFn = [](std::unique_ptr<Page>, uint16_t, uint16_t, uint32_t) {};
    ASSERT_TRUE(parser.beginParse());
    struct Observer {
      ChapterHtmlSlimParser* chapter;
      std::vector<std::pair<uint32_t, uint32_t>> text;
    } observer{&parser, {}};
    static Observer* active = nullptr;
    active = &observer;
    XML_SetCharacterDataHandler(parser.xmlParser_, [](void* data, const char* text, int length) {
      auto& state = *active;
      EXPECT_EQ(data, state.chapter);
      const uint32_t before = state.chapter->visibleTextOffset;
      ChapterHtmlSlimParser::characterData(state.chapter, text, length);
      const auto* cp = reinterpret_cast<const unsigned char*>(text);
      for (uint32_t at = before; at < state.chapter->visibleTextOffset; ++at) {
        state.text.emplace_back(at, utf8NextCodepoint(&cp));
      }
    });
    ASSERT_EQ(XML_Parse(parser.xmlParser_, html.data(), static_cast<int>(html.size()), XML_TRUE), XML_STATUS_OK);
    ASSERT_TRUE(parser.finishParse());
    active = nullptr;
    std::filesystem::remove(file);
    Search search("nguoi");
    search.html(html, 1);
    EXPECT_EQ(search.scanner.visibleOffset(), parser.visibleTextOffset);
    std::vector<uint32_t> expected;
    std::string counted;
    std::vector<size_t> byteStarts;
    for (const auto& [offset, cp] : observer.text) {
      byteStarts.push_back(counted.size());
      utf8AppendCodepoint(cp, counted);
    }
    for (const char* word : {"người", "Người", "NGƯỜI", "ngu\u031bo\u031b\u0300i"}) {
      for (size_t at = counted.find(word); at != std::string::npos; at = counted.find(word, at + 1)) {
        const auto found = std::lower_bound(byteStarts.begin(), byteStarts.end(), at);
        ASSERT_NE(found, byteStarts.end());
        ASSERT_EQ(*found, at);
        expected.push_back(observer.text[found - byteStarts.begin()].first);
      }
    }
    std::sort(expected.begin(), expected.end());
    ASSERT_EQ(search.results.count, expected.size());
    for (size_t i = 0; i < search.results.count; ++i) {
      const uint32_t offset = search.results.items[i].visibleOffset;
      EXPECT_EQ(offset, expected[i]);
    }
  }
}

TEST(BookSearch, RejectsMalformedXmlAndOversizedQueryCleanly) {
  Search search("a");
  ASSERT_TRUE(search.scanner.feed("<body>a", 7));
  EXPECT_FALSE(search.scanner.finish());
  EXPECT_EQ(search.scanner.status(), booksearch::Status::ParseError);
  EXPECT_EQ(search.scanner.begin(std::string(129, 'a').c_str(), 4), booksearch::Status::QueryTooLong);
}

TEST(BookSearch, HostThroughputReference) {
  Search search("missing");
  std::string html = "<body>";
  for (unsigned i = 0; i < 40000; ++i) html += "Người đang đọc sách. ";
  html += "</body>";
  const auto start = std::chrono::steady_clock::now();
  search.html(html);
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::printf("host_reference bytes=%zu bytes_per_second=%.0f\n", html.size(), html.size() / seconds);
  EXPECT_EQ(search.results.count, 0u);
}
}  // namespace
