#include <gtest/gtest.h>
#include <SecureHttpClient.h>
#include <ctime>
#include <memory>

using String = std::string;
#define LOG_DBG(...)
#define LOG_ERR(...)
#define CROSSPOINT_VERSION "test"
struct TestClock { bool syncFromNTP() { return true; } } halClock;
namespace network_trust { const char* forUrl(const std::string&) { return "fixture"; } }
using esp_err_t = int;
constexpr int ESP_OK = 0, WIFI_PS_NONE = 0, WIFI_PS_MIN_MODEM = 1;
int esp_wifi_set_ps(int) { return ESP_OK; }
struct HttpDownloader {
  static constexpr uint32_t PINNED_CA_TIMEOUT_MS = 10000;
  using ProgressCallback = std::function<void(size_t, size_t)>;
  enum DownloadError { OK, HTTP_ERROR, FILE_ERROR, ABORTED };
};
namespace freeink {
SecureClient::~SecureClient() { stop(); }
void SecureClient::setCACert(const char* ca) { _rootCA = ca; }
void SecureClient::setInsecure() { _insecure = true; }
int SecureClient::connect(IPAddress ip, uint16_t p) { return _transport.connect(ip, p); }
int SecureClient::connect(const char* h, uint16_t p) { return _transport.connect(h, p); }
size_t SecureClient::write(uint8_t b) { return _transport.write(b); }
size_t SecureClient::write(const uint8_t* p, size_t n) { return _transport.write(p, n); }
int SecureClient::available() { return _transport.available(); }
int SecureClient::read() { return _transport.read(); }
int SecureClient::read(uint8_t* p, size_t n) { return _transport.read(p, n); }
int SecureClient::peek() { return _transport.peek(); }
void SecureClient::flush() { _transport.flush(); }
void SecureClient::stop() { _transport.stop(); }
uint8_t SecureClient::connected() { return _transport.connected(); }
}

#ifndef FREEINK_NET_WOLFSSL
#include "esp_http_fixture.h"
#endif
#define HTTP_DOWNLOADER_TRANSPORT_FIXTURE 1
#include "production_transport.inc"

static auto runDownload(Sink& sink) {
#ifdef FREEINK_NET_WOLFSSL
  return runGetWolf("http://fixture.test/book", "", "", sink, false, nullptr, true);
#else
  return runGet("http://fixture.test/book", "", "", sink, nullptr, true);
#endif
}

