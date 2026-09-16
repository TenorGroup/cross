#include <gtest/gtest.h>

#include "SecureHttpClient.h"
// Test the production HTTP parser and redirect logic. TLS transport is stubbed.
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
}  // namespace freeink
class Transport : public testing::Test {
 protected:
  void SetUp() override { wire::reset(); }
};
static std::string redirect(const std::string& to) {
  return "HTTP/1.1 302 Found\r\nLocation: " + to + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
}
static std::string success() { return "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK"; }
TEST_F(Transport, StripsCredentialsAndCustomSecretsAcrossOrigin) {
  wire::replies = {redirect("https://evil.example/end"), success()};
  freeink::SecureHttpClient h;
  ASSERT_TRUE(h.begin("https://books.example/feed"));
  h.setBasicAuth("user", "secret");
  h.addHeader("Cookie", "session=fixture");
  h.setFollowRedirects(2);
  EXPECT_EQ(h.GET(), 200);
  ASSERT_EQ(wire::requests.size(), 2);
  EXPECT_NE(wire::requests[0].bytes.find("Authorization: Basic"), std::string::npos);
  EXPECT_EQ(wire::requests[1].host, "evil.example");
  EXPECT_EQ(wire::requests[1].bytes.find("Authorization:"), std::string::npos);
  EXPECT_EQ(wire::requests[1].bytes.find("Cookie:"), std::string::npos);
}
TEST_F(Transport, PreservesCredentialsOnSameOrigin) {
  wire::replies = {redirect("/next"), success()};
  freeink::SecureHttpClient h;
  ASSERT_TRUE(h.begin("https://books.example/feed"));
  h.setBasicAuth("user", "secret");
  h.setFollowRedirects(2);
  EXPECT_EQ(h.GET(), 200);
  ASSERT_EQ(wire::requests.size(), 2);
  EXPECT_NE(wire::requests[1].bytes.find("Authorization: Basic"), std::string::npos);
}
TEST_F(Transport, RejectsTlsDowngradeWithoutSendingSecondRequest) {
  wire::replies = {redirect("http://books.example/end"), success()};
  freeink::SecureHttpClient h;
  ASSERT_TRUE(h.begin("https://books.example/feed"));
  h.setBasicAuth("user", "secret");
  h.setFollowRedirects(2);
  EXPECT_EQ(h.GET(), 302);
  EXPECT_EQ(wire::requests.size(), 1);
}
TEST_F(Transport, HandlesProtocolRelativeRedirectAndChangedPort) {
  wire::replies = {redirect("//books.example:444/end"), success()};
  freeink::SecureHttpClient h;
  ASSERT_TRUE(h.begin("https://books.example/feed"));
  h.setBasicAuth("user", "secret");
  h.setFollowRedirects(2);
  EXPECT_EQ(h.GET(), 200);
  ASSERT_EQ(wire::requests.size(), 2);
  EXPECT_EQ(wire::requests[1].port, 444);
  EXPECT_EQ(wire::requests[1].bytes.find("Authorization:"), std::string::npos);
}
TEST_F(Transport, RejectsInjectedUrlAndHeader) {
  freeink::SecureHttpClient h;
  EXPECT_FALSE(h.begin("https://books.example/a\r\nInjected: true"));
  ASSERT_TRUE(h.begin("https://books.example/a"));
  h.addHeader("X-Test", "ok\r\nInjected: true");
  wire::replies = {success()};
  EXPECT_EQ(h.GET(), 200);
  ASSERT_EQ(wire::requests.size(), 1);
  EXPECT_EQ(wire::requests[0].bytes.find("Injected:"), std::string::npos);
}
TEST_F(Transport, BoundsBufferedBodies) {
  wire::replies = {"HTTP/1.1 200 OK\r\nContent-Length: 65536\r\nConnection: close\r\n\r\n" + std::string(65536, 'x')};
  freeink::SecureHttpClient h;
  ASSERT_TRUE(h.begin("https://books.example/feed"));
  h.GET();
  EXPECT_FALSE(h.responseComplete());
  EXPECT_TRUE(h.callbackAborted());
  EXPECT_LE(h.getString().size(), 16384);
}
TEST_F(Transport, BoundsHeaderCount) {
  std::string response = "HTTP/1.1 200 OK\r\n";
  for (int i = 0; i < 1000; ++i) response += "X-Extra: value\r\n";
  response += "Content-Length: 2\r\n\r\nOK";
  wire::replies = {response};
  freeink::SecureHttpClient h;
  ASSERT_TRUE(h.begin("https://books.example/feed"));
  EXPECT_LT(h.GET(), 0);
}
TEST_F(Transport, RejectsTruncatedHeaders) {
  wire::replies = {"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n"};
  freeink::SecureHttpClient h;
  h.setTimeout(50);
  ASSERT_TRUE(h.begin("https://books.example/feed"));
  EXPECT_LT(h.GET(), 0);
}
TEST_F(Transport, BeginNewOriginClearsCredentials) {
  freeink::SecureHttpClient h;
  ASSERT_TRUE(h.begin("https://books.example/a"));
  h.setBasicAuth("user", "secret");
  ASSERT_TRUE(h.begin("https://other.example/b"));
  wire::replies = {success()};
  EXPECT_EQ(h.GET(), 200);
  ASSERT_EQ(wire::requests.size(), 1);
  EXPECT_EQ(wire::requests[0].bytes.find("Authorization:"), std::string::npos);
}
