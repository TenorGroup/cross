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
