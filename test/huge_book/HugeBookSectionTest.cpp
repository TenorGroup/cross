// Laying out a single-file novel whose table of contents has thousands of
// anchors, under the heap the reader has while the page-turner radio runs.
#include "HeapCap.h"

#include <Epub/Section.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>

namespace {
// Heap left to the reader while the radio runs (X3, BLE on).
constexpr size_t RADIO_HEAP = 45 * 1024;

class HugeBookSection : public ::testing::Test {
 protected:
  std::filesystem::path root;
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  GfxRenderer renderer;
  ReaderRenderSpec spec;

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
  void makeBook(int chapters) {
    epub->tocCount = chapters;
    epub->contents = "<html><body>";
    for (int i = 0; i < chapters; ++i) {
      epub->contents += "<h2 id=\"" + Epub::anchorFor(i) + "\">Chuong " + std::to_string(i + 1) +
                        "</h2><p>mua nang gio chieu sang toi mat tay long viec</p>";
    }
    epub->contents += "</body></html>";
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
  for (int tick = 0; tick < 40 && section.isBuilding(); ++tick) {
    ASSERT_TRUE(section.buildSomeMore(2));
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
