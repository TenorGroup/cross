// The headless OPDS implicit login (Opds/src/OpdsImplicitAuth.cpp) over the stubbed TLS wire. It sends the
// stored password in a form POST, so it must judge the server's certificate and must not follow a redirect
// from https to http.
#include <gtest/gtest.h>

#include "OpdsImplicitAuth.h"
#include "SecureClientStub.h"

namespace {
class ImplicitLogin : public testing::Test {
 protected:
  void SetUp() override { wire::reset(); }
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
