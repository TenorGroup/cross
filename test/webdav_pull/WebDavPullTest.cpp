#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "src/network/WebDavPull.h"

namespace {

std::string response(std::string_view href, bool collection = false, std::string_view length = "12") {
  std::string xml = "<D:response><D:href>";
  xml += href;
  xml += "</D:href><D:propstat><D:prop>";
  if (collection) {
    xml += "<D:resourcetype><D:collection/></D:resourcetype>";
  } else {
    xml += "<D:resourcetype/><D:getcontentlength>";
    xml += length;
    xml += "</D:getcontentlength><D:getcontenttype>application/epub+zip</D:getcontenttype>";
  }
  xml += "</D:prop></D:propstat></D:response>";
  return xml;
}

std::string multistatus(std::string body) {
  return "<?xml version=\"1.0\"?><D:multistatus xmlns:D=\"DAV:\">" + body + "</D:multistatus>";
}

webdav::PropfindEntry epubEntry(std::string path) {
  webdav::PropfindEntry entry;
  entry.href = path;
  entry.path = std::move(path);
  entry.contentType = "application/epub+zip";
  entry.contentLength = 32;
  return entry;
}

}  // namespace

TEST(WebDavPullParser, Parses207EntriesAndDecodesHref) {
  const auto parsed = webdav::parsePropfind207(
      multistatus(response("/Books/" , true) + response("/Books/one%20book.epub")));
  ASSERT_TRUE(parsed.ok());
  ASSERT_EQ(parsed.entries.size(), 2u);
  EXPECT_TRUE(parsed.entries[0].collection);
  EXPECT_EQ(parsed.entries[0].depth, 1u);
  EXPECT_EQ(parsed.entries[1].path, "/Books/one book.epub");
  EXPECT_EQ(parsed.entries[1].contentLength, 12u);
}

TEST(WebDavPullParser, RejectsMalformedXml) {
  const auto parsed = webdav::parsePropfind207("<D:multistatus><D:response><D:href>/a.epub</D:href>");
  EXPECT_EQ(parsed.error, webdav::ParseError::malformed_xml);
  EXPECT_EQ(webdav::parsePropfind207(multistatus("") + "garbage").error, webdav::ParseError::malformed_xml);
}

TEST(WebDavPullParser, RejectsOversizedXmlHrefPropertyAndEntry) {
  const std::string oversizedText = std::string("<D:response><D:href>/a.epub</D:href><D:propstat><D:prop>") +
                                    "<D:displayname>" +
                                    std::string(webdav::WebDavPullLimits::maxXmlText + 1, 'x') +
                                    "</D:displayname></D:prop></D:propstat></D:response>";
  EXPECT_EQ(webdav::parsePropfind207(multistatus(oversizedText)).error, webdav::ParseError::oversized_xml);

  const std::string largeHref = "/" + std::string(webdav::WebDavPullLimits::maxHref, 'a');
  EXPECT_EQ(webdav::parsePropfind207(multistatus(response(largeHref))).error, webdav::ParseError::oversized_href);

  const std::string property = std::string(webdav::WebDavPullLimits::maxProperty + 1, 'x');
  EXPECT_EQ(webdav::parsePropfind207(multistatus("<D:response><D:href>/a.epub</D:href><D:propstat><D:prop>"
                                                "<D:getcontenttype>" + property +
                                                "</D:getcontenttype></D:prop></D:propstat></D:response>"))
                .error,
            webdav::ParseError::oversized_property);

  std::string oversizedEntry = response("/a.epub");
  oversizedEntry.insert(oversizedEntry.find("</D:response>"), "<!--" + std::string(1400, 'x') + "-->");
  EXPECT_EQ(webdav::parsePropfind207(multistatus(oversizedEntry)).error, webdav::ParseError::oversized_entry);
}

TEST(WebDavPullParser, AcceptsLarge207BodyWhenEachFieldIsBounded) {
  std::string body;
  for (int i = 0; i < 665; ++i) body += response("/book" + std::to_string(i) + ".epub");
  const auto parsed = webdav::parsePropfind207(multistatus(std::move(body)));
  ASSERT_TRUE(parsed.ok());
  EXPECT_EQ(parsed.entries.size(), 665u);
}

