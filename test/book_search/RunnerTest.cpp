#include <BookSearchRunner.h>
#include <Epub.h>
#include <HalMemory.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <cstdlib>
#include <unistd.h>

using Status = BookSearchRunner::Status;

#ifdef BOARD_HAS_PSRAM
namespace {
struct RunnerTest : ::testing::Test {
  std::filesystem::path cache;
  Epub epub;
  BookSearchRunner runner;
  void SetUp() override {
    ASSERT_EQ(HalMemory::live, 0u);
    HalMemory::calls = HalMemory::failAt = HalMemory::largest = 0;
    cache = std::filesystem::path(std::getenv("TMPDIR")) /
            (std::string("book-search-fixture-") + std::to_string(getpid()) + "-" +
             ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::filesystem::create_directories(cache / "html");
    epub.cache = cache.string();
    epub.chapters = {"<body>Người ở chương 1</body>", "<body>người ở chương 2</body>"};
  }
  void TearDown() override {
    runner.clear();
    EXPECT_EQ(HalMemory::live, 0u);
    std::filesystem::remove_all(cache);
  }
};

TEST_F(RunnerTest, CachedAndUncachedChapterUseSameOffsetsAndOneKbChunks) {
  std::ofstream(cache / "html/0.html") << epub.chapters[0];
  ASSERT_EQ(runner.begin("nguoi"), Status::Ready);
  ASSERT_EQ(runner.scanChapter(epub, 0), Status::Complete);
  EXPECT_EQ(epub.reads, 0u);
  ASSERT_EQ(runner.scanChapter(epub, 1), Status::Complete);
  EXPECT_EQ(epub.reads, 1u);
  EXPECT_EQ(epub.chunk, 1024u);
  ASSERT_NE(runner.results(), nullptr);
  ASSERT_EQ(runner.results()->count, 2u);
  EXPECT_EQ(runner.results()->items[0].visibleOffset, 0u);
  EXPECT_EQ(runner.results()->items[1].visibleOffset, 0u);
  EXPECT_EQ(runner.results()->items[1].spine, 1u);
  EXPECT_GT(HalMemory::largest, 200000u);
  EXPECT_EQ(runner.bytesScanned(), epub.chapters[0].size() + epub.chapters[1].size());
}

TEST_F(RunnerTest, CancelInsideChapterRollsBackItsPartialResultsAndCanRetry) {
  ASSERT_EQ(runner.begin("nguoi"), Status::Ready);
  ASSERT_EQ(runner.scanChapter(epub, 0), Status::Complete);
  epub.chapters[1] = "<body>người " + std::string(3000, 'x') + " người</body>";
  unsigned checks = 0;
  auto stop = [](void* context) { return ++*static_cast<unsigned*>(context) >= 3; };
  EXPECT_EQ(runner.scanChapter(epub, 1, stop, &checks), Status::Cancelled);
  ASSERT_EQ(runner.results()->count, 1u);
  EXPECT_EQ(runner.scanChapter(epub, 1), Status::Complete);
  EXPECT_EQ(runner.results()->count, 3u);
}

TEST_F(RunnerTest, ReadAndParseFailuresRetainEarlierChapters) {
  ASSERT_EQ(runner.begin("nguoi"), Status::Ready);
  ASSERT_EQ(runner.scanChapter(epub, 0), Status::Complete);
  epub.failRead = true;
  EXPECT_EQ(runner.scanChapter(epub, 1), Status::ReadError);
  EXPECT_EQ(runner.results()->count, 1u);
  epub.failRead = false;
  epub.chapters[1] = "<body>người";
  EXPECT_EQ(runner.scanChapter(epub, 1), Status::ParseError);
  EXPECT_EQ(runner.results()->count, 1u);
  EXPECT_EQ(runner.scanChapter(epub, -1), Status::InvalidSpine);
  EXPECT_EQ(runner.scanChapter(epub, 2), Status::InvalidSpine);
}

TEST_F(RunnerTest, EveryPsramAllocationCanRefuseWithoutLeakingOrAddingMatches) {
  ASSERT_EQ(runner.begin("nguoi"), Status::Ready);
  ASSERT_EQ(runner.scanChapter(epub, 0), Status::Complete);
  const size_t operations = HalMemory::calls;
  runner.clear();
  for (size_t fail = 1; fail <= operations; ++fail) {
    HalMemory::calls = 0;
    HalMemory::failAt = fail;
    const Status started = runner.begin("nguoi");
    if (started == Status::Ready) {
      const Status scanned = runner.scanChapter(epub, 0);
      EXPECT_EQ(scanned, Status::NoMemory) << "allocation " << fail;
      ASSERT_NE(runner.results(), nullptr);
      EXPECT_EQ(runner.results()->count, 0u);
    } else EXPECT_EQ(started, Status::NoMemory) << "allocation " << fail;
    runner.clear();
    EXPECT_EQ(HalMemory::live, 0u) << "allocation " << fail;
  }
  HalMemory::failAt = 0;
}

TEST_F(RunnerTest, FullResultSetStopsBeforeNextChapter) {
  epub.chapters[0] = "<body>";
  for (unsigned i = 0; i < 300; ++i) epub.chapters[0] += "người ";
  epub.chapters[0] += "</body>";
  ASSERT_EQ(runner.begin("nguoi"), Status::Ready);
  EXPECT_EQ(runner.scanChapter(epub, 0), Status::Full);
  ASSERT_EQ(runner.results()->count, 200u);
  const size_t reads = epub.reads;
  EXPECT_EQ(runner.scanChapter(epub, 1), Status::Full);
  EXPECT_EQ(epub.reads, reads);
}
}  // namespace
#else
TEST(BookSearchRunnerX3, UnsupportedDoesNotAllocateOrRead) {
  HalMemory::calls = 0;
  Epub epub;
  BookSearchRunner runner;
  EXPECT_FALSE(runner.supported());
  EXPECT_EQ(runner.begin("nguoi"), Status::Unsupported);
  EXPECT_EQ(runner.scanChapter(epub, 0), Status::Unsupported);
  EXPECT_EQ(runner.results(), nullptr);
  EXPECT_EQ(HalMemory::calls, 0u);
  EXPECT_EQ(epub.reads, 0u);
}
#endif
