#include <HalStorage.h>
#include <QuoteStore.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <chrono>
#include <unistd.h>
#include <string>

#include "../huge_book/HeapCap.h"

constexpr int CONTENT_LENGTH_UNKNOWN = -1;
enum class StrId { STR_WEB_FAILED_OPEN_FILE };
const char* trWeb(int, StrId) { return "Read error"; }
void resetTaskWatchdogIfSubscribed() {}
struct TestResponse {
  int status = 0, length = 0;
  std::string type, header, body;
  bool live = true, ended = false;
  size_t cutAfter = SIZE_MAX, transferred = 0;
  TestResponse& client() { return *this; }
  uint8_t connected() const { return live; }  // the real client answers with a byte
  void stop() { live = false; }
  void setContentLength(int value) { length = value; }
  void sendHeader(const char*, const char* value) { header = value; }
  void send(int code, const char* mime, const char* text) { status = code; type = mime; body = text; }
  void sendContent(const char* data, size_t size) {
    body.append(data, size);
    if (body.size() >= cutAfter) live = false;
  }
  void sendContent(const char*) { ended = true; }
};
struct CrossPointWebServer {
  TestResponse response;
  TestResponse* server = &response;
  int requestLanguage() { return 0; }
  void noteTransferActivity(size_t size) { response.transferred += size; }
  void handleQuotesExport();
};
#include "QuotesExport.inc"