namespace {
bool collectEntry(const webdav::PropfindEntry& entry, void* context) {
  auto* const paths = static_cast<std::vector<std::string>*>(context);
  paths->push_back(entry.path);
  return true;
}
}  // namespace

TEST(WebDavPullParser, StreamsLarge207OneEntryAtATime) {
  std::string xml = multistatus("");
  for (int i = 0; i < 665; ++i) xml.insert(xml.size() - 16, response("/book" + std::to_string(i) + ".epub"));
  std::vector<std::string> paths;
  webdav::PropfindStreamParser parser;
  for (std::size_t offset = 0; offset < xml.size();) {
    const std::size_t count = std::min<std::size_t>(37, xml.size() - offset);
    ASSERT_TRUE(parser.feed(reinterpret_cast<const std::uint8_t*>(xml.data() + offset), count, collectEntry, &paths))
        << "error=" << static_cast<int>(parser.error()) << " offset=" << offset;
    offset += count;
  }
  ASSERT_TRUE(parser.finish());
  EXPECT_EQ(parser.entries(), 665u);
  ASSERT_EQ(paths.size(), 665u);
  EXPECT_EQ(paths.front(), "/book0.epub");
  EXPECT_EQ(paths.back(), "/book664.epub");
}

TEST(WebDavPullParser, RejectsPathAndDepthLimits) {
  const std::string longPath = "/" + std::string(webdav::WebDavPullLimits::maxPath, 'a');
  EXPECT_EQ(webdav::parsePropfind207(multistatus(response(longPath))).error, webdav::ParseError::invalid_path);

  EXPECT_EQ(webdav::parsePropfind207(multistatus(response("/1/2/3/4/5/6/7/8/9/a.epub"))).error,
            webdav::ParseError::too_deep);
  EXPECT_EQ(webdav::parsePropfind207(multistatus(response("/../a.epub"))).error, webdav::ParseError::invalid_path);
  EXPECT_EQ(webdav::parsePropfind207(multistatus(response("//a.epub"))).error, webdav::ParseError::unsafe_origin);
}

TEST(WebDavPullParser, RejectsUnsafeAbsoluteHref) {
  EXPECT_EQ(webdav::parsePropfind207(multistatus(response("https://other.example/a.epub")), "https://books.example")
                .error,
            webdav::ParseError::unsafe_origin);
  EXPECT_TRUE(webdav::parsePropfind207(multistatus(response("https://books.example/a.epub")), "https://books.example")
                  .ok());
}

TEST(WebDavPullRedirect, AllowsSameOriginUpgradeAndRejectsCrossOriginOrDowngrade) {
  EXPECT_TRUE(webdav::redirectAllowed("http://books.example/catalog", "https://books.example/book.epub"));
  EXPECT_TRUE(webdav::redirectAllowed("https://books.example/catalog", "https://books.example/book.epub"));
  EXPECT_FALSE(webdav::redirectAllowed("https://books.example/catalog", "http://books.example/book.epub"));
  EXPECT_FALSE(webdav::redirectAllowed("https://books.example/catalog", "https://cdn.example/book.epub"));
}

TEST(WebDavPullEpub, RequiresUncompressedMimetypeFirstEntry) {
  std::string valid(30, '\0');
  valid[0] = 'P'; valid[1] = 'K'; valid[2] = 3; valid[3] = 4;
  valid[26] = 8;
  valid += "mimetypeapplication/epub+zip";
  EXPECT_TRUE(webdav::isValidEpub(valid));
  valid[8] = 8;
  EXPECT_FALSE(webdav::isValidEpub(valid));
  EXPECT_FALSE(webdav::isValidEpub("PK\x03\x04garbage"));
}

TEST(WebDavPullQueue, ReportsStatusesAndRejectsInvalidEpub) {
  const auto entry = epubEntry("/book.epub");
  webdav::PullQueue queue;
  EXPECT_EQ(queue.submit(entry, true, true).status, webdav::PullStatus::skipped_existing);
  EXPECT_EQ(queue.submit(entry, false, true).status, webdav::PullStatus::downloaded);
  EXPECT_EQ(queue.submit(entry, false, true).status, webdav::PullStatus::collision);
  EXPECT_EQ(queue.submit(epubEntry("/bad.epub"), false, true, false).status, webdav::PullStatus::invalid);
  EXPECT_EQ(queue.submit(epubEntry("/failed.epub"), false, false).status, webdav::PullStatus::failed);
  EXPECT_EQ(queue.size(), 1u);
}