class DownloadPump : public testing::Test {
 protected:
  void SetUp() override { wire::reset(); }
  void expectCancel(const std::string& response) {
    wire::replies = {response};
    bool cancel = false;
    int pumps = 0;
    Sink sink;
    sink.cancelFlag = &cancel;
    sink.write = [](const uint8_t*, size_t) { return true; };
    sink.progress = [&](size_t, size_t) { if (++pumps == 3) cancel = true; };
    const auto before = millis();
    EXPECT_EQ(runDownload(sink), HttpDownloader::ABORTED);
    EXPECT_GE(pumps, 3);
    EXPECT_LT(millis() - before, 150UL);
  }
};
TEST_F(DownloadPump, CancelsWhileWaitingForFirstHeader) { expectCancel(""); }
TEST_F(DownloadPump, CancelsWhileHeaderIsIncomplete) { expectCancel("HTTP/1.1 200 OK\r\nContent-Len"); }
TEST_F(DownloadPump, CancelsWhileFixedBodyStalls) { expectCancel("HTTP/1.1 200 OK\r\nContent-Length: 50\r\n\r\nabc"); }
TEST_F(DownloadPump, CancelsWhileChunkedBodyStalls) { expectCancel("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"); }
TEST_F(DownloadPump, CancelsWhileCloseDelimitedBodyStalls) { expectCancel("HTTP/1.1 200 OK\r\n\r\nabc"); }
TEST_F(DownloadPump, UnknownSizeReportsFinalBytes) {
  wire::replies = {"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n"};
  size_t reported = 0, total = 999;
  Sink sink;
  sink.write = [](const uint8_t*, size_t) { return true; };
  sink.progress = [&](size_t bytes, size_t size) { reported = bytes; total = size; };
  EXPECT_EQ(runDownload(sink), HttpDownloader::OK);
  EXPECT_EQ(reported, 3);
  EXPECT_EQ(total, 0);
}
TEST_F(DownloadPump, CancelBeforeConnecting) {
  wire::replies = {"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK"};
  bool cancel = false;
  Sink sink;
  sink.cancelFlag = &cancel;
  sink.write = [](const uint8_t*, size_t) { return true; };
  sink.progress = [&](size_t, size_t) { cancel = true; };
  EXPECT_EQ(runDownload(sink), HttpDownloader::ABORTED);
  EXPECT_EQ(wire::connectAttempts, 0);
}
TEST_F(DownloadPump, PreservesFixedChunkedAndCloseDelimitedPayloads) {
  const std::string replies[] = {
    "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc",
    "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n",
    "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nabc"
  };
  for (const auto& reply : replies) {
    wire::reset();
    wire::closeAfterReply = true;
    wire::replies = {reply};
    std::string payload;
    Sink sink;
    sink.write = [&](const uint8_t* bytes, size_t n) { payload.append(reinterpret_cast<const char*>(bytes), n); return true; };
    EXPECT_EQ(runDownload(sink), HttpDownloader::OK);
    EXPECT_EQ(payload, "abc");
  }
}

#ifdef FREEINK_NET_WOLFSSL
// The OTA failure record reads how far a failed transfer got off the sink.
class TransferRecord : public testing::Test {
 protected:
  void SetUp() override {
    wire::reset();
    wire::closeAfterReply = true;
  }
  HttpDownloader::DownloadError get(const std::string& reply) {
    wire::replies = {reply};
    sink.write = [](const uint8_t*, size_t) { return true; };
    return runDownload(sink);
  }
  Sink sink;
};
TEST_F(TransferRecord, BodyCutShort) {
  EXPECT_EQ(get("HTTP/1.1 200 OK\r\nContent-Length: 50\r\n\r\nabc"), HttpDownloader::HTTP_ERROR);
  EXPECT_EQ(sink.status, 200);
  EXPECT_TRUE(sink.headers);
  EXPECT_EQ(sink.downloaded, 3u);
  EXPECT_GT(sink.lastDataMs, sink.startMs);
}
TEST_F(TransferRecord, NoStatusLine) {
  EXPECT_EQ(get(""), HttpDownloader::HTTP_ERROR);
  EXPECT_EQ(sink.status, 0);
  EXPECT_FALSE(sink.headers);
  EXPECT_EQ(sink.lastDataMs, 0u);
}
TEST_F(TransferRecord, HeadersCutShort) {
  EXPECT_EQ(get("HTTP/1.1 200 OK\r\nContent-Len"), HttpDownloader::HTTP_ERROR);
  EXPECT_EQ(sink.status, 200);
  EXPECT_FALSE(sink.headers);
}
TEST_F(TransferRecord, ErrorStatus) {
  EXPECT_EQ(get("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n"), HttpDownloader::HTTP_ERROR);
  EXPECT_EQ(sink.status, 503);
  EXPECT_TRUE(sink.headers);
}