namespace {

std::string quoteDirectory() { return fixtureRoot + "/.crosspoint/quotes"; }

QuoteRecord quoteOf(const int index) {
  QuoteRecord quote;
  quote.path = "/sach-" + std::to_string(index) + ".epub";
  quote.title = "Sách tiếng Việt " + std::to_string(index);
  quote.text = "Một trích dẫn có dấu tiếng Việt " + std::to_string(index);
  quote.spine = index % 7;
  quote.page = index + 1;
  quote.day = 20261011;
  quote.minute = static_cast<uint16_t>(index % 1440);
  quote.hasAnchor = true;
  quote.anchorStart = static_cast<uint32_t>(index * 2);
  quote.anchorEnd = static_cast<uint32_t>(index * 2 + 9);
  return quote;
}

class QuoteClippingsTest : public ::testing::Test {
 protected:
  std::filesystem::path root;
  void SetUp() override {
    root = std::filesystem::temp_directory_path() / ("cross-quote-clippings-" + std::to_string(getpid()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    fixtureRoot = root.string();
    io = {};
    storageFault = {};
    readFault = {};
  }
  void TearDown() override {
    std::filesystem::remove_all(root);
    fixtureRoot.clear();
  }
};

TEST_F(QuoteClippingsTest, EmptyStoreStartsWithUtf8Bom) {
  std::string output;
  ASSERT_EQ(quotes::streamKindle([&](const char* data, size_t size) {
              output.append(data, size);
              return true;
            }),
            0u);
  EXPECT_EQ(output, "\xEF\xBB\xBF");
}

TEST_F(QuoteClippingsTest, FiveHundredRecordsUseKindleBlocksAndDoNotWrite) {
  for (int i = 0; i < 500; ++i) ASSERT_TRUE(quotes::save(quoteOf(i)));
  const auto before = std::filesystem::directory_iterator(quoteDirectory());
  size_t beforeCount = 0;
  for (const auto& entry : before) {
    (void)entry;
    ++beforeCount;
  }

  std::string output;
  ASSERT_EQ(quotes::streamKindle([&](const char* data, size_t size) {
              output.append(data, size);
              return true;
            }),
            500u);
  EXPECT_EQ(output.compare(0, 3, "\xEF\xBB\xBF"), 0);
  EXPECT_EQ(std::count(output.begin(), output.end(), '='), 500u * 10u);
  EXPECT_NE(output.find("Sách tiếng Việt 0\n- Your Highlight on Page 2 | Chapter 1 | Added on Sunday, October 11, 2026, 12:00 AM\n\n"), std::string::npos);
  EXPECT_NE(output.find("Một trích dẫn có dấu tiếng Việt 499\n==========\n"), std::string::npos);

  size_t afterCount = 0;
  for (const auto& entry : std::filesystem::directory_iterator(quoteDirectory())) {
    (void)entry;
    ++afterCount;
  }
  EXPECT_EQ(afterCount, beforeCount);
  for (int i = 0; i < 500; ++i) {
    const auto quote = quoteOf(i);
    char place[192];
    const unsigned hour = i / 60;
    snprintf(place, sizeof(place), "\n- Your Highlight on Page %d | Chapter %d | Added on Sunday, October 11, 2026, %02u:%02u AM\n\n",
             i + 2, i % 7 + 1, hour % 12 ? hour % 12 : 12, i % 60);
    EXPECT_NE(output.find(quote.title + place + quote.text + "\n==========\n"), std::string::npos) << i;
  }
  io = {};
  size_t streamed = 0, sends = 0, biggest = 0;
  const auto begin = std::chrono::steady_clock::now();
  heapcap::reset(12288);
  const size_t records = quotes::streamKindle([&](const char*, size_t size) {
    streamed += size;
    ++sends;
    biggest = std::max(biggest, size);
    return true;
  });
  heapcap::stop();
  const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
  EXPECT_EQ(records, 500u);
  EXPECT_EQ(streamed, output.size());
  // About 1 KB per send: a chunk per line would be 6 sends a record, 3,000 for 500.
  EXPECT_LE(biggest, 1024u);
  EXPECT_LE(sends, streamed / 900 + 2) << sends << " sends for " << streamed << " bytes";
  EXPECT_EQ(heapcap::aborts, 0u);
  EXPECT_EQ(heapcap::live, 0u);
  EXPECT_EQ(io.writeOpens + io.writes + io.mkdirs + io.removes + io.renames, 0u);
  printf("QUOTE_EXPORT_HOST count=%zu bytes=%zu sends=%zu peak=%zu live=%zu elapsed_ms=%.3f\n",
         records, streamed, sends, heapcap::peak, heapcap::live, elapsed);
}

TEST_F(QuoteClippingsTest, LeavesBrokenFilesAndRecoveryCopiesUntouched) {
  ASSERT_TRUE(quotes::save(quoteOf(0)));
  std::vector<quotes::QuoteId> ids;
  quotes::listNames(0, ids);
  const auto path = quoteDirectory() + "/" + quotes::nameOf(ids.front());
  std::filesystem::copy_file(path, path + ".davbak");
  std::filesystem::copy_file(path, path + ".tmp");
  std::ofstream(path) << "broken";
  std::map<std::string, std::string> before;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (entry.is_regular_file()) {
      std::ifstream input(entry.path());
      before[entry.path().string()] = std::string(std::istreambuf_iterator<char>(input), {});
    }
  }
  io = {};
  std::string output;
  EXPECT_EQ(quotes::streamKindle([&](const char* data, size_t size) {
    output.append(data, size);
    return true;
  }), 0u);
  EXPECT_EQ(io.writeOpens, 0u);
  EXPECT_EQ(io.writes, 0u);
  EXPECT_EQ(io.mkdirs, 0u);
  EXPECT_EQ(io.removes, 0u);
  EXPECT_EQ(io.renames, 0u);
  for (const auto& [name, bytes] : before) {
    std::ifstream input(name);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(input), {}), bytes) << name;
  }
}

TEST_F(QuoteClippingsTest, RouteSendsAttachmentAndPreservesCard) {
  ASSERT_TRUE(quotes::save(quoteOf(0)));
  io = {};
  CrossPointWebServer web;
  web.handleQuotesExport();
  EXPECT_EQ(web.response.status, 200);
  EXPECT_EQ(web.response.length, CONTENT_LENGTH_UNKNOWN);
  EXPECT_EQ(web.response.header, "attachment; filename=\"My Clippings.txt\"");
  EXPECT_EQ(web.response.type, "text/plain; charset=utf-8");
  EXPECT_TRUE(web.response.ended);
  EXPECT_EQ(web.response.body, "\xEF\xBB\xBF" "Sách tiếng Việt 0\n"
    "- Your Highlight on Page 2 | Chapter 1 | Added on Sunday, October 11, 2026, 12:00 AM\n\n"
    "Một trích dẫn có dấu tiếng Việt 0\n==========\n");
  EXPECT_EQ(io.writeOpens + io.writes + io.mkdirs + io.removes + io.renames, 0u);
  EXPECT_EQ(io.fullPathFileOpens, 1u);
}

