#include <Epub/parsers/ContentOpfParser.h>
#include <Epub/BookMetadataCache.h>
#include <gtest/gtest.h>

namespace {
const std::string cachePath = "/cache", basePath = "OPS/";
const std::string manifest = "<package><manifest><item id='one' href='one.xhtml' media-type='application/xhtml+xml'/></manifest>";
const std::string spine = "<spine><itemref idref='one'/></spine></package>";
class OpfCacheIo : public testing::Test {
 void SetUp() override { opfFaults = {}; Storage.files.clear(); }
 void TearDown() override { opfFaults = {}; }
};
size_t feed(ContentOpfParser& parser, const std::string& value) {
 return parser.write(reinterpret_cast<const uint8_t*>(value.data()), value.size());
}
}
TEST_F(OpfCacheIo, ManifestToSpineRetainsHref) {
 BookMetadataCache cache;
 ContentOpfParser parser(cachePath, basePath, manifest.size() + spine.size(), &cache);
 ASSERT_TRUE(parser.setup());
 ASSERT_EQ(feed(parser, manifest), manifest.size());
 ASSERT_EQ(feed(parser, spine), spine.size());
 ASSERT_EQ(cache.entries.size(), 1u);
 EXPECT_EQ(cache.entries[0], "OPS/one.xhtml");
}
TEST_F(OpfCacheIo, NegativeTempReadFailsParserWithoutSpineEntry) {
 BookMetadataCache cache;
 ContentOpfParser parser(cachePath, basePath, manifest.size() + spine.size(), &cache);
 ASSERT_TRUE(parser.setup());
 ASSERT_EQ(feed(parser, manifest), manifest.size());
 opfFaults.read = true;
 EXPECT_EQ(feed(parser, spine), 0u);
 EXPECT_TRUE(cache.entries.empty());
 EXPECT_EQ(feed(parser, spine), 0u);
}
TEST_F(OpfCacheIo, TempWriteFailureStopsManifest) {
 BookMetadataCache cache;
 ContentOpfParser parser(cachePath, basePath, manifest.size() + spine.size(), &cache);
 ASSERT_TRUE(parser.setup());
 opfFaults.write = true;
 EXPECT_EQ(feed(parser, manifest), 0u);
 EXPECT_TRUE(cache.entries.empty());
}
TEST_F(OpfCacheIo, TempSeekFailureStopsSpine) {
 BookMetadataCache cache;
 ContentOpfParser parser(cachePath, basePath, manifest.size() + spine.size(), &cache);
 ASSERT_TRUE(parser.setup());
 ASSERT_EQ(feed(parser, manifest), manifest.size());
 opfFaults.seek = true;
 EXPECT_EQ(feed(parser, spine), 0u);
 EXPECT_TRUE(cache.entries.empty());
}
TEST_F(OpfCacheIo, TempSyncFailureStopsManifest) {
 BookMetadataCache cache;
 ContentOpfParser parser(cachePath, basePath, manifest.size() + spine.size(), &cache);
 ASSERT_TRUE(parser.setup());
 opfFaults.sync = true;
 EXPECT_EQ(feed(parser, manifest), 0u);
}
TEST_F(OpfCacheIo, TempOpenReadFailureStopsSpine) {
 BookMetadataCache cache;
 ContentOpfParser parser(cachePath, basePath, manifest.size() + spine.size(), &cache);
 ASSERT_TRUE(parser.setup());
 ASSERT_EQ(feed(parser, manifest), manifest.size());
 opfFaults.openRead = true;
 EXPECT_EQ(feed(parser, spine), 0u);
}
