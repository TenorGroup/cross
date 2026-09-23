// The quote kept last is remembered on the card, so Home can show it after deep sleep: the
// device wakes straight into the book, the reader saves a quote, and Home has never shown the
// book since power-on. The marker is a dot file beside the quotes, so no listing counts it.
#include <HalStorage.h>
#include <QuoteStore.h>
#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {

std::string quoteDirectory() { return fixtureRoot + "/.crosspoint/quotes"; }

std::set<std::string> filesOnCard() {
  std::set<std::string> found;
  if (!std::filesystem::exists(quoteDirectory())) return found;
  for (const auto& entry : std::filesystem::directory_iterator(quoteDirectory()))
    found.insert(entry.path().filename().string());
  return found;
}

QuoteRecord quoteOf(const std::string& path, const std::string& text, uint16_t minute, bool anchored = false) {
  QuoteRecord quote;
  quote.path = path;
  quote.title = "Book";
  quote.text = text;
  quote.spine = 2;
  quote.page = 5;
  quote.day = 20260923;
  quote.minute = minute;
  quote.hasAnchor = anchored;
  quote.anchorStart = anchored ? 10 : 0;
  quote.anchorEnd = anchored ? 20 : 0;
  return quote;
}

// Id of the one quote file of `path` whose record holds `text`.
quotes::QuoteId idOfText(const std::string& path, const std::string& text) {
  std::vector<quotes::QuoteId> ids;
  quotes::listNames(quotes::bookKey(path), ids);
  for (const auto id : ids) {
    QuoteRecord record;
    if (quotes::load(id, record) && record.text == text) return id;
  }
  return 0;
}

class QuoteLatestTest : public ::testing::Test {
 protected:
  std::filesystem::path root;
  void SetUp() override {
    root = std::filesystem::temp_directory_path() /
           ("cross-quote-latest-" + std::to_string(getpid()) + "-" +
            ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    fixtureRoot = root.string();
    io = {};
    storageFault = {};
  }
  void TearDown() override {
    std::filesystem::remove_all(root);
    fixtureRoot.clear();
    storageFault = {};
  }
};

TEST_F(QuoteLatestTest, NothingSavedMeansNoLatest) {
  quotes::QuoteId id = 123;
  EXPECT_FALSE(quotes::latestSaved(id));
  EXPECT_EQ(id, 123u);
}

TEST_F(QuoteLatestTest, SaveRecordsTheIdOfTheQuoteItWrote) {
  ASSERT_TRUE(quotes::save(quoteOf("/a.epub", "first words", 600)));
  ASSERT_TRUE(quotes::save(quoteOf("/b.epub", "second words", 601)));
  quotes::QuoteId id = 0;
  ASSERT_TRUE(quotes::latestSaved(id));
  EXPECT_EQ(id, idOfText("/b.epub", "second words"));
  EXPECT_TRUE(filesOnCard().count(".latest"));
  // The staged copy is gone once the marker is in place.
  EXPECT_FALSE(filesOnCard().count(".latest.tmp"));
}

// Saving words already kept writes no second record, but the reader did keep them just now, so
// they are what Home shows next.
TEST_F(QuoteLatestTest, SavingAQuoteAgainPointsAtTheRecordAlreadyThere) {
  ASSERT_TRUE(quotes::save(quoteOf("/a.epub", "kept twice", 600)));
  ASSERT_TRUE(quotes::save(quoteOf("/a.epub", "other words", 601)));
  ASSERT_TRUE(quotes::save(quoteOf("/a.epub", "kept twice", 700)));
  quotes::QuoteId id = 0;
  ASSERT_TRUE(quotes::latestSaved(id));
  EXPECT_EQ(id, idOfText("/a.epub", "kept twice"));
}

TEST_F(QuoteLatestTest, AnEditIsTheLatestToo) {
  ASSERT_TRUE(quotes::save(quoteOf("/a.epub", "to be edited", 600)));
  ASSERT_TRUE(quotes::save(quoteOf("/a.epub", "kept after", 601)));
  const auto edited = idOfText("/a.epub", "to be edited");
  ASSERT_NE(edited, 0u);
  ASSERT_TRUE(quotes::replace(edited, quoteOf("/a.epub", "edited", 600)));
  quotes::QuoteId id = 0;
  ASSERT_TRUE(quotes::latestSaved(id));
  EXPECT_EQ(id, edited);
}

TEST_F(QuoteLatestTest, ForgetClearsTheMarker) {
  ASSERT_TRUE(quotes::save(quoteOf("/a.epub", "shown once", 600)));
  quotes::forgetLatestSaved();
  quotes::QuoteId id = 0;
  EXPECT_FALSE(quotes::latestSaved(id));
  EXPECT_FALSE(filesOnCard().count(".latest"));
  // Forgetting twice is harmless.
  quotes::forgetLatestSaved();
  EXPECT_FALSE(quotes::latestSaved(id));
}

// A marker that does not hold a quote id (a cut write, a stray file) reads as none.
TEST_F(QuoteLatestTest, AGarbledMarkerReadsAsNone) {
  std::filesystem::create_directories(quoteDirectory());
  for (const std::string text : {"", "12345", "zzzzzzzzzzzzzzzz", "E40C292C09C54D90"}) {
    std::ofstream(quoteDirectory() + "/.latest") << text;
    quotes::QuoteId id = 7;
    EXPECT_FALSE(quotes::latestSaved(id)) << text;
    EXPECT_EQ(id, 7u);
  }
}

TEST_F(QuoteLatestTest, ListingsAndMigrationIgnoreTheMarker) {
  ASSERT_TRUE(quotes::save(quoteOf("/a.epub", "anchored words", 600, true)));
  ASSERT_TRUE(quotes::save(quoteOf("/b.epub", "other book", 601)));
  ASSERT_TRUE(filesOnCard().count(".latest"));
  // A staged marker left by a cut write sits beside it too.
  std::ofstream(quoteDirectory() + "/.latest.tmp") << "e40c292c09c54d90";
  std::vector<quotes::QuoteId> ids;
  quotes::listNames(0, ids);
  EXPECT_EQ(ids.size(), 2u);
  std::vector<quotes::BookSummary> books;
  quotes::listBooks(books);
  ASSERT_EQ(books.size(), 2u);
  EXPECT_EQ(books[0].count + books[1].count, 2u);
  std::vector<QuoteAnchor> anchors;
  quotes::loadAnchors("/a.epub", anchors);
  EXPECT_EQ(anchors.size(), 1u);
  std::filesystem::remove(quoteDirectory() + "/.ten-v2");
  const auto before = filesOnCard();
  EXPECT_TRUE(quotes::migrateNames());
  auto after = filesOnCard();
  after.erase(".ten-v2");
  EXPECT_EQ(after, before);
  quotes::QuoteId id = 0;
  ASSERT_TRUE(quotes::latestSaved(id));
  EXPECT_EQ(id, idOfText("/b.epub", "other book"));
}

// Deep sleep ends in a fresh boot: nothing of the saving process is left but the card. A child
// process saves and exits; this process, which never saved, reads the marker back.
TEST_F(QuoteLatestTest, TheMarkerOutlivesTheProcessThatWroteIt) {
  const pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) _exit(quotes::save(quoteOf("/a.epub", "saved before sleep", 600)) ? 0 : 1);
  int status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  quotes::QuoteId id = 0;
  ASSERT_TRUE(quotes::latestSaved(id));
  EXPECT_EQ(id, idOfText("/a.epub", "saved before sleep"));
}

}  // namespace
