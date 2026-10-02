// Laying out a single-file novel whose table of contents has thousands of
// anchors, under the heap the reader has while the page-turner radio runs.
#include "HeapCap.h"

#include <Epub/Section.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>

namespace {
// Heap left to the reader while the radio runs (X3, BLE on).
constexpr size_t RADIO_HEAP = 45 * 1024;
// Heap once the reader stops the radio for a starved build (releaseHeapForBuild).
constexpr size_t RADIO_RELEASED_HEAP = 110 * 1024;

// A build that starves of heap parks; the reader then stops the radio once and
// lays out again. Anything else that stops the build is a failure.
bool buildTick(Section& section, int pages, bool& released) {
  if (section.buildSomeMore(pages)) return true;
  if (!section.buildStarved() || released) return false;
  released = true;
  heapcap::cap = std::max(heapcap::cap, RADIO_RELEASED_HEAP);
  return true;
}

class HugeBookSection : public ::testing::Test {
 protected:
  std::filesystem::path root;
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  GfxRenderer renderer;
  ReaderRenderSpec spec;
  bool radioReleased = false;

  void SetUp() override {
    root = std::filesystem::temp_directory_path() /
           ("huge-section-" + std::to_string(getpid()) + "-" +
            ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    epub->cachePath = root.string();
    spec.viewportWidth = 480;
    spec.viewportHeight = 320;
    spec.embeddedStyle = false;
    spec.dropCapMode = 0;
  }
  void TearDown() override {
    heapcap::stop();
    std::filesystem::remove_all(root);
  }
  // One XHTML file, one short chapter per TOC anchor: several chapters fit on
  // one page unless the TOC boundary starts a fresh page.
  // idsPerChapter > 0 gives every paragraph an id, as some converters do; those
  // ids compete with the chapter anchors for the parser's anchor budget.
  void makeBook(int chapters, int idsPerChapter = 0) {
    epub->tocCount = chapters;
    epub->contents = "<html><body>";
    for (int i = 0; i < chapters; ++i) {
      epub->contents += "<h2 id=\"" + Epub::anchorFor(i) + "\">Chuong " + std::to_string(i + 1) + "</h2>";
      if (idsPerChapter == 0) epub->contents += "<p>mua nang gio chieu sang toi mat tay long viec</p>";
      for (int k = 0; k < idsPerChapter; ++k) {
        epub->contents += "<p id=\"p" + std::to_string(i) + "_" + std::to_string(k) + "\">mua nang gio</p>";
      }
    }
    epub->contents += "</body></html>";
  }
  // Lays out the way the reader does for a TOC jump: a few pages per tick,
  // parked between ticks, until the target anchor is on a page or the chapter
  // ends. Returns the target's page, or nothing when the jump would miss.
  std::optional<uint16_t> buildToAnchor(Section& section, const std::string& anchor) {
    if (!section.startBuild(spec)) return std::nullopt;
    bool& released = radioReleased = false;
    for (int tick = 0; tick < 20000 && !section.isBuildComplete() && !section.findAnchor(anchor); ++tick) {
      if (!buildTick(section, 3, released)) return std::nullopt;
      if (!section.isBuildComplete()) section.parkBuild();
    }
    return section.findAnchor(anchor);
  }
  // A chapter jump is right when the target page holds the chapter start: after
  // the previous chapter's page and before the next one's.
  void expectChapterJump(int chapters, int target, size_t cap) {
    makeBook(chapters, 5);
    std::filesystem::remove_all(root / "sections");
    std::filesystem::remove_all(root / "html");
    Section section(epub, 0, renderer);
    heapcap::reset(cap);
    const auto page = buildToAnchor(section, Epub::anchorFor(target - 1));
    const unsigned aborts = heapcap::aborts;
    const size_t peak = heapcap::peak;
    heapcap::stop();
    printf("HUGE_SECTION jump chapters=%d target=%d cap=%zu page=%d built=%u complete=%d peak=%zu aborts=%u first=%zu "
           "radio_released=%d\n",
           chapters, target, cap == SIZE_MAX ? 0 : cap, page ? *page : -1, section.pageCount,
           section.isBuildComplete() ? 1 : 0, peak, aborts, heapcap::firstAbortSize, radioReleased ? 1 : 0);
    EXPECT_EQ(aborts, 0u) << "device would abort";
    ASSERT_TRUE(page.has_value()) << "chapter " << target << " is not in the anchor map: the jump lands on page 0";
    const auto before = section.findAnchor(Epub::anchorFor(target - 2));
    ASSERT_TRUE(before.has_value()) << "chapter " << target - 1;
    EXPECT_GT(*page, *before) << "chapter " << target << " must start its own page";
  }
};
}  // namespace

// Step 1 of the investigation: heap taken by starting a build, by anchor count.
TEST_F(HugeBookSection, StartBuildPeakByAnchorCount) {
  for (int n : {100, 1000, 2000, 5000}) {
    makeBook(n);
    std::filesystem::remove_all(root / "sections");
    Section section(epub, 0, renderer);
    heapcap::reset(SIZE_MAX);
    epub->tocReads = 0;
    const bool started = section.startBuild(spec);
    printf("HUGE_SECTION start n=%d ok=%d peak=%zu live=%zu toc_reads=%d\n", n, started ? 1 : 0, heapcap::peak,
           heapcap::live, epub->tocReads);
    heapcap::stop();
    section.abandonBuild();
  }
}

// X3 log r45b: opening the 5,000-chapter single-file book aborted in
// makeBuildParser while collecting every TOC anchor of the spine.
TEST_F(HugeBookSection, FiveThousandAnchorsStartAndTurnUnderRadioHeap) {
  makeBook(5000);
  Section section(epub, 0, renderer);
  heapcap::reset(RADIO_HEAP);
  const bool started = section.startBuild(spec);
  printf("HUGE_SECTION radio_start n=5000 ok=%d peak=%zu aborts=%u first_abort_request=%zu live=%zu\n", started ? 1 : 0,
         heapcap::peak, heapcap::aborts, heapcap::firstAbortSize, heapcap::firstAbortLive);
  ASSERT_EQ(heapcap::aborts, 0u) << "device would abort while starting the build";
  ASSERT_TRUE(started);
  // Lay out pages the way the reader does while reading on: a few pages per
  // tick, parking the parser between ticks to free render heap.
  bool released = false;
  for (int tick = 0; tick < 40 && section.isBuilding(); ++tick) {
    ASSERT_TRUE(buildTick(section, 2, released));
    section.parkBuild();
  }
  const size_t peak = heapcap::peak;
  const unsigned aborts = heapcap::aborts;
  heapcap::stop();
  printf("HUGE_SECTION radio n=5000 pages=%u peak=%zu aborts=%u toc_reads=%d\n", section.pageCount, peak, aborts,
         epub->tocReads);
  EXPECT_EQ(aborts, 0u) << "device would abort (first request " << heapcap::firstAbortSize << " B with "
                        << heapcap::firstAbortLive << " B live)";
  EXPECT_GE(section.pageCount, 40u);
  // Early chapters are TOC boundaries: each starts its own page.
  std::optional<uint16_t> previous;
  for (int i = 0; i < 30; ++i) {
    const auto page = section.findAnchor(Epub::anchorFor(i));
    ASSERT_TRUE(page.has_value()) << Epub::anchorFor(i);
    if (previous) EXPECT_GT(*page, *previous) << Epub::anchorFor(i);
    previous = page;
  }
}

// The TOC anchors a build honours must not depend on when the reader parked
// and resumed it, or two builds of one chapter paginate differently.
TEST_F(HugeBookSection, PaginationDoesNotDependOnParking) {
  makeBook(600);
  uint16_t oneShot = 0;
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.createSectionFile(spec));
    oneShot = section.pageCount;
  }
  std::filesystem::remove_all(root / "sections");
  uint16_t parked = 0;
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.startBuild(spec));
    for (int tick = 0; tick < 5000 && !section.isBuildComplete(); ++tick) {
      ASSERT_TRUE(section.buildSomeMore(3));
      if (!section.isBuildComplete()) section.parkBuild();
    }
    ASSERT_TRUE(section.isBuildComplete());
    parked = section.pageCount;
  }
  printf("HUGE_SECTION pagination n=600 one_shot=%u parked=%u\n", oneShot, parked);
  EXPECT_EQ(oneShot, parked);
  // Every chapter stays reachable from the table of contents.
  Section section(epub, 0, renderer);
  ASSERT_TRUE(section.loadSectionFile(spec));
  for (int i = 0; i < 600; ++i) EXPECT_TRUE(section.getPageForAnchor(Epub::anchorFor(i)).has_value()) << i;
}

// Review finding D1: a single-file book whose paragraphs carry ids used up the
// parser's id budget early, and from the 257th chapter on a TOC jump found no
// anchor and fell back to the first page. v1.0.13 jumped right.
TEST_F(HugeBookSection, TocJumpPastTheIdBudgetLandsOnTheChapter) {
  expectChapterJump(300, 280, SIZE_MAX);
  expectChapterJump(700, 650, SIZE_MAX);
}

// The same jumps with the page-turner radio running.
TEST_F(HugeBookSection, TocJumpPastTheIdBudgetUnderRadioHeap) {
  expectChapterJump(300, 280, RADIO_HEAP);
  expectChapterJump(700, 650, RADIO_HEAP);
  // Too tight to reserve the chapter table: the build starves, the reader stops
  // the radio, and the jump still lands on the chapter.
  expectChapterJump(700, 650, 34 * 1024);
  EXPECT_TRUE(radioReleased);
}
