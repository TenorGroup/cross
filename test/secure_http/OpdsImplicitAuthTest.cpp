// The headless OPDS implicit login (Opds/src/OpdsImplicitAuth.cpp) over the stubbed TLS wire. It sends the
// stored password in a form POST, so it must judge the server's certificate and must not follow a redirect
// from https to http.
#include <gtest/gtest.h>

#include "OpdsImplicitAuth.h"
#include "SecureClientStub.h"

namespace {
class ImplicitLogin : public testing::Test {
 protected:
  void SetUp() override {
    wire::reset();
    ESP = {};
  }
};
std::string redirect(const std::string& to) {
  return "HTTP/1.1 302 Found\r\nLocation: " + to + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
}
std::string page(const std::string& html) {
  return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(html.size()) + "\r\nConnection: close\r\n\r\n" + html;
}
const char* const ROOTS = "pem-roots";
const OpdsRootsFor ASK = [](const std::string&) { return ROOTS; };
const std::string LOGIN_FORM =
    "<html><form action=\"/signin\" method=\"post\"><input type=\"text\" name=\"user\">"
    "<input type=\"password\" name=\"pw\"></form></html>";
}  // namespace

TEST_F(ImplicitLogin, EveryHttpsStepJudgesTheCertificateAgainstTheRootsTheAppGives) {
  wire::replies = {page(LOGIN_FORM), redirect("opds://authorize/#access_token=T")};
  std::string token;
  EXPECT_TRUE(opdsImplicitAuthenticate("https://books.example/authorize", "reader", "hunter2", token, ASK));
  EXPECT_EQ(token, "T");
  ASSERT_EQ(wire::tls.size(), 2u);
  for (const auto& mode : wire::tls) {
    EXPECT_FALSE(mode.insecure);
    EXPECT_EQ(mode.roots, ROOTS);
  }
}

TEST_F(ImplicitLogin, WithoutRootsNothingIsSent) {
  wire::replies = {page(LOGIN_FORM)};
  std::string token;
  EXPECT_FALSE(opdsImplicitAuthenticate("https://books.example/authorize", "reader", "hunter2", token,
                                        [](const std::string&) -> const char* { return nullptr; }));
  EXPECT_FALSE(opdsImplicitAuthenticate("https://books.example/authorize", "reader", "hunter2", token, nullptr));
  EXPECT_TRUE(wire::requests.empty());
}

TEST_F(ImplicitLogin, APlainHttpServerTheUserTypedStaysPlainHttp) {
  wire::replies = {page(LOGIN_FORM), redirect("opds://authorize/#access_token=T")};
  std::string token;
  EXPECT_TRUE(opdsImplicitAuthenticate("http://books.example/authorize", "reader", "hunter2", token, ASK));
  EXPECT_TRUE(wire::tls.empty());
}

TEST_F(ImplicitLogin, AnHttpsToHttpRedirectStopsTheLoginBeforeAnyPasswordIsSent) {
  wire::replies = {redirect("http://books.example/login"), page(LOGIN_FORM), page(LOGIN_FORM)};
  std::string token;
  EXPECT_FALSE(opdsImplicitAuthenticate("https://books.example/authorize", "reader", "hunter2", token, ASK));
  EXPECT_EQ(wire::requests.size(), 1u);
  for (const auto& request : wire::requests) EXPECT_EQ(request.bytes.find("hunter2"), std::string::npos);
}

TEST_F(ImplicitLogin, AnHttpsToHttpRedirectLaterInTheChainStopsIt) {
  wire::replies = {redirect("https://sso.example/login"), redirect("http://sso.example/login"), page(LOGIN_FORM)};
  std::string token;
  EXPECT_FALSE(opdsImplicitAuthenticate("https://books.example/authorize", "reader", "hunter2", token, ASK));
  EXPECT_EQ(wire::requests.size(), 2u);
}

// The login page is held in one string that grows to 16 KB while the TLS session is live (about 50 KB on the heap): a
// block from 4 KB up is asked of the heap floor (release rule 20) before it is taken, and a login the heap cannot hold
// stops, with nothing sent, instead of taking the heap under the floor (or aborting when the allocation fails).
TEST_F(ImplicitLogin, APageTheHeapCannotHoldStopsTheLoginBeforeAnythingIsSent) {
  const std::string big = "<!-- " + std::string(6000, 'x') + " -->" + LOGIN_FORM;
  wire::replies = {page(big), redirect("opds://authorize/#access_token=T")};
  ESP.freeHeap = 15 * 1024;  // 6 KB more leaves 9 KB, under the floor of 10 KB
  ESP.maxAlloc = 15 * 1024;
  std::string token;
  EXPECT_FALSE(opdsImplicitAuthenticate("https://books.example/authorize", "reader", "hunter2", token, ASK));
  for (const auto& request : wire::requests) EXPECT_EQ(request.bytes.find("hunter2"), std::string::npos);
  EXPECT_EQ(wire::requests.size(), 1u) << "the password went out although the page could not be held";
}

TEST_F(ImplicitLogin, TheSamePageOnAHeapThatHoldsItLogsIn) {
  const std::string big = "<!-- " + std::string(6000, 'x') + " -->" + LOGIN_FORM;
  wire::replies = {page(big), redirect("opds://authorize/#access_token=T")};
  ESP.freeHeap = 64 * 1024;
  ESP.maxAlloc = 40 * 1024;
  std::string token;
  EXPECT_TRUE(opdsImplicitAuthenticate("https://books.example/authorize", "reader", "hunter2", token, ASK));
  EXPECT_EQ(token, "T");
}

// A page under 4 KB is an allocation the floor does not govern: a low heap does not turn it away.
TEST_F(ImplicitLogin, ASmallPageIsNotTurnedAwayOnALowHeap) {
  wire::replies = {page(LOGIN_FORM), redirect("opds://authorize/#access_token=T")};
  ESP.freeHeap = 12 * 1024;
  ESP.maxAlloc = 9 * 1024;
  std::string token;
  EXPECT_TRUE(opdsImplicitAuthenticate("https://books.example/authorize", "reader", "hunter2", token, ASK));
}