// The OTA image comes in parts: a part must continue the stream at the byte asked for.
class WolfRange : public testing::Test {
 protected:
  void SetUp() override {
    wire::reset();
    wire::closeAfterReply = true;
  }
  HttpDownloader::DownloadError get(const std::string& reply, size_t first, size_t last) {
    wire::replies = {reply};
    sink.write = [this](const uint8_t* bytes, size_t n) {
      payload.append(reinterpret_cast<const char*>(bytes), n);
      return true;
    };
    range.first = first;
    range.last = last;
    return runGetWolf("http://fixture.test/fw.bin", "", "", sink, false, nullptr, false, &range);
  }
  Sink sink;
  ByteRange range;
  std::string payload;
};
TEST_F(WolfRange, AsksForThePartAndTakesAMatching206) {
  EXPECT_EQ(get("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 4-6/10\r\nContent-Length: 3\r\n\r\nefg", 4, 6),
            HttpDownloader::OK);
  EXPECT_NE(wire::requests[0].bytes.find("\r\nRange: bytes=4-6\r\n"), std::string::npos);
  EXPECT_EQ(payload, "efg");
  EXPECT_FALSE(range.whole);
}
TEST_F(WolfRange, RefusesAPartStartingElsewhere) {
  EXPECT_NE(get("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 0-2/10\r\nContent-Length: 3\r\n\r\nabc", 4, 6),
            HttpDownloader::OK);
  EXPECT_EQ(payload, "");
}
TEST_F(WolfRange, TakesTheWholeBodyForAPartFromZero) {
  EXPECT_EQ(get("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nabcde", 0, 2), HttpDownloader::OK);
  EXPECT_EQ(payload, "abcde");
  EXPECT_TRUE(range.whole);
}
TEST_F(WolfRange, RefusesTheWholeBodyForALaterPart) {
  EXPECT_NE(get("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nabcde", 3, 4), HttpDownloader::OK);
  EXPECT_EQ(payload, "");
  EXPECT_TRUE(range.whole);
}

class WolfRedirect : public testing::Test {
 protected:
  void SetUp() override { wire::reset(); }
  HttpDownloader::DownloadError get(const std::string& url) {
    Sink sink;
    sink.write = [&](const uint8_t* data, size_t size) {
      payload.append(reinterpret_cast<const char*>(data), size);
      return true;
    };
    return runGetWolf(url, "", "", sink, false, nullptr, true);
  }
  std::string payload;
};

TEST_F(WolfRedirect, QueryOnlyLocationMayContainAbsoluteUrlValue) {
  wire::replies = {"HTTP/1.1 302 Found\r\nLocation: ?next=https://catalog.example/b\r\nContent-Length: 0\r\n\r\n",
                   "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc"};
  EXPECT_EQ(get("https://books.example/catalog/page?old=1"), HttpDownloader::OK);
  ASSERT_EQ(wire::requests.size(), 2u);
  EXPECT_NE(wire::requests[1].bytes.find("GET /catalog/page?next=https://catalog.example/b HTTP/1.1"),
            std::string::npos);
}

TEST_F(WolfRedirect, RelativeLocationMayContainAbsoluteUrlValue) {
  wire::replies = {"HTTP/1.1 302 Found\r\nLocation: next.epub?origin=https://example.org\r\nContent-Length: 0\r\n\r\n",
                   "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc"};
  EXPECT_EQ(get("https://books.example/catalog/page"), HttpDownloader::OK);
  ASSERT_EQ(wire::requests.size(), 2u);
  EXPECT_NE(wire::requests[1].bytes.find("GET /catalog/next.epub?origin=https://example.org HTTP/1.1"),
            std::string::npos);
}

TEST_F(WolfRedirect, RejectsUnsupportedScheme) {
  wire::replies = {"HTTP/1.1 302 Found\r\nLocation: ftp://books.example/book\r\nContent-Length: 0\r\n\r\n"};
  EXPECT_EQ(get("https://books.example/catalog/page"), HttpDownloader::HTTP_ERROR);
  EXPECT_EQ(wire::connectAttempts, 1);
}

TEST_F(WolfRedirect, RejectsOverlongLocationBeforeConnecting) {
  wire::replies = {"HTTP/1.1 302 Found\r\nLocation: https://books.example/" + std::string(2048, 'a') +
                   "\r\nContent-Length: 0\r\n\r\n"};
  EXPECT_EQ(get("https://books.example/catalog/page"), HttpDownloader::HTTP_ERROR);
  EXPECT_EQ(wire::connectAttempts, 1);
}
#endif

