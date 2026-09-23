// File names that carry the book and the moment a quote was kept, so the Quotes screens
// can group, count and order the store from a directory listing without opening files,
// and the reader opens only the files of the book it is showing.
#include <HalStorage.h>
#include <QuoteStore.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
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
  // The mark of the quote kept last is rewritten by every save and edit; QuoteLatestTest owns
  // it, and these tests are about the records and their staged copies.
  for (const auto& entry : std::filesystem::directory_iterator(quoteDirectory()))
    if (entry.path().filename() != ".latest") found.insert(entry.path().filename().string());
  return found;
}

std::set<std::string> quoteFiles() {
  std::set<std::string> found;
  for (const auto& name : filesOnCard())
    if (name.size() == 21 && name.substr(16) == ".json") found.insert(name);
  return found;
}

void writeFile(const std::string& name, const std::string& text) {
  std::filesystem::create_directories(quoteDirectory());
  std::ofstream output(quoteDirectory() + "/" + name);
  output << text;
}

std::string recordJson(const std::string& path, const std::string& text, int spine, int page, uint32_t day,
                       int minute = -1, bool anchored = false, uint32_t start = 0, uint32_t end = 0) {
  std::string json = R"({"schema":1,"path":")" + path + R"(","title":"Book","text":")" + text +
                     R"(","spine":)" + std::to_string(spine) + R"(,"page":)" + std::to_string(page) +
                     R"(,"day":)" + std::to_string(day);
  if (minute >= 0) json += R"(,"gio":)" + std::to_string(minute);
  if (anchored) json += R"(,"vo":)" + std::to_string(start) + R"(,"ve":)" + std::to_string(end);
  return json + "}";
}

std::string fileName(uint32_t book, uint32_t dayCode, uint32_t minute, uint32_t slot) {
  char name[32];
  snprintf(name, sizeof(name), "%08x%04x%03x%x.json", book, dayCode, minute, slot);
  return name;
}

quotes::QuoteId idFor(const std::string& name) {
  quotes::QuoteId id = 0;
  EXPECT_TRUE(quotes::idOf(name, id)) << name;
  return id;
}

std::vector<std::string> namesOf(const std::vector<quotes::QuoteId>& ids) {
  std::vector<std::string> names;
  for (const auto id : ids) names.push_back(quotes::nameOf(id));
  return names;
}

// The v1.0.10 name: FNV-1a 64 over path, a zero byte, text, a zero byte, spine, page.
std::string legacyName(const QuoteRecord& q) {
  uint64_t hash = 14695981039346656037ULL;
  for (const auto* part : {&q.path, &q.text}) {
    for (const unsigned char c : *part) {
      hash ^= c;
      hash *= 1099511628211ULL;
    }
    hash ^= 0;
    hash *= 1099511628211ULL;
  }
  for (int value : {q.spine, q.page}) {
    hash ^= static_cast<uint32_t>(value);
    hash *= 1099511628211ULL;
  }
  char name[24];
  snprintf(name, sizeof(name), "%016llx.json", static_cast<unsigned long long>(hash));
  return name;
}

QuoteRecord quoteOf(const std::string& path, const std::string& text, uint32_t day, uint16_t minute) {
  QuoteRecord quote;
  quote.path = path;
  quote.title = "Book";
  quote.text = text;
  quote.spine = 2;
  quote.page = 5;
  quote.day = day;
  quote.minute = minute;
  return quote;
}

// Published FNV-1a 32 test vectors, so the key is checked against the algorithm itself
// and not against a second copy of this code.
constexpr uint32_t KEY_A = 0xe40c292c;       // "a"
constexpr uint32_t KEY_FOOBAR = 0xbf9cf968;  // "foobar"
// 2026-09-22: (2026 - 2020) * 372 + 8 * 31 + 21
constexpr uint32_t CODE_20260922 = 2501;
constexpr uint16_t MINUTE_2041 = 20 * 60 + 41;