TEST_F(QuoteClippingsTest, RouteStopsWhenBrowserDisconnects) {
  ASSERT_TRUE(quotes::save(quoteOf(0)));
  CrossPointWebServer web;
  web.response.cutAfter = 3;
  io = {};
  web.handleQuotesExport();
  EXPECT_FALSE(web.response.ended);
  EXPECT_FALSE(web.response.live);
  EXPECT_EQ(io.fullPathFileOpens, 0u);
}

TEST_F(QuoteClippingsTest, RefusesShortHeapBeforeResponseStarts) {
  CrossPointWebServer web;
  heapcap::reset(8191);
  web.handleQuotesExport();
  heapcap::stop();
  EXPECT_EQ(web.response.status, 503);
  EXPECT_EQ(heapcap::refusals, 1u);
  EXPECT_EQ(heapcap::aborts, 0u);
  EXPECT_FALSE(web.response.ended);
}

TEST_F(QuoteClippingsTest, KeepsUnknownDateAndUnknownMinuteHonest) {
  auto quote = quoteOf(0);
  quote.day = 0;
  quote.minute = quotes::NO_MINUTE;
  ASSERT_TRUE(quotes::save(quote));
  CrossPointWebServer web;
  web.handleQuotesExport();
  EXPECT_NE(web.response.body.find("- Your Highlight on Page 2 | Chapter 1\n\n"), std::string::npos);
  EXPECT_EQ(web.response.body.find("Added on"), std::string::npos);
  quote.day = 20261011;
  quote.text += " khác";
  ASSERT_TRUE(quotes::save(quote));
  web.handleQuotesExport();
  EXPECT_NE(web.response.body.find("Added on Sunday, October 11, 2026\n\n"), std::string::npos);
}

TEST_F(QuoteClippingsTest, ReadErrorEndsPartialDownloadWithoutTerminator) {
  ASSERT_TRUE(quotes::save(quoteOf(0)));
  std::vector<quotes::QuoteId> ids;
  quotes::listNames(0, ids);
  readFault.path = "/.crosspoint/quotes/" + quotes::nameOf(ids.front());
  readFault.fullPath = true;
  readFault.failAfter = 12;
  CrossPointWebServer web;
  web.handleQuotesExport();
  EXPECT_FALSE(web.response.ended);
  EXPECT_FALSE(web.response.live);
  EXPECT_EQ(web.response.body, "\xEF\xBB\xBF");
}

TEST_F(QuoteClippingsTest, MaximumRecordFitsAndHeapDoesNotGrowWithCount) {
  auto quote = quoteOf(0);
  quote.title = std::string(512, 't');
  quote.text = std::string(1024, 'x');
  quote.path = "/" + std::string(1023, 'p');
  ASSERT_TRUE(quotes::save(quote));
  // Warm the filesystem fixture map before measuring the export allocations.
  quotes::streamKindle([](const char*, size_t) { return true; });
  heapcap::reset(12288);
  size_t bytes = 0;
  const size_t count = quotes::streamKindle([&](const char*, size_t size) { bytes += size; return true; });
  heapcap::stop();
  EXPECT_EQ(count, 1u);
  EXPECT_GT(bytes, 1536u);
  EXPECT_EQ(heapcap::aborts, 0u);
  EXPECT_LE(heapcap::peak, 12288u);
  printf("QUOTE_EXPORT_HOST count=%zu bytes=%zu peak=%zu live=%zu\n", count, bytes, heapcap::peak, heapcap::live);
}

}  // namespace