// Measured on the X4C 18/10/2026: a transfer cut off mid-body was shown as "Invalid EPUB", and one
// broken EPUB on the server stopped every file listed after it, on every later pull.
TEST(WebDavPullQueue, CutTransferIsFailedAndOnlyFailedStopsThePull) {
  EXPECT_EQ(webdav::downloadStatus(epubEntry("/cut.epub"), false, false, false), webdav::PullStatus::failed);
  EXPECT_EQ(webdav::downloadStatus(epubEntry("/bad.epub"), false, true, false), webdav::PullStatus::invalid);
  EXPECT_EQ(webdav::downloadStatus(epubEntry("/ok.epub"), false, true, true), webdav::PullStatus::downloaded);
  EXPECT_EQ(webdav::downloadStatus(epubEntry("/ok.epub"), true, false, false), webdav::PullStatus::skipped_existing);
  webdav::PullQueue queue;
  EXPECT_EQ(queue.submit(epubEntry("/cut.epub"), false, false, false).status, webdav::PullStatus::failed);

  EXPECT_TRUE(webdav::pullContinuesAfter(webdav::PullStatus::invalid));
  EXPECT_TRUE(webdav::pullContinuesAfter(webdav::PullStatus::skipped_existing));
  EXPECT_TRUE(webdav::pullContinuesAfter(webdav::PullStatus::collision));
  EXPECT_TRUE(webdav::pullContinuesAfter(webdav::PullStatus::downloaded));
  EXPECT_FALSE(webdav::pullContinuesAfter(webdav::PullStatus::failed));
}

TEST(WebDavPullQueue, Enforces128KiBByteCap) {
  webdav::PullQueue queue;
  webdav::QueueResult last;
  for (int i = 0; i < 200; ++i) {
    auto entry = epubEntry("/book" + std::to_string(i) + ".epub");
    entry.href += "?" + std::string(900, 'x');
    last = queue.submit(entry, false, true);
    if (last.status == webdav::PullStatus::failed) break;
  }
  EXPECT_EQ(last.status, webdav::PullStatus::failed);
  EXPECT_LE(queue.bytes(), webdav::WebDavPullLimits::maxQueueBytes);
  EXPECT_GT(queue.bytes(), webdav::WebDavPullLimits::maxQueueBytes - 2000u);
}

// A Vietnamese title with an ellipsis is listed by the parser, so the download must take it too.
TEST(WebDavPullPath, LocalPathRefusesOnlyDotSegments) {
  EXPECT_TRUE(webdav::validLocalPath("/Truyện... tập 1.epub"));
  EXPECT_TRUE(webdav::validLocalPath("/Books/a..b.epub"));
  EXPECT_TRUE(webdav::validLocalPath("/..hidden.epub"));
  EXPECT_FALSE(webdav::validLocalPath("/Books/../x.epub"));
  EXPECT_FALSE(webdav::validLocalPath("/.."));
  EXPECT_FALSE(webdav::validLocalPath("/a/.."));
  EXPECT_FALSE(webdav::validLocalPath("/./x.epub"));
  EXPECT_FALSE(webdav::validLocalPath("x.epub"));
  EXPECT_FALSE(webdav::validLocalPath("//x.epub"));
  EXPECT_FALSE(webdav::validLocalPath("/a\\b.epub"));
  EXPECT_FALSE(webdav::validLocalPath(std::string("/a\x01.epub")));
}

// A folder with no EPUB in it (only folders or TXT) left the screen on "Reading WebDAV listing".
TEST(WebDavPullStatus, AListingWithNothingToFetchStillCompletes) {
  EXPECT_EQ(webdav::statusAfterPull(WebDavPullStatus::discovering), WebDavPullStatus::complete);
  EXPECT_EQ(webdav::statusAfterPull(WebDavPullStatus::downloading), WebDavPullStatus::complete);
  EXPECT_EQ(webdav::statusAfterPull(WebDavPullStatus::failed), WebDavPullStatus::failed);
}
