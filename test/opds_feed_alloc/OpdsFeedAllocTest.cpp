// The OPDS 2.0 parser is one block of sizeof(Opds2Parser) (14,592 B on the device) taken on the first JSON
// byte, inside the TLS session. The app hands the feed parser the heap floor's door for that block.
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "OpdsFeedParser.h"

namespace {
const char kFeed[] =
    R"({"metadata":{"title":"Shelf"},"navigation":[{"href":"/new","title":"New","rel":"current"}],)"
    R"("publications":[{"metadata":{"title":"A Book","identifier":"urn:1"},)"
    R"("links":[{"href":"/a.epub","type":"application/epub+zip","rel":"http://opds-spec.org/acquisition/open-access"}]}]})";

size_t asked = 0;
int taken = 0;
int refuse = 0;
void* door(const size_t bytes) {
  asked = bytes;
  ++taken;
  return refuse ? nullptr : std::malloc(bytes);
}
void feed(OpdsFeedParser& parser, const std::string& body) {
  parser.write(reinterpret_cast<const uint8_t*>(body.data()), body.size());
  parser.flush();
}
}  // namespace

class OpdsFeedAlloc : public testing::Test {
 protected:
  void SetUp() override { asked = 0, taken = 0, refuse = 0; }
};

TEST_F(OpdsFeedAlloc, TheJsonParserBlockComesFromTheDoor) {
  OpdsFeedParser parser;
  parser.setBlockAllocator(&door);
  feed(parser, kFeed);
  EXPECT_EQ(taken, 1);
  EXPECT_EQ(asked, sizeof(Opds2Parser));
  EXPECT_FALSE(parser.error());
  EXPECT_FALSE(parser.allocationFailed());
  EXPECT_TRUE(parser.isOpds2());
  EXPECT_EQ(parser.getFeedTitle(), "Shelf");
  EXPECT_GE(parser.takeEntries().size(), 1u);
}

TEST_F(OpdsFeedAlloc, AReusedParserTakesAFreshBlockAfterReset) {
  OpdsFeedParser parser;
  parser.setBlockAllocator(&door);
  feed(parser, kFeed);
  parser.reset();
  EXPECT_FALSE(parser.isOpds2());
  feed(parser, kFeed);
  EXPECT_EQ(taken, 2);
  EXPECT_FALSE(parser.error());
}

TEST_F(OpdsFeedAlloc, ARefusedBlockFailsTheFeedAndSaysWhy) {
  refuse = 1;
  OpdsFeedParser parser;
  parser.setBlockAllocator(&door);
  feed(parser, kFeed);
  EXPECT_EQ(taken, 1);
  EXPECT_TRUE(parser.error());
  EXPECT_TRUE(parser.allocationFailed());
  EXPECT_FALSE(parser.isOpds2());
  EXPECT_TRUE(parser.takeEntries().empty());
}

TEST_F(OpdsFeedAlloc, AtomFeedsDoNotAskForTheBlock) {
  refuse = 1;
  OpdsFeedParser parser;
  parser.setBlockAllocator(&door);
  feed(parser, R"(<?xml version="1.0"?><feed xmlns="http://www.w3.org/2005/Atom"><title>T</title></feed>)");
  EXPECT_EQ(taken, 0);
  EXPECT_FALSE(parser.allocationFailed());
}

TEST_F(OpdsFeedAlloc, WithoutADoorTheParserStillWorks) {
  OpdsFeedParser parser;
  feed(parser, kFeed);
  EXPECT_FALSE(parser.error());
  EXPECT_EQ(parser.getFeedTitle(), "Shelf");
}
