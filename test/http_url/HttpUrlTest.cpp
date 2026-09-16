#include <gtest/gtest.h>

#include "HttpUrl.h"

using namespace freeink::http_url;
TEST(HttpUrl, KeepsCredentialsOnlyOnSameOrigin) {
  EXPECT_TRUE(sameOrigin("https://Books.example/a", "HTTPS://books.example:443/b"));
  EXPECT_FALSE(sameOrigin("https://books.example/a", "https://evil.example/a"));
  EXPECT_FALSE(sameOrigin("https://books.example/a", "https://books.example:444/b"));
  EXPECT_FALSE(sameOrigin("https://books.example/a", "http://books.example/a"));
}
TEST(HttpUrl, RejectsDowngradeAndMalformedRedirect) {
  EXPECT_FALSE(redirectAllowed("https://books.example/a", "http://books.example/a"));
  EXPECT_FALSE(redirectAllowed("HTTPS://books.example/a", "http://other.example/a"));
  EXPECT_TRUE(redirectAllowed("http://books.example/a", "https://books.example/a"));
  EXPECT_TRUE(redirectAllowed("https://books.example/a", "https://cdn.example/a"));
  EXPECT_FALSE(redirectAllowed("https://books.example/a", "ftp://books.example/a"));
}
TEST(HttpUrl, RejectsInjectionCredentialsAndInvalidPorts) {
  Parts p;
  for (const auto url : {"https://x/a\r\nInjected: 1", "https://x/a b", "https://user:pass@host/a", "https://x:0/",
                         "https://x:65536/", "https://x:443evil/", "https://x:/", "https:///a", "https://x\\evil/a"}) {
    EXPECT_FALSE(parse(url, p)) << url;
  }
  EXPECT_FALSE(parse(std::string_view("https://x/\0evil", 15), p));
}
TEST(HttpUrl, SeparatesQueryAndFragment) {
  Parts p;
  ASSERT_TRUE(parse("https://x?q=1#local", p));
  EXPECT_EQ(p.host, "x");
  EXPECT_EQ(p.target, "?q=1");
  EXPECT_EQ(p.port, 443);
}
