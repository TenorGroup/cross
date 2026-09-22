// Quote records must carry the anchor that lets a saved quote be found again on a
// later render, and must keep loading the records written before the anchor existed.
#include <HalStorage.h>
#include <QuoteStore.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::string quoteDirectory() { return fixtureRoot + "/.crosspoint/quotes"; }

// The store names a file after a hash of path, text, spine and page, so a test that
// wants to read a record back has to find the file the store just wrote.
std::string onlyQuoteFileName() {
  std::string found;
  for (const auto& entry : std::filesystem::directory_iterator(quoteDirectory())) {
    if (entry.path().extension() != ".json") continue;
    EXPECT_TRUE(found.empty()) << "expected exactly one quote file";
    found = entry.path().filename().string();
  }
  return found;
}

std::string fileText(const std::string& name) {
  std::ifstream input(quoteDirectory() + "/" + name);
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

QuoteRecord sampleQuote() {
  QuoteRecord quote;
  quote.path = "/sach/audit.epub";
  quote.title = "Synonym Lookup Test";
  quote.text = "position. Clear";
  quote.spine = 3;
  quote.page = 7;
  quote.day = 20260922;
  return quote;
}

class QuoteAnchorStoreTest : public ::testing::Test {
 protected:
  std::filesystem::path root;
  void SetUp() override {
    root = std::filesystem::temp_directory_path() /
           ("cross-quote-" + std::to_string(getpid()) + "-" +
            ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    fixtureRoot = root.string();
  }
  void TearDown() override {
    std::filesystem::remove_all(root);
    fixtureRoot.clear();
  }
};

TEST_F(QuoteAnchorStoreTest, AnchorSurvivesSaveAndLoad) {
  QuoteRecord saved = sampleQuote();
  saved.hasAnchor = true;
  saved.anchorStart = 12345;
  saved.anchorEnd = 12360;
  ASSERT_TRUE(quotes::save(saved));

  const std::string name = onlyQuoteFileName();
  ASSERT_FALSE(name.empty());
  QuoteRecord loaded;
  ASSERT_TRUE(quotes::load(name, loaded));
  EXPECT_TRUE(loaded.hasAnchor);
  EXPECT_EQ(loaded.anchorStart, 12345u);
  EXPECT_EQ(loaded.anchorEnd, 12360u);
  EXPECT_EQ(loaded.text, saved.text);
  EXPECT_EQ(loaded.spine, saved.spine);
  EXPECT_EQ(loaded.page, saved.page);
  EXPECT_EQ(loaded.day, saved.day);
}

// The anchor is two extra keys inside the existing schema 1, so a record written by a
// build without highlighting still loads, keeps every field it always had, and is just
// not drawn.
TEST_F(QuoteAnchorStoreTest, RecordWithoutAnchorStillLoads) {
  std::filesystem::create_directories(quoteDirectory());
  const std::string name = "00112233445566ff.json";
  {
    std::ofstream output(quoteDirectory() + "/" + name);
    output << R"({"schema":1,"path":"/audit.epub","title":"Synonym Lookup Test",)"
           << R"("text":"position.","spine":0,"page":0,"day":20260921})";
  }
  QuoteRecord loaded;
  ASSERT_TRUE(quotes::load(name, loaded));
  EXPECT_FALSE(loaded.hasAnchor);
  EXPECT_EQ(loaded.text, "position.");
  EXPECT_EQ(loaded.title, "Synonym Lookup Test");
  EXPECT_EQ(loaded.path, "/audit.epub");
  EXPECT_EQ(loaded.day, 20260921u);
}

// An unanchored save must not write the keys at all, so an older build reading the same
// card sees exactly the file it used to write.
TEST_F(QuoteAnchorStoreTest, UnanchoredSaveWritesNoAnchorKeys) {
  ASSERT_TRUE(quotes::save(sampleQuote()));
  const std::string name = onlyQuoteFileName();
  ASSERT_FALSE(name.empty());
  const std::string text = fileText(name);
  EXPECT_EQ(text.find("\"vo\""), std::string::npos) << text;
  EXPECT_EQ(text.find("\"ve\""), std::string::npos) << text;
  QuoteRecord loaded;
  ASSERT_TRUE(quotes::load(name, loaded));
  EXPECT_FALSE(loaded.hasAnchor);
}

// The reader holds anchors, not quote text: one scan of the directory when the book
// opens, and only the quotes of that book.
TEST_F(QuoteAnchorStoreTest, LoadAnchorsKeepsOnlyThisBook) {
  QuoteRecord mine = sampleQuote();
  mine.hasAnchor = true;
  mine.anchorStart = 100;
  mine.anchorEnd = 118;
  ASSERT_TRUE(quotes::save(mine));

  QuoteRecord second = mine;
  second.text = "another saved run of words";
  second.spine = 4;
  second.page = 9;
  second.anchorStart = 900;
  second.anchorEnd = 926;
  ASSERT_TRUE(quotes::save(second));

  QuoteRecord other = sampleQuote();
  other.path = "/sach/khac.epub";
  other.hasAnchor = true;
  other.anchorStart = 5;
  other.anchorEnd = 20;
  ASSERT_TRUE(quotes::save(other));

  QuoteRecord unanchored = sampleQuote();
  unanchored.text = "saved before anchors existed";
  unanchored.page = 11;
  ASSERT_TRUE(quotes::save(unanchored));

  std::vector<QuoteAnchor> anchors;
  quotes::loadAnchors("/sach/audit.epub", anchors);
  ASSERT_EQ(anchors.size(), 2u);
  bool sawSpineThree = false, sawSpineFour = false;
  for (const auto& anchor : anchors) {
    if (anchor.spine == 3) {
      sawSpineThree = true;
      EXPECT_EQ(anchor.start, 100u);
      EXPECT_EQ(anchor.end, 118u);
    }
    if (anchor.spine == 4) {
      sawSpineFour = true;
      EXPECT_EQ(anchor.start, 900u);
      EXPECT_EQ(anchor.end, 926u);
    }
  }
  EXPECT_TRUE(sawSpineThree);
  EXPECT_TRUE(sawSpineFour);
}

// Highlighting the same words again on a card that already holds the pre-anchor record
// must start drawing them, instead of hitting the duplicate guard and staying invisible.
TEST_F(QuoteAnchorStoreTest, ReSavingAPreAnchorRecordAddsTheAnchor) {
  ASSERT_TRUE(quotes::save(sampleQuote()));
  const std::string name = onlyQuoteFileName();
  ASSERT_FALSE(name.empty());

  QuoteRecord again = sampleQuote();
  again.hasAnchor = true;
  again.anchorStart = 400;
  again.anchorEnd = 415;
  ASSERT_TRUE(quotes::save(again));

  EXPECT_EQ(onlyQuoteFileName(), name) << "the duplicate guard must still keep one file";
  QuoteRecord loaded;
  ASSERT_TRUE(quotes::load(name, loaded));
  EXPECT_TRUE(loaded.hasAnchor);
  EXPECT_EQ(loaded.anchorStart, 400u);
  EXPECT_EQ(loaded.anchorEnd, 415u);

  // A second identical save stops at the duplicate guard, so nothing is rewritten.
  ASSERT_TRUE(quotes::save(again));
  EXPECT_EQ(onlyQuoteFileName(), name);
}

// The detail view names the moment a quote was kept, so the minute of the day travels
// next to the day code. It is one more optional key inside schema 1.
TEST_F(QuoteAnchorStoreTest, SavedTimeSurvivesSaveAndLoad) {
  QuoteRecord saved = sampleQuote();
  saved.minute = 20 * 60 + 41;
  ASSERT_TRUE(quotes::save(saved));

  const std::string name = onlyQuoteFileName();
  ASSERT_FALSE(name.empty());
  QuoteRecord loaded;
  ASSERT_TRUE(quotes::load(name, loaded));
  EXPECT_EQ(loaded.minute, 20u * 60 + 41);
  EXPECT_EQ(loaded.day, saved.day);
}

// A record written before the time was stamped, and a record kept while the clock could
// not be read, both come back with no minute so the detail view shows the date alone.
TEST_F(QuoteAnchorStoreTest, RecordWithoutTimeLoadsWithNoMinute) {
  std::filesystem::create_directories(quoteDirectory());
  const std::string name = "00112233445566fe.json";
  {
    std::ofstream output(quoteDirectory() + "/" + name);
    output << R"({"schema":1,"path":"/audit.epub","title":"Synonym Lookup Test",)"
           << R"("text":"position.","spine":0,"page":0,"day":20260921})";
  }
  QuoteRecord loaded;
  ASSERT_TRUE(quotes::load(name, loaded));
  EXPECT_EQ(loaded.minute, quotes::NO_MINUTE);

  // An unreadable clock leaves the record without the key too, so an older build reading
  // the same card sees exactly the file it used to write.
  std::filesystem::remove(quoteDirectory() + "/" + name);
  QuoteRecord clockless = sampleQuote();
  clockless.text = "kept while the clock was unreadable";
  ASSERT_TRUE(quotes::save(clockless));
  const std::string written = fileText(onlyQuoteFileName());
  EXPECT_EQ(written.find("\"gio\""), std::string::npos) << written;
}

TEST_F(QuoteAnchorStoreTest, LoadAnchorsOnEmptyStoreGivesNothing) {
  std::vector<QuoteAnchor> anchors;
  anchors.push_back(QuoteAnchor{1, 2, 3});
  quotes::loadAnchors("/sach/audit.epub", anchors);
  EXPECT_TRUE(anchors.empty());
}

}  // namespace