class QuoteNameStoreTest : public ::testing::Test {
 protected:
  std::filesystem::path root;
  void SetUp() override {
    root = std::filesystem::temp_directory_path() /
           ("cross-quote-name-" + std::to_string(getpid()) + "-" +
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

TEST_F(QuoteNameStoreTest, BookKeyIsFnv1a32OfThePath) {
  EXPECT_EQ(quotes::bookKey(""), 0x811c9dc5u);
  EXPECT_EQ(quotes::bookKey("a"), KEY_A);
  EXPECT_EQ(quotes::bookKey("foobar"), KEY_FOOBAR);
}

TEST_F(QuoteNameStoreTest, NameCarriesBookDayMinuteAndSlot) {
  ASSERT_TRUE(quotes::save(quoteOf("a", "first words", 20260922, MINUTE_2041)));
  const std::string expected = "e40c292c09c54d90.json";
  EXPECT_EQ(fileName(KEY_A, CODE_20260922, MINUTE_2041, 0), expected);
  EXPECT_EQ(quoteFiles(), std::set<std::string>{expected});
  EXPECT_EQ(quotes::bookKeyOfName(expected), KEY_A);
  EXPECT_EQ(quotes::momentOfName(expected), (CODE_20260922 << 12) | MINUTE_2041);
  QuoteRecord loaded;
  ASSERT_TRUE(quotes::load(expected, loaded));
  EXPECT_EQ(loaded.text, "first words");
}

// A quote kept while the clock could not be read, and a day before the scheme's epoch,
// both land on moment zero: they sort as the oldest, never as garbage.
TEST_F(QuoteNameStoreTest, UnknownDayAndMinuteWriteZeros) {
  ASSERT_TRUE(quotes::save(quoteOf("a", "no clock", 0, quotes::NO_MINUTE)));
  ASSERT_TRUE(quotes::save(quoteOf("a", "last century", 20191231, quotes::NO_MINUTE)));
  ASSERT_TRUE(quotes::save(quoteOf("foobar", "date only", 20200101, quotes::NO_MINUTE)));
  EXPECT_EQ(quoteFiles(), (std::set<std::string>{"e40c292c00000000.json", "e40c292c00000001.json",
                                                 "bf9cf96800000000.json"}));
}

TEST_F(QuoteNameStoreTest, ValidNameIsSixteenLowercaseHexAndJson) {
  EXPECT_TRUE(quotes::validName("e40c292c09c54d90.json"));
  EXPECT_FALSE(quotes::validName("E40C292C09C54D90.json"));
  EXPECT_FALSE(quotes::validName("e40c292c09c54d9.json"));
  EXPECT_FALSE(quotes::validName("e40c292c09c54d90.json.tmp"));
  EXPECT_FALSE(quotes::validName("e40c292c09c54d9g.json"));
  EXPECT_FALSE(quotes::validName(".ten-v2"));
}

TEST_F(QuoteNameStoreTest, TwoQuotesInTheSameMinuteTakeTheNextSlot) {
  ASSERT_TRUE(quotes::save(quoteOf("a", "one", 20260922, MINUTE_2041)));
  ASSERT_TRUE(quotes::save(quoteOf("a", "two", 20260922, MINUTE_2041)));
  const std::string first = fileName(KEY_A, CODE_20260922, MINUTE_2041, 0);
  const std::string second = fileName(KEY_A, CODE_20260922, MINUTE_2041, 1);
  EXPECT_EQ(quoteFiles(), (std::set<std::string>{first, second}));
  QuoteRecord loaded;
  ASSERT_TRUE(quotes::load(second, loaded));
  EXPECT_EQ(loaded.text, "two");
  std::vector<quotes::QuoteId> ids;
  quotes::listNames(0, ids);
  EXPECT_EQ(namesOf(ids), (std::vector<std::string>{second, first}));
}

// Sixteen slots fill up in a burst of saves; the seventeenth quote moves its name to the
// next minute and keeps its own minute inside the record, so it shows the exact time.
TEST_F(QuoteNameStoreTest, SeventeenthQuoteInOneMinuteMovesToTheNextMinute) {
  for (int i = 0; i < 16; i++) ASSERT_TRUE(quotes::save(quoteOf("a", "q" + std::to_string(i), 20260922, 1)));
  ASSERT_TRUE(quotes::save(quoteOf("a", "one too many", 20260922, 1)));
  const std::string moved = fileName(KEY_A, CODE_20260922, 2, 0);
  ASSERT_EQ(quoteFiles().size(), 17u);
  ASSERT_TRUE(quoteFiles().count(moved));
  QuoteRecord loaded;
  ASSERT_TRUE(quotes::load(moved, loaded));
  EXPECT_EQ(loaded.text, "one too many");
  EXPECT_EQ(loaded.minute, 1u);
  // A quote really kept in minute 2 takes the next free slot there.
  ASSERT_TRUE(quotes::save(quoteOf("a", "kept at two", 20260922, 2)));
  EXPECT_TRUE(quoteFiles().count(fileName(KEY_A, CODE_20260922, 2, 1)));
}

// The last minute of the day has nowhere to move to.
TEST_F(QuoteNameStoreTest, FullLastMinuteOfTheDayIsRefused) {
  for (uint32_t slot = 0; slot < 16; slot++)
    writeFile(fileName(KEY_A, CODE_20260922, 1439, slot),
              recordJson("a", "late " + std::to_string(slot), 2, 5, 20260922, 1439));
  EXPECT_FALSE(quotes::save(quoteOf("a", "one too many", 20260922, 1439)));
  EXPECT_EQ(quoteFiles().size(), 16u);
}

TEST_F(QuoteNameStoreTest, ListNamesIsNewestFirstAcrossAndWithinBooks) {
  ASSERT_TRUE(quotes::save(quoteOf("a", "a old", 20260101, 600)));
  ASSERT_TRUE(quotes::save(quoteOf("foobar", "f mid", 20260501, 10)));
  ASSERT_TRUE(quotes::save(quoteOf("a", "a new", 20260922, 5)));
  ASSERT_TRUE(quotes::save(quoteOf("foobar", "f new", 20260922, 7)));
  ASSERT_TRUE(quotes::save(quoteOf("a", "a unknown", 0, quotes::NO_MINUTE)));

  const auto text = [](const quotes::QuoteId id) {
    QuoteRecord quote;
    EXPECT_TRUE(quotes::load(id, quote)) << quotes::nameOf(id);
    return quote.text;
  };
  std::vector<quotes::QuoteId> names;
  quotes::listNames(0, names);
  std::vector<std::string> texts;
  for (const auto id : names) texts.push_back(text(id));
  EXPECT_EQ(texts, (std::vector<std::string>{"f new", "a new", "f mid", "a old", "a unknown"}));

  quotes::listNames(KEY_A, names);
  texts.clear();
  for (const auto id : names) texts.push_back(text(id));
  EXPECT_EQ(texts, (std::vector<std::string>{"a new", "a old", "a unknown"}));

  quotes::listNames(0xdeadbeef, names);
  EXPECT_TRUE(names.empty());
}

// Sorting never opens a record: the moment is in the name.
TEST_F(QuoteNameStoreTest, ListNamesAndListBooksOpenNoFile) {
  writeFile(fileName(KEY_A, 100, 5, 0), "not json at all");
  writeFile(fileName(KEY_FOOBAR, 200, 5, 0), "not json at all");
  writeFile("notes.txt", "ignored");
  io = {};
  std::vector<quotes::QuoteId> names;
  quotes::listNames(0, names);
  EXPECT_EQ(names.size(), 2u);
  std::vector<quotes::BookSummary> books;
  quotes::listBooks(books);
  EXPECT_EQ(books.size(), 2u);
  EXPECT_EQ(io.readDocs, 0u);
  EXPECT_EQ(io.fullPathFileOpens, 0u);
  EXPECT_EQ(io.readFiles, 0u);
}

TEST_F(QuoteNameStoreTest, ListNamesKeepsTheNewestAtTheCap) {
  const size_t total = quotes::MAX_QUOTES + 20;
  for (size_t i = 0; i < total; i++)
    writeFile(fileName(KEY_A, static_cast<uint32_t>(1 + i / 16), 0, static_cast<uint32_t>(i % 16)), "{}");
  std::vector<quotes::QuoteId> ids;
  quotes::listNames(0, ids);
  ASSERT_EQ(ids.size(), quotes::MAX_QUOTES);
  EXPECT_EQ(quotes::nameOf(ids.front()),
            fileName(KEY_A, static_cast<uint32_t>(1 + (total - 1) / 16), 0, (total - 1) % 16));
  EXPECT_EQ(quotes::nameOf(ids.back()), fileName(KEY_A, static_cast<uint32_t>(1 + 20 / 16), 0, 20 % 16));
}

TEST_F(QuoteNameStoreTest, ListBooksCountsAndOrdersByNewestQuote) {
  ASSERT_TRUE(quotes::save(quoteOf("a", "a1", 20260101, 1)));
  ASSERT_TRUE(quotes::save(quoteOf("a", "a2", 20260102, 1)));
  ASSERT_TRUE(quotes::save(quoteOf("a", "a3", 20260103, 1)));
  ASSERT_TRUE(quotes::save(quoteOf("foobar", "f1", 20260920, 1)));
  ASSERT_TRUE(quotes::save(quoteOf("foobar", "f2", 20260110, 1)));

  std::vector<quotes::BookSummary> books;
  quotes::listBooks(books);
  ASSERT_EQ(books.size(), 2u);
  EXPECT_EQ(books[0].book, KEY_FOOBAR);
  EXPECT_EQ(books[0].count, 2u);
  QuoteRecord newest;
  ASSERT_TRUE(quotes::load(books[0].newest, newest));
  EXPECT_EQ(newest.text, "f1");
  EXPECT_EQ(books[1].book, KEY_A);
  EXPECT_EQ(books[1].count, 3u);
  ASSERT_TRUE(quotes::load(books[1].newest, newest));
  EXPECT_EQ(newest.text, "a3");
}

// Keeping the same words again, even minutes later, must not add a second file.
TEST_F(QuoteNameStoreTest, SameQuoteAtAnotherMomentIsNotWrittenTwice) {
  ASSERT_TRUE(quotes::save(quoteOf("a", "same words", 20260922, 100)));
  const auto before = quoteFiles();
  ASSERT_TRUE(quotes::save(quoteOf("a", "same words", 20260923, 200)));
  EXPECT_EQ(quoteFiles(), before);
}

// The anchor upgrade rewrites the old record under its own name, so the quote keeps its
// place in the list and the moment it was first kept.
TEST_F(QuoteNameStoreTest, AnchorUpgradeKeepsTheName) {
  ASSERT_TRUE(quotes::save(quoteOf("a", "same words", 20260922, 100)));
  const auto before = quoteFiles();
  ASSERT_EQ(before.size(), 1u);
  QuoteRecord anchored = quoteOf("a", "same words", 20260923, 200);
  anchored.hasAnchor = true;
  anchored.anchorStart = 40;
  anchored.anchorEnd = 50;
  ASSERT_TRUE(quotes::save(anchored));
  EXPECT_EQ(filesOnCard(), before);
  QuoteRecord loaded;
  ASSERT_TRUE(quotes::load(*before.begin(), loaded));
  EXPECT_TRUE(loaded.hasAnchor);
  EXPECT_EQ(loaded.anchorStart, 40u);
  EXPECT_EQ(loaded.anchorEnd, 50u);
  EXPECT_EQ(loaded.day, 20260922u);
  EXPECT_EQ(loaded.minute, 100u);
}

TEST_F(QuoteNameStoreTest, ReplaceKeepsTheNameAndChangesTheContent) {
  ASSERT_TRUE(quotes::save(quoteOf("a", "chosen too many words", 20260922, 100)));
  const std::string name = *quoteFiles().begin();
  QuoteRecord edited;
  ASSERT_TRUE(quotes::load(name, edited));
  edited.text = "too many";
  edited.hasAnchor = true;
  edited.anchorStart = 7;
  edited.anchorEnd = 15;
  ASSERT_TRUE(quotes::replace(name, edited));
  EXPECT_EQ(filesOnCard(), std::set<std::string>{name}) << "no staged file may be left behind";
  QuoteRecord loaded;
  ASSERT_TRUE(quotes::load(name, loaded));
  EXPECT_EQ(loaded.text, "too many");
  EXPECT_TRUE(loaded.hasAnchor);
  EXPECT_EQ(loaded.anchorStart, 7u);
  EXPECT_EQ(loaded.anchorEnd, 15u);
  EXPECT_EQ(loaded.minute, 100u);
}

TEST_F(QuoteNameStoreTest, ReplaceOfAMissingNameFails) {
  EXPECT_FALSE(quotes::replace(fileName(KEY_A, 1, 1, 0), quoteOf("a", "text", 20260922, 1)));
  EXPECT_TRUE(quoteFiles().empty());
}

TEST_F(QuoteNameStoreTest, RemoveDeletesOnlyThatQuote) {
  ASSERT_TRUE(quotes::save(quoteOf("a", "keep", 20260922, 1)));
  ASSERT_TRUE(quotes::save(quoteOf("a", "drop", 20260922, 2)));
  const std::string drop = fileName(KEY_A, CODE_20260922, 2, 0);
  ASSERT_TRUE(quotes::remove(drop));
  EXPECT_EQ(quoteFiles(), std::set<std::string>{fileName(KEY_A, CODE_20260922, 1, 0)});
  EXPECT_FALSE(quotes::remove(drop));
  EXPECT_FALSE(quotes::remove("../escape.json"));
}

// Cards written by v1.0.10 hold hash names. One pass moves each record to the name its
// own fields give it; content is untouched and a record that cannot be read stays put.
TEST_F(QuoteNameStoreTest, MigrateRenamesLegacyNamesOnceAndWritesTheMarker) {
  const QuoteRecord a1 = quoteOf("a", "legacy one", 20260922, MINUTE_2041);
  const QuoteRecord a2 = quoteOf("a", "legacy two", 20260922, MINUTE_2041);
  const QuoteRecord f1 = quoteOf("foobar", "legacy undated", 20260101, quotes::NO_MINUTE);
  for (const auto* q : {&a1, &a2, &f1})
    writeFile(legacyName(*q), recordJson(q->path, q->text, q->spine, q->page, q->day,
                                         q->minute == quotes::NO_MINUTE ? -1 : q->minute));
  const std::string broken = "0123456789abcdef.json";
  writeFile(broken, "{ this is not a record");

  io = {};
  ASSERT_TRUE(quotes::migrateNames());
  EXPECT_EQ(io.mutationsWithOpenHandle, 0u);
  const std::string a1Name = fileName(KEY_A, CODE_20260922, MINUTE_2041, 0);
  const std::string a2Name = fileName(KEY_A, CODE_20260922, MINUTE_2041, 1);
  const std::string f1Name = fileName(KEY_FOOBAR, (2026 - 2020) * 372, 0, 0);
  EXPECT_EQ(quoteFiles(), (std::set<std::string>{a1Name, a2Name, f1Name, broken}));
  EXPECT_TRUE(filesOnCard().count(".ten-v2"));

  std::set<std::string> texts;
  for (const auto& name : {a1Name, a2Name, f1Name}) {
    QuoteRecord loaded;
    ASSERT_TRUE(quotes::load(name, loaded)) << name;
    texts.insert(loaded.text);
  }
  EXPECT_EQ(texts, (std::set<std::string>{"legacy one", "legacy two", "legacy undated"}));

  // Once marked, the pass costs one existence check: nothing is read or renamed.
  const size_t renames = io.renames;
  const size_t reads = io.readDocs;
  ASSERT_TRUE(quotes::migrateNames());
  EXPECT_EQ(io.renames, renames);
  EXPECT_EQ(io.readDocs, reads);

  // Even without the marker a second pass finds every name already right.
  std::filesystem::remove(quoteDirectory() + "/.ten-v2");
  const auto settled = quoteFiles();
  ASSERT_TRUE(quotes::migrateNames());
  EXPECT_EQ(io.renames, renames);
  EXPECT_EQ(quoteFiles(), settled);
  EXPECT_TRUE(filesOnCard().count(".ten-v2"));
}

// A rename that fails leaves no marker, so the next visit to the Quotes screen tries again.
TEST_F(QuoteNameStoreTest, FailedRenameLeavesNoMarkerAndIsRetried) {
  const QuoteRecord a1 = quoteOf("a", "legacy one", 20260922, MINUTE_2041);
  const std::string legacy = legacyName(a1);
  writeFile(legacy, recordJson(a1.path, a1.text, a1.spine, a1.page, a1.day, a1.minute));
  const std::string target = fileName(KEY_A, CODE_20260922, MINUTE_2041, 0);
  storageFault.operation = StorageFaultOperation::Rename;
  storageFault.path = "/.crosspoint/quotes/" + legacy;
  storageFault.target = "/.crosspoint/quotes/" + target;
  EXPECT_FALSE(quotes::migrateNames());
  EXPECT_EQ(storageFault.hits, 1u);
  EXPECT_FALSE(filesOnCard().count(".ten-v2"));
  EXPECT_EQ(quoteFiles(), std::set<std::string>{legacy});

  storageFault = {};
  EXPECT_TRUE(quotes::migrateNames());
  EXPECT_EQ(quoteFiles(), std::set<std::string>{target});
  EXPECT_TRUE(filesOnCard().count(".ten-v2"));
}

// The reader opens only the files of the book it shows, so a corrupt or huge record of
// another book costs nothing, and the number of other books' quotes no longer matters.
TEST_F(QuoteNameStoreTest, LoadAnchorsOpensOnlyThisBooksFiles) {
  const std::string mine = "a";
  for (uint32_t i = 0; i < 10; i++)
    writeFile(fileName(KEY_A, 1000 + i, 0, 0), recordJson(mine, "mine " + std::to_string(i), static_cast<int>(i), 0,
                                                        20260922, -1, true, 10 * i, 10 * i + 5));
  // Six hundred records of another book, none of them valid: opening even one of them
  // would show up in the read count below.
  for (uint32_t i = 0; i < 600; i++) writeFile(fileName(KEY_FOOBAR, 1 + i / 16, 0, i % 16), "{ corrupt");

  io = {};
  std::vector<QuoteAnchor> anchors;
  quotes::loadAnchors(mine, anchors);
  EXPECT_EQ(anchors.size(), 10u);
  EXPECT_EQ(io.readDocs, 10u);
  EXPECT_EQ(io.mutationsWithOpenHandle, 0u);
}

// A book with more quotes than the reader holds anchors for fills the cap and stops,
// instead of stopping at a scan limit counted over the whole store.
TEST_F(QuoteNameStoreTest, LoadAnchorsFillsThePerBookCap) {
  for (uint32_t i = 0; i < 300; i++)
    writeFile(fileName(KEY_A, 1 + i / 16, 0, i % 16),
              recordJson("a", "quote " + std::to_string(i), 1, 0, 20260922, -1, true, 10 * i, 10 * i + 5));
  // Other books fill most of the directory, so a scan limit counted over the whole store
  // would run out long before this book's cap.
  for (uint32_t i = 0; i < 600; i++) writeFile(fileName(KEY_FOOBAR, 1 + i / 16, 0, i % 16), "{ corrupt");
  io = {};
  std::vector<QuoteAnchor> anchors;
  quotes::loadAnchors("a", anchors);
  EXPECT_EQ(anchors.size(), quotes::MAX_BOOK_ANCHORS);
  EXPECT_LE(io.readDocs, quotes::MAX_BOOK_ANCHORS);
}

// Two paths can share a key; the record's own path still decides.
TEST_F(QuoteNameStoreTest, LoadAnchorsChecksThePathInsideTheRecord) {
  writeFile(fileName(KEY_A, 1, 0, 0), recordJson("/other/book.epub", "collision", 1, 0, 20260922, -1, true, 1, 5));
  std::vector<QuoteAnchor> anchors;
  quotes::loadAnchors("a", anchors);
  EXPECT_TRUE(anchors.empty());
}

// The screens hold ids, not names: one fixed-width number per quote and no allocation
// behind it, so the list at its cap is 4 KiB.
static_assert(sizeof(quotes::QuoteId) == 8, "a quote id is the 16 hex digits of its name as one number");

TEST_F(QuoteNameStoreTest, IdIsTheNameReadAsOneNumber) {
  const std::string name = "e40c292c09c54d93.json";
  quotes::QuoteId id = 0;
  ASSERT_TRUE(quotes::idOf(name, id));
  EXPECT_EQ(id, 0xe40c292c09c54d93ull);
  EXPECT_EQ(quotes::nameOf(id), name);
  EXPECT_EQ(quotes::nameOf(0x5ull), "0000000000000005.json");
  EXPECT_EQ(quotes::bookKeyOfName(id), KEY_A);
  EXPECT_EQ(quotes::momentOfName(id), (CODE_20260922 << 12) | MINUTE_2041);
  EXPECT_EQ(quotes::momentOfName(id), quotes::momentOfName(name));
  quotes::QuoteId untouched = 42;
  EXPECT_FALSE(quotes::idOf("E40C292C09C54D93.json", untouched));
  EXPECT_FALSE(quotes::idOf("e40c292c09c54d93.json.tmp", untouched));
  EXPECT_EQ(untouched, 42u);
}

TEST_F(QuoteNameStoreTest, LoadRemoveReplaceById) {
  ASSERT_TRUE(quotes::save(quoteOf("a", "by id", 20260922, MINUTE_2041)));
  const quotes::QuoteId id = idFor(fileName(KEY_A, CODE_20260922, MINUTE_2041, 0));
  QuoteRecord loaded;
  ASSERT_TRUE(quotes::load(id, loaded));
  EXPECT_EQ(loaded.text, "by id");
  loaded.text = "by";
  ASSERT_TRUE(quotes::replace(id, loaded));
  QuoteRecord again;
  ASSERT_TRUE(quotes::load(id, again));
  EXPECT_EQ(again.text, "by");
  ASSERT_TRUE(quotes::remove(id));
  EXPECT_FALSE(quotes::load(id, again));
  EXPECT_FALSE(quotes::replace(id, loaded));
  EXPECT_TRUE(quoteFiles().empty());
}

// A full list: 512 quotes spread over two books and many moments come back newest first,
// every id naming a file that is there.
TEST_F(QuoteNameStoreTest, ListNamesOrdersAFullStore) {
  std::vector<std::string> written;
  for (uint32_t i = 0; i < quotes::MAX_QUOTES; i++) {
    const uint32_t book = i % 3 == 0 ? KEY_FOOBAR : KEY_A;
    const std::string name = fileName(book, 100 + i / 64, (i * 7) % 1440, i % 16);
    writeFile(name, "{}");
    written.push_back(name);
  }
  std::vector<quotes::QuoteId> ids;
  quotes::listNames(0, ids);
  ASSERT_EQ(ids.size(), quotes::MAX_QUOTES);
  for (size_t i = 1; i < ids.size(); i++) {
    const uint32_t newer = quotes::momentOfName(ids[i - 1]), older = quotes::momentOfName(ids[i]);
    ASSERT_GE(newer, older) << i;
    if (newer == older) ASSERT_GE(ids[i - 1] & 0xf, ids[i] & 0xf) << i;
  }
  std::vector<std::string> listed = namesOf(ids);
  std::sort(listed.begin(), listed.end());
  std::sort(written.begin(), written.end());
  EXPECT_EQ(listed, written);
}

// A power cut between the two steps of replace() leaves the complete new record as
// "<name>.tmp" with no "<name>". The next visit promotes it, marker or not.
TEST_F(QuoteNameStoreTest, CutAfterRemoveIsRepairedByPromotingTheStagedRecord) {
  ASSERT_TRUE(quotes::migrateNames());
  ASSERT_TRUE(quotes::save(quoteOf("a", "chosen too many words", 20260922, 100)));
  const std::string name = fileName(KEY_A, CODE_20260922, 100, 0);
  QuoteRecord edited;
  ASSERT_TRUE(quotes::load(name, edited));
  edited.text = "too many";
  // The rename is where the power goes: the old file is already gone.
  storageFault.operation = StorageFaultOperation::Rename;
  storageFault.path = "/.crosspoint/quotes/" + name + ".tmp";
  storageFault.target = "/.crosspoint/quotes/" + name;
  EXPECT_FALSE(quotes::replace(name, edited));
  storageFault = {};
  ASSERT_EQ(quoteFiles(), std::set<std::string>{});
  ASSERT_TRUE(filesOnCard().count(name + ".tmp"));

  ASSERT_TRUE(quotes::migrateNames());
  EXPECT_EQ(quoteFiles(), std::set<std::string>{name});
  EXPECT_FALSE(filesOnCard().count(name + ".tmp"));
  QuoteRecord loaded;
  ASSERT_TRUE(quotes::load(name, loaded));
  EXPECT_EQ(loaded.text, "too many");
}

// A cut while the staged file was still being written leaves it beside the old record,
// which is intact. The half-written file is dropped and the old record stays.
TEST_F(QuoteNameStoreTest, CutWhileStagingDropsThePartialFileAndKeepsTheOldRecord) {
  ASSERT_TRUE(quotes::migrateNames());
  ASSERT_TRUE(quotes::save(quoteOf("a", "kept as it was", 20260922, 100)));
  const std::string name = fileName(KEY_A, CODE_20260922, 100, 0);
  writeFile(name + ".tmp", R"({"schema":1,"path":"a","title":"Book","text":"half wri)");
  ASSERT_TRUE(quotes::migrateNames());
  EXPECT_EQ(filesOnCard(), (std::set<std::string>{name, ".ten-v2"}));
  QuoteRecord loaded;
  ASSERT_TRUE(quotes::load(name, loaded));
  EXPECT_EQ(loaded.text, "kept as it was");
}

// save() stages a brand new quote the same way. A cut while that file is written leaves a
// partial "<name>.tmp" with nothing beside it; promoting it would list a quote that cannot
// be opened, so a staged file is promoted only when it reads back as a whole record.
TEST_F(QuoteNameStoreTest, PartialStagedFileWithNoRecordIsDropped) {
  ASSERT_TRUE(quotes::migrateNames());
  const std::string name = fileName(KEY_A, CODE_20260922, 100, 0);
  writeFile(name + ".tmp", R"({"schema":1,"path":"a","title":"Bo)");
  ASSERT_TRUE(quotes::migrateNames());
  EXPECT_EQ(filesOnCard(), std::set<std::string>{".ten-v2"});
}

// Only staged quote files are touched by the repair walk.
TEST_F(QuoteNameStoreTest, RepairLeavesOtherFilesAlone) {
  ASSERT_TRUE(quotes::migrateNames());
  writeFile("notes.txt.tmp", "someone else's file");
  writeFile("E40C292C09C54D90.json.tmp", "not ours either");
  io = {};
  ASSERT_TRUE(quotes::migrateNames());
  EXPECT_EQ(io.renames, 0u);
  EXPECT_EQ(io.removes, 0u);
  EXPECT_TRUE(filesOnCard().count("notes.txt.tmp"));
  EXPECT_TRUE(filesOnCard().count("E40C292C09C54D90.json.tmp"));
}

// Records kept before the minute was stamped all share minute 0 of their day. Twenty of one
// book on one day overflow the sixteen slots; migration moves the rest to minute 1, writes
// the marker, and a second pass accepts those names instead of moving them again.
TEST_F(QuoteNameStoreTest, MigrateMovesOverflowToTheNextMinute) {
  std::vector<std::string> legacy;
  for (int i = 0; i < 20; i++) {
    const QuoteRecord q = quoteOf("a", "undated " + std::to_string(i), 20260922, quotes::NO_MINUTE);
    legacy.push_back(legacyName(q));
    writeFile(legacy.back(), recordJson(q.path, q.text, q.spine, q.page, q.day));
  }
  ASSERT_TRUE(quotes::migrateNames());
  EXPECT_TRUE(filesOnCard().count(".ten-v2"));
  std::set<std::string> expected;
  for (uint32_t i = 0; i < 20; i++) expected.insert(fileName(KEY_A, CODE_20260922, i / 16, i % 16));
  EXPECT_EQ(quoteFiles(), expected);
  for (const auto& name : expected) {
    QuoteRecord loaded;
    ASSERT_TRUE(quotes::load(name, loaded)) << name;
    EXPECT_EQ(loaded.minute, quotes::NO_MINUTE) << name;
  }

  std::filesystem::remove(quoteDirectory() + "/.ten-v2");
  const size_t renames = io.renames;
  ASSERT_TRUE(quotes::migrateNames());
  EXPECT_EQ(io.renames, renames);
  EXPECT_EQ(quoteFiles(), expected);
}

}  // namespace
