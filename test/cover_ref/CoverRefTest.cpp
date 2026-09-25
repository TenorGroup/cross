// Home writes a missing card thumbnail from cover.ref instead of loading the book's index. A file
// it cannot trust (another version, cut short, damaged) must read as missing, so Home takes the old
// route and still writes the thumbnail.
#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "util/CoverRef.h"

namespace {
std::vector<uint8_t> bytesFor(const std::string& href) {
  std::vector<uint8_t> out(coverref::MAX_BYTES);
  out.resize(coverref::encode(href, out.data(), out.size()));
  return out;
}
}  // namespace

TEST(CoverRef, RoundTrip) {
  for (const std::string href : {"OEBPS/cover.jpg", "images/Bìa sách.png", ""}) {
    const auto bytes = bytesFor(href);
    ASSERT_EQ(bytes.size(), coverref::HEAD + href.size()) << href;
    std::string back = "stale";
    EXPECT_TRUE(coverref::decode(bytes.data(), bytes.size(), back)) << href;
    EXPECT_EQ(back, href);
  }
}

TEST(CoverRef, CarriesItsVersion) {
  const auto bytes = bytesFor("OEBPS/cover.jpg");
  ASSERT_GE(bytes.size(), coverref::HEAD);
  EXPECT_EQ(0, std::memcmp(bytes.data(), coverref::MAGIC, 4));
  EXPECT_EQ(bytes[4], coverref::VERSION);
}

TEST(CoverRef, AnotherVersionReadsAsMissing) {
  auto bytes = bytesFor("OEBPS/cover.jpg");
  ASSERT_FALSE(bytes.empty());
  bytes[4] = coverref::VERSION + 1;
  std::string href;
  EXPECT_FALSE(coverref::decode(bytes.data(), bytes.size(), href));
}

TEST(CoverRef, CutOrDamagedReadsAsMissing) {
  const auto bytes = bytesFor("OEBPS/cover.jpg");
  ASSERT_FALSE(bytes.empty());
  std::string href;
  for (size_t cut = 0; cut < bytes.size(); cut++) EXPECT_FALSE(coverref::decode(bytes.data(), cut, href)) << cut;
  auto longer = bytes;
  longer.push_back('x');
  EXPECT_FALSE(coverref::decode(longer.data(), longer.size(), href));
  auto magic = bytes;
  magic[0] = 'X';
  EXPECT_FALSE(coverref::decode(magic.data(), magic.size(), href));
  EXPECT_FALSE(coverref::decode(nullptr, 0, href));
}

TEST(CoverRef, APathTooLongIsNotWritten) {
  std::vector<uint8_t> out(coverref::MAX_BYTES + 10);
  EXPECT_EQ(coverref::encode(std::string(256, 'a'), out.data(), out.size()), 0u);
  EXPECT_EQ(coverref::encode(std::string(255, 'a'), out.data(), out.size()), coverref::MAX_BYTES);
  EXPECT_EQ(coverref::encode("OEBPS/cover.jpg", out.data(), 10), 0u);
}
