#include <Epub/Section.h>
#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unistd.h>

namespace {
std::string readBytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
TEST(SectionBuildBudget, ColdFirstPageSurvivesParserReleaseBeforePaint) {
  const auto root = std::filesystem::temp_directory_path() / ("reader-cold-budget-" + std::to_string(getpid()));
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  auto epub = std::make_shared<Epub>();
  epub->cachePath = root.string();
  epub->contents = "<html><body>";
  for (int n = 0; n < 180; ++n) epub->contents += "<p>word" + std::to_string(n) + "</p>";
  epub->contents += "</body></html>";
  GfxRenderer renderer;
  ReaderRenderSpec spec;
  spec.viewportWidth = 160; spec.viewportHeight = 34;
  spec.embeddedStyle = false; spec.dropCapMode = 0;
  {
    Section cold(epub, 0, renderer);
    ASSERT_TRUE(cold.startBuild(spec));
    ASSERT_TRUE(cold.buildSomeMore(1));
    ASSERT_TRUE(cold.isBuilding());
    const auto pages = cold.pageCount;
    ASSERT_GT(pages, 0);
    cold.currentPage = 0;
    const auto offset = cold.getVisibleTextOffsetForPage(0);
    cold.suspendBuild();
    ASSERT_FALSE(cold.isBuilding());
    ASSERT_TRUE(cold.isPartial());
    ASSERT_EQ(cold.pageCount, pages);
    ASSERT_EQ(cold.currentPage, 0);
    ASSERT_EQ(cold.getVisibleTextOffsetForPage(0), offset);
    ASSERT_NE(cold.loadPage(0), nullptr);
    Section reopened(epub, 0, renderer);
    ASSERT_TRUE(reopened.loadSectionFile(spec));
    ASSERT_EQ(reopened.pageCount, pages);
    ASSERT_NE(reopened.loadPage(0), nullptr);
  }
  std::filesystem::remove_all(root);
}
TEST(SectionBuildBudget, SuspendedSmallerRebuildPreservesCacheBytesAndCurrentPage) {
  const auto root = std::filesystem::temp_directory_path() / ("reader-build-budget-" + std::to_string(getpid()));
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  auto epub = std::make_shared<Epub>();
  epub->cachePath = root.string();
  // This fixture exercises the cold fallback. A valid checkpoint resumes at the
  // watermark and extends it on the first tick, so it cannot produce a smaller rebuild.
  epub->contents = "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><html><body>";
  for (int n = 0; n < 180; ++n) epub->contents += "<p>word" + std::to_string(n) + "</p>";
  epub->contents += "</body></html>";
  GfxRenderer renderer;
  ReaderRenderSpec spec;
  spec.viewportWidth = 160; spec.viewportHeight = 34;
  spec.embeddedStyle = false; spec.dropCapMode = 0;
  uint16_t partialPages;
  {
    Section initial(epub, 0, renderer);
    ASSERT_TRUE(initial.startBuild(spec));
    unsigned ticks = 0;
    while (initial.pageCount < 12) {
      ASSERT_TRUE(initial.isBuilding());
      ASSERT_TRUE(initial.buildSomeMore(12));
      ASSERT_LT(++ticks, 100u);
    }
    initial.suspendBuild();
    ASSERT_FALSE(initial.isBuilding());
    ASSERT_TRUE(initial.isPartial());
    partialPages = initial.pageCount;
    ASSERT_GT(partialPages, 4);
  }
  const auto before = readBytes(root / "sections/0.bin");
  {
    Section resumed(epub, 0, renderer);
    ASSERT_TRUE(resumed.loadSectionFile(spec));
    resumed.currentPage = partialPages - 1;
    const auto offset = resumed.getVisibleTextOffsetForPage(resumed.currentPage);
    ASSERT_TRUE(offset.has_value());
    ASSERT_TRUE(resumed.startBuild(spec));
    ASSERT_TRUE(resumed.buildSomeMore(1));
    resumed.suspendBuild();
    ASSERT_FALSE(resumed.isBuilding());
    ASSERT_TRUE(resumed.isPartial());
    ASSERT_EQ(resumed.pageCount, partialPages);
    ASSERT_EQ(resumed.currentPage, partialPages - 1);
    ASSERT_EQ(readBytes(root / "sections/0.bin"), before);
    ASSERT_EQ(resumed.getVisibleTextOffsetForPage(resumed.currentPage), offset);
    ASSERT_NE(resumed.loadPage(resumed.currentPage), nullptr);
    ASSERT_FALSE(std::filesystem::exists(root / "sections/0.bin.part"));
    // Explicit demand crosses the suspended watermark and persists a larger partial.
    ++resumed.currentPage;
    ASSERT_TRUE(resumed.startBuild(spec));
    while (resumed.isBuilding() && resumed.currentPage >= resumed.pageCount) ASSERT_TRUE(resumed.buildSomeMore(2));
    ASSERT_GT(resumed.pageCount, resumed.currentPage);
    resumed.suspendBuild();
    ASSERT_GT(resumed.pageCount, resumed.currentPage);
    ASSERT_NE(resumed.loadPage(resumed.currentPage), nullptr);
  }
  std::filesystem::remove_all(root);
}
}