#ifndef FREEINK_NET_WOLFSSL
class FallbackRedirect : public testing::Test {
 protected:
  void SetUp() override {
    wire::reset();
    wire::fallbackRequests.clear();
  }
  HttpDownloader::DownloadError get(const std::string& url, const std::string& user = {},
                                    const std::string& password = {}) {
    Sink sink;
    sink.write = [&](const uint8_t* data, size_t size) {
      payload.append(reinterpret_cast<const char*>(data), size);
      return true;
    };
    return runGet(url, user, password, sink, nullptr, true);
  }
  std::string payload;
};

TEST_F(FallbackRedirect, FollowsHttpsWithSameOriginBasicAuth) {
  wire::replies = {"HTTP/1.1 302 Found\r\nLocation: /book\r\nContent-Length: 0\r\n\r\n",
                   "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc"};
  EXPECT_EQ(get("https://books.example/opds", "reader", "secret"), HttpDownloader::OK);
  EXPECT_EQ(payload, "abc");
  ASSERT_EQ(wire::fallbackRequests.size(), 2u);
  EXPECT_EQ(wire::fallbackRequests[1].url, "https://books.example/book");
  EXPECT_TRUE(wire::fallbackRequests[1].authorized);
}

TEST_F(FallbackRedirect, PreservesRedirectQuery) {
  wire::replies = {"HTTP/1.1 302 Found\r\nLocation: /book?token=abc\r\nContent-Length: 0\r\n\r\n",
                   "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc"};
  EXPECT_EQ(get("https://books.example/opds"), HttpDownloader::OK);
  ASSERT_EQ(wire::fallbackRequests.size(), 2u);
  EXPECT_EQ(wire::fallbackRequests[1].url, "https://books.example/book?token=abc");
}

TEST_F(FallbackRedirect, QueryOnlyLocationKeepsCurrentPath) {
  wire::replies = {"HTTP/1.1 302 Found\r\nLocation: ?page=2\r\nContent-Length: 0\r\n\r\n",
                   "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc"};
  EXPECT_EQ(get("https://books.example/catalog/page?old=1"), HttpDownloader::OK);
  ASSERT_EQ(wire::fallbackRequests.size(), 2u);
  EXPECT_EQ(wire::fallbackRequests[1].url, "https://books.example/catalog/page?page=2");
}

TEST_F(FallbackRedirect, QueryOnlyLocationMayContainAbsoluteUrlValue) {
  wire::replies = {"HTTP/1.1 302 Found\r\nLocation: ?next=https://catalog.example/b\r\nContent-Length: 0\r\n\r\n",
                   "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc"};
  EXPECT_EQ(get("https://books.example/catalog/page?old=1"), HttpDownloader::OK);
  ASSERT_EQ(wire::fallbackRequests.size(), 2u);
  EXPECT_EQ(wire::fallbackRequests[1].url,
            "https://books.example/catalog/page?next=https://catalog.example/b");
}

TEST_F(FallbackRedirect, RelativeLocationMayContainAbsoluteUrlValue) {
  wire::replies = {"HTTP/1.1 302 Found\r\nLocation: next.epub?origin=https://example.org\r\nContent-Length: 0\r\n\r\n",
                   "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc"};
  EXPECT_EQ(get("https://books.example/catalog/page"), HttpDownloader::OK);
  ASSERT_EQ(wire::fallbackRequests.size(), 2u);
  EXPECT_EQ(wire::fallbackRequests[1].url,
            "https://books.example/catalog/next.epub?origin=https://example.org");
}

TEST_F(FallbackRedirect, RelativeLocationIgnoresSlashInCurrentQuery) {
  wire::replies = {"HTTP/1.1 302 Found\r\nLocation: next\r\nContent-Length: 0\r\n\r\n",
                   "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc"};
  EXPECT_EQ(get("https://books.example/catalog/page?next=/nested/item"), HttpDownloader::OK);
  ASSERT_EQ(wire::fallbackRequests.size(), 2u);
  EXPECT_EQ(wire::fallbackRequests[1].url, "https://books.example/catalog/next");
}

TEST_F(FallbackRedirect, RelativeParentLocationRemovesDotSegment) {
  wire::replies = {"HTTP/1.1 302 Found\r\nLocation: ../book\r\nContent-Length: 0\r\n\r\n",
                   "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc"};
  EXPECT_EQ(get("https://books.example/catalog/chapter/page"), HttpDownloader::OK);
  ASSERT_EQ(wire::fallbackRequests.size(), 2u);
  EXPECT_EQ(wire::fallbackRequests[1].url, "https://books.example/catalog/book");
}

TEST_F(FallbackRedirect, RejectsOverlongLocationBeforeConnecting) {
  wire::replies = {"HTTP/1.1 302 Found\r\nLocation: https://books.example/" + std::string(2048, 'a') +
                   "\r\nContent-Length: 0\r\n\r\n"};
  EXPECT_EQ(get("https://books.example/opds"), HttpDownloader::HTTP_ERROR);
  EXPECT_EQ(wire::fallbackRequests.size(), 1u);
}

TEST_F(FallbackRedirect, RemovesBasicAuthAcrossOrigins) {
  wire::replies = {"HTTP/1.1 302 Found\r\nLocation: https://cdn.example/book\r\nContent-Length: 0\r\n\r\n",
                   "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc"};
  EXPECT_EQ(get("https://books.example/opds", "reader", "secret"), HttpDownloader::OK);
  EXPECT_EQ(payload, "abc");
  ASSERT_EQ(wire::fallbackRequests.size(), 2u);
  EXPECT_EQ(wire::fallbackRequests[1].url, "https://cdn.example/book");
  EXPECT_FALSE(wire::fallbackRequests[1].authorized);
}

TEST_F(FallbackRedirect, AllowsHttpUpgradeToHttps) {
  wire::replies = {"HTTP/1.1 301 Moved\r\nLocation: https://books.example/book\r\nContent-Length: 0\r\n\r\n",
                   "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc"};
  EXPECT_EQ(get("http://books.example/opds"), HttpDownloader::OK);
  EXPECT_EQ(payload, "abc");
  ASSERT_EQ(wire::fallbackRequests.size(), 2u);
  EXPECT_EQ(wire::fallbackRequests[1].url, "https://books.example/book");
}

TEST_F(FallbackRedirect, RejectsHttpsDowngrade) {
  wire::replies = {"HTTP/1.1 302 Found\r\nLocation: http://books.example/book\r\nContent-Length: 0\r\n\r\n"};
  EXPECT_EQ(get("https://books.example/opds"), HttpDownloader::HTTP_ERROR);
  EXPECT_EQ(wire::fallbackRequests.size(), 1u);
}

TEST_F(FallbackRedirect, RejectsUnsupportedScheme) {
  wire::replies = {"HTTP/1.1 302 Found\r\nLocation: ftp://books.example/book\r\nContent-Length: 0\r\n\r\n"};
  EXPECT_EQ(get("https://books.example/opds"), HttpDownloader::HTTP_ERROR);
  EXPECT_EQ(wire::fallbackRequests.size(), 1u);
}

TEST_F(FallbackRedirect, RejectsSixthHop) {
  for (int i = 0; i < 6; ++i)
    wire::replies.push_back("HTTP/1.1 302 Found\r\nLocation: /again\r\nContent-Length: 0\r\n\r\n");
  EXPECT_EQ(get("http://books.example/opds"), HttpDownloader::HTTP_ERROR);
  EXPECT_EQ(wire::fallbackRequests.size(), 6u);
}
#endif
