#include <Epub/Section.h>
#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <unistd.h>
#include <cstdlib>
#include <new>
#include <iostream>
#include <chrono>

namespace allocationProbe {
bool enabled = false;
size_t largest = 0;
size_t rejectSize = 0;
unsigned matchingCalls = 0;
unsigned rejectCall = 0;
unsigned rejected = 0;
}
void* operator new(std::size_t size) {
  if (allocationProbe::enabled) allocationProbe::largest = std::max(allocationProbe::largest, size);
  if (void* value = std::malloc(size ? size : 1)) return value;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  if (size == allocationProbe::rejectSize && ++allocationProbe::matchingCalls == allocationProbe::rejectCall) {
    ++allocationProbe::rejected;
    return nullptr;
  }
  try { return ::operator new(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept { return ::operator new(size, tag); }
void operator delete(void* value, const std::nothrow_t&) noexcept { std::free(value); }
void operator delete[](void* value, const std::nothrow_t&) noexcept { std::free(value); }
namespace {
std::string bytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
template <typename T> T pod(const std::string& raw, size_t offset) {
  T result{};
  if (offset + sizeof(result) <= raw.size()) memcpy(&result, raw.data() + offset, sizeof(result));
  return result;
}
class SectionCacheTest : public ::testing::Test {
 protected:
  std::filesystem::path root;
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  GfxRenderer renderer;
  ReaderRenderSpec spec;
  std::vector<std::string> expected;
  std::string valid;
  uint16_t fullPages = 0;
  std::vector<uint32_t> pageOffsets;
  uint32_t lutOffset = 0;
  static constexpr size_t pageCountOffset = 22;
  static constexpr size_t pageLutHeaderOffset = 24;
  std::filesystem::path cache() const { return root / "sections/0.bin"; }
  void SetUp() override {
    storageFault::reset();
    root = std::filesystem::temp_directory_path() /
      ("cross-section-" + std::to_string(getpid()) + "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    epub->cachePath = root.string();
    epub->contents = "<html><body>";
    for (int i = 0; i < 180; ++i) {
      std::string word = "word" + std::to_string(i);
      expected.push_back(word);
      epub->contents += "<p>" + word + "</p>";
    }
    epub->contents += "</body></html>";
    spec.viewportWidth = 160; spec.viewportHeight = 34;
    spec.embeddedStyle = false; spec.dropCapMode = 0;
    Section initial(epub, 0, renderer);
    ASSERT_TRUE(initial.createSectionFile(spec));
    fullPages = initial.pageCount;
    ASSERT_GT(fullPages, 8);
    valid = bytes(cache());
    ASSERT_EQ(pod<uint16_t>(valid, pageCountOffset), fullPages);
    lutOffset = pod<uint32_t>(valid, pageLutHeaderOffset);
    for (uint16_t i = 0; i < fullPages; ++i) pageOffsets.push_back(pod<uint32_t>(valid, lutOffset + i * 4));
    ASSERT_GT(pageOffsets.front(), 0u);
    ASSERT_LT(pageOffsets.back(), lutOffset);
  }
  void TearDown() override { storageMetrics::enabled = false; allocationProbe::enabled = false; allocationProbe::rejectSize = 0; storageFault::reset(); std::filesystem::remove_all(root); }
  void armWrite(size_t offset) {
    storageFault::target = "/sections/0.bin.part";
    storageFault::writeOffset = offset;
    storageFault::failOnce = true;
  }
  void retainedBackupCanRebuild(bool partial) {
    {
      Section build(epub, 0, renderer);
      ASSERT_TRUE(build.startBuild(spec));
      storageFault::target = "/sections/0.bin.davbak";
      storageFault::failRemove = true;
      ASSERT_TRUE(build.buildSomeMore(partial ? 2 : 0));
      EXPECT_EQ(build.isBuilding(), partial);
    }
    ASSERT_EQ(storageFault::hit, 1u);
    storageFault::reset();
    ASSERT_TRUE(std::filesystem::exists(cache().string() + ".davbak"));
    EXPECT_TRUE(bytes(cache().string() + ".davbak") == valid);
    {
      Section reopened(epub, 0, renderer);
      ASSERT_TRUE(reopened.loadSectionFile(spec));
      EXPECT_EQ(reopened.isPartial(), partial);
      ASSERT_TRUE(reopened.createSectionFile(spec));
      ASSERT_TRUE(reopened.isBuildComplete());
      ASSERT_EQ(reopened.pageCount, fullPages);
    }
    EXPECT_FALSE(std::filesystem::exists(cache().string() + ".davbak"));
    retryAndCheckEveryWord();
  }
  void retryAndCheckEveryWord() {
    storageFault::reset();
    Section retry(epub, 0, renderer);
    ASSERT_TRUE(retry.createSectionFile(spec));
    ASSERT_TRUE(retry.isBuildComplete());
    ASSERT_EQ(retry.pageCount, fullPages);
    std::vector<std::string> actual;
    for (uint16_t p = 0; p < retry.pageCount; ++p) {
      auto page = retry.loadPage(p);
      ASSERT_NE(page, nullptr) << p;
      for (const auto& element : page->elements) {
        if (element->getTag() != TAG_PageLine) continue;
        const auto* block = static_cast<const PageLine&>(*element).getBlock();
        for (uint16_t w = 0; w < block->wordCount(); ++w) actual.emplace_back(block->wordText(w));
      }
    }
    EXPECT_EQ(actual, expected);
  }
};
TEST_F(SectionCacheTest, FullProductionRoundTripPreservesAllBookWords) { retryAndCheckEveryWord(); }
TEST_F(SectionCacheTest, RejectsFinalCacheFromHeapDependentCssResolver) {
  std::string legacy = valid;
  legacy[0] = 53;
  {
    std::ofstream output(cache(), std::ios::binary | std::ios::trunc);
    output.write(legacy.data(), legacy.size());
  }
  Section reopened(epub, 0, renderer);
  EXPECT_FALSE(reopened.loadSectionFile(spec));
}
TEST_F(SectionCacheTest, RejectsPartialCacheFromHeapDependentCssResolver) {
  {
    Section partial(epub, 0, renderer);
    ASSERT_TRUE(partial.startBuild(spec));
    ASSERT_TRUE(partial.buildSomeMore(2));
    ASSERT_TRUE(partial.isBuilding());
    partial.suspendBuild();
    ASSERT_TRUE(partial.isPartial());
  }
  std::string legacy = bytes(cache());
  ASSERT_GT(legacy.size(), 0u);
  legacy[0] = static_cast<char>(0xFE - (53 - 28));
  {
    std::ofstream output(cache(), std::ios::binary | std::ios::trunc);
    output.write(legacy.data(), legacy.size());
  }
  Section reopened(epub, 0, renderer);
  EXPECT_FALSE(reopened.loadSectionFile(spec));
}
TEST_F(SectionCacheTest, MiddlePageShortWriteFailsImmediatelyAndKeepsPreviousCache) {
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.startBuild(spec));
    armWrite(pageOffsets[3]);
    EXPECT_FALSE(section.buildSomeMore(2));
    EXPECT_FALSE(section.isBuildComplete());
    EXPECT_EQ(storageFault::hit, 1u);
    storageFault::reset();
  }
  EXPECT_TRUE(bytes(cache()) == valid) << "Previously committed cache bytes changed";
  retryAndCheckEveryWord();
}
TEST_F(SectionCacheTest, LastPageShortWriteFailsAndKeepsPreviousCache) {
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.startBuild(spec));
    armWrite(pageOffsets.back());
    EXPECT_FALSE(section.buildSomeMore(0));
    EXPECT_FALSE(section.isBuildComplete());
    EXPECT_EQ(storageFault::hit, 1u);
    storageFault::reset();
  }
  EXPECT_TRUE(bytes(cache()) == valid) << "Previously committed cache bytes changed";
  retryAndCheckEveryWord();
}
TEST_F(SectionCacheTest, FinalLutShortWriteRejectsCommitAndKeepsPreviousCache) {
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.startBuild(spec));
    armWrite(lutOffset);
    EXPECT_FALSE(section.buildSomeMore(0));
    EXPECT_FALSE(section.isBuildComplete());
    EXPECT_EQ(storageFault::hit, 1u);
    storageFault::reset();
  }
  EXPECT_TRUE(bytes(cache()) == valid) << "Previously committed cache bytes changed";
  retryAndCheckEveryWord();
}
TEST_F(SectionCacheTest, DestructorDoesNotPromoteFailedPartialCommit) {
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.startBuild(spec));
    ASSERT_TRUE(section.buildSomeMore(2));
    ASSERT_TRUE(section.isBuilding());
    ASSERT_GT(section.pageCount, 2u);
    armWrite(0);
  }
  EXPECT_EQ(storageFault::hit, 1u);
  storageFault::reset();
  EXPECT_TRUE(bytes(cache()) == valid) << "Previously committed cache bytes changed";
  retryAndCheckEveryWord();
}
TEST_F(SectionCacheTest, FailedSyncRejectsCommitAndKeepsPreviousCache) {
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.startBuild(spec));
    storageFault::target = "/sections/0.bin.part";
    storageFault::failFlush = true;
    EXPECT_FALSE(section.buildSomeMore(0));
    EXPECT_FALSE(section.isBuildComplete());
    storageFault::reset();
  }
  EXPECT_TRUE(bytes(cache()) == valid) << "Previously committed cache bytes changed";
  retryAndCheckEveryWord();
}
TEST_F(SectionCacheTest, TextBlockOomAfterSeveralPagesFailsAndKeepsPreviousCache) {
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.startBuild(spec));
    allocationProbe::rejectSize = sizeof(TextBlock);
    allocationProbe::matchingCalls = 0;
    allocationProbe::rejectCall = 4;
    allocationProbe::rejected = 0;
    EXPECT_FALSE(section.buildSomeMore(0));
    EXPECT_FALSE(section.isBuildComplete());
    EXPECT_EQ(allocationProbe::rejected, 1u);
    allocationProbe::rejectSize = 0;
  }
  EXPECT_TRUE(bytes(cache()) == valid) << "Previously committed cache bytes changed";
  retryAndCheckEveryWord();
}
TEST_F(SectionCacheTest, TenThousandPagesKeepLutAllocationBoundedAndRemainReadable) {
  constexpr unsigned count = 10050;
  epub->contents = "<html><body>";
  for (unsigned i = 0; i < count; ++i) epub->contents += "<p>word" + std::to_string(i) + "</p>";
  epub->contents += "</body></html>";
  std::filesystem::remove(root / "html/0.html");
  Section section(epub, 0, renderer);
  allocationProbe::largest = 0;
  allocationProbe::enabled = true;
  storageMetrics::begin();
  const auto buildStart = std::chrono::steady_clock::now();
  const bool ok = section.createSectionFile(spec);
  const auto buildEnd = std::chrono::steady_clock::now();
  storageMetrics::enabled = false;
  allocationProbe::enabled = false;
  ASSERT_TRUE(ok);
  ASSERT_EQ(section.pageCount, count);
  std::cout << "SECTION_LUT pages=" << count << " largest_cpp_allocation=" << allocationProbe::largest
    << " build_us=" << std::chrono::duration_cast<std::chrono::microseconds>(buildEnd - buildStart).count()
    << " read_calls=" << storageMetrics::readCalls << " read_bytes=" << storageMetrics::readBytes
    << " write_calls=" << storageMetrics::writeCalls << " write_bytes=" << storageMetrics::writeBytes
    << " seek_calls=" << storageMetrics::seekCalls << " sidecar_max_bytes=" << storageMetrics::sidecarMaxBytes << "\n";
  EXPECT_LE(allocationProbe::largest, 8192u);
  const auto jumpStart = std::chrono::steady_clock::now();
  storageMetrics::begin();
  for (uint16_t i = 0; i < count; ++i) {
    const auto page = section.loadPage(i);
    ASSERT_NE(page, nullptr) << i;
    ASSERT_EQ(page->elements.size(), 1u);
    const auto* block = static_cast<const PageLine&>(*page->elements.front()).getBlock();
    ASSERT_EQ(block->wordCount(), 1u);
    EXPECT_EQ(block->wordText(0), "word" + std::to_string(i));
  }
  const auto jumpEnd = std::chrono::steady_clock::now();
  storageMetrics::enabled = false;
  std::cout << "SECTION_READ pages=" << count
    << " all_page_reads_us=" << std::chrono::duration_cast<std::chrono::microseconds>(jumpEnd - jumpStart).count()
    << " read_calls=" << storageMetrics::readCalls << " read_bytes=" << storageMetrics::readBytes
    << " seek_calls=" << storageMetrics::seekCalls << "\n";
}
TEST_F(SectionCacheTest, FailedSwapKeepsPreviousCache) {
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.startBuild(spec));
    storageFault::target = "/sections/0.bin.part";
    storageFault::failRename = true;
    EXPECT_FALSE(section.buildSomeMore(0));
    EXPECT_FALSE(section.isBuildComplete());
    EXPECT_EQ(storageFault::hit, 1u);
    storageFault::reset();
  }
  EXPECT_TRUE(bytes(cache()) == valid) << "Previously committed cache bytes changed";
  retryAndCheckEveryWord();
}
TEST_F(SectionCacheTest, TruncatedHeadersAreRejectedBeforeAnyLargeAllocation) {
  for (size_t length = 0; length < 44; ++length) {
    std::ofstream out(cache(), std::ios::binary | std::ios::trunc);
    out.write(valid.data(), length); out.close();
    Section section(epub, 0, renderer);
    allocationProbe::largest = 0;
    allocationProbe::enabled = true;
    const bool accepted = section.loadSectionFile(spec);
    allocationProbe::enabled = false;
    EXPECT_FALSE(accepted) << "truncated header length=" << length;
    EXPECT_LE(allocationProbe::largest, 8192u);
  }
}
TEST_F(SectionCacheTest, CorruptLutRangesAndPageCountAreRejectedBeforeAllocation) {
  for (const size_t offset : {size_t(22), size_t(24), size_t(28), size_t(32), size_t(36), size_t(40)}) {
    auto corrupted = valid;
    if (offset == 22) {
      const uint16_t count = UINT16_MAX;
      memcpy(corrupted.data() + offset, &count, sizeof(count));
    } else {
      const uint32_t position = UINT32_MAX - 8;
      memcpy(corrupted.data() + offset, &position, sizeof(position));
    }
    std::ofstream out(cache(), std::ios::binary | std::ios::trunc);
    out.write(corrupted.data(), corrupted.size()); out.close();
    Section section(epub, 0, renderer);
    allocationProbe::largest = 0;
    allocationProbe::enabled = true;
    const bool accepted = section.loadSectionFile(spec);
    allocationProbe::enabled = false;
    EXPECT_FALSE(accepted) << "corrupt field offset=" << offset;
    EXPECT_LE(allocationProbe::largest, 8192u);
  }
}
TEST_F(SectionCacheTest, ActiveLutShortWriteFailsAndKeepsPreviousCache) {
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.startBuild(spec));
    storageFault::target = "/sections/0.bin.lut.part";
    storageFault::writeOffset = 36;
    storageFault::failOnce = true;
    EXPECT_FALSE(section.buildSomeMore(2));
    EXPECT_FALSE(section.isBuildComplete());
    EXPECT_EQ(storageFault::hit, 1u);
    storageFault::reset();
  }
  EXPECT_TRUE(bytes(cache()) == valid);
  retryAndCheckEveryWord();
}
TEST_F(SectionCacheTest, PartialResumePreservesReadAccessAndVisibleOffsets) {
  std::filesystem::remove(cache());
  uint16_t partialPages = 0;
  {
    Section partial(epub, 0, renderer);
    ASSERT_TRUE(partial.startBuild(spec));
    ASSERT_TRUE(partial.buildSomeMore(2));
    ASSERT_TRUE(partial.isBuilding());
    partialPages = partial.pageCount;
    ASSERT_GT(partialPages, 2u);
    ASSERT_LT(partialPages, fullPages);
    uint32_t offset = 0;
    for (uint16_t p = 0; p < partialPages; ++p) {
      EXPECT_EQ(partial.getVisibleTextOffsetForPage(p), offset);
      EXPECT_EQ(partial.getPageForVisibleTextOffset(offset, true), p);
      ASSERT_NE(partial.loadPage(p), nullptr);
      offset += expected[p].size();
    }
  }
  {
    Section resumed(epub, 0, renderer);
    ASSERT_TRUE(resumed.loadSectionFile(spec));
    ASSERT_TRUE(resumed.isPartial());
    ASSERT_EQ(resumed.pageCount, partialPages);
    ASSERT_TRUE(resumed.startBuild(spec));
    const auto oldPage = resumed.loadPage(partialPages - 1);
    ASSERT_NE(oldPage, nullptr);
    const auto* oldBlock = static_cast<const PageLine&>(*oldPage->elements.front()).getBlock();
    EXPECT_EQ(oldBlock->wordText(0), expected[partialPages - 1]);
    ASSERT_TRUE(resumed.buildSomeMore(0));
    ASSERT_EQ(resumed.pageCount, fullPages);
    ASSERT_FALSE(resumed.isPartial());
  }
  retryAndCheckEveryWord();
}
TEST_F(SectionCacheTest, DuringBuildLookupKeepsFirstAndLastDuplicateVisibleOffsets) {
  std::filesystem::remove(cache());
  std::filesystem::remove(root / "html/0.html");
  epub->contents = "<html><body>";
  for (unsigned i = 0; i < 500; ++i) epub->contents += "<hr/>";
  epub->contents += "<p>end</p></body></html>";
  Section section(epub, 0, renderer);
  ASSERT_TRUE(section.startBuild(spec));
  ASSERT_TRUE(section.buildSomeMore(2));
  ASSERT_TRUE(section.isBuilding());
  ASSERT_GT(section.pageCount, 3);
  EXPECT_EQ(section.getVisibleTextOffsetForPage(section.pageCount - 1), 0u);
  EXPECT_EQ(section.getPageForVisibleTextOffset(0, true), 0u);
  EXPECT_EQ(section.getPageForVisibleTextOffset(0, false), section.pageCount - 1);
}

TEST_F(SectionCacheTest, SkippedLargeSubtreeYieldsWithinFourParseChunks) {
  std::filesystem::remove(cache());
  std::filesystem::remove(root / "html/0.html");
  epub->contents = "<html><head><style>" + std::string(100 * 1024, 'x') +
                   "</style></head><body><p>word</p></body></html>";
  Section section(epub, 0, renderer);
  ASSERT_TRUE(section.startBuild(spec));
  storageMetrics::begin();
  const bool advanced = section.buildSomeMore(1);
  storageMetrics::enabled = false;
  ASSERT_TRUE(advanced);
  EXPECT_TRUE(section.isBuilding());
  EXPECT_EQ(section.pageCount, 0);
  EXPECT_LE(storageMetrics::readBytes, 4096u);
  std::cout << "SECTION_TICK skipped_subtree_bytes=" << 100 * 1024
            << " first_tick_read_bytes=" << storageMetrics::readBytes << "\n";
}

TEST_F(SectionCacheTest, RetainedBackupAfterFinalPromotionAllowsValidatedRebuild) {
  retainedBackupCanRebuild(false);
}
TEST_F(SectionCacheTest, RetainedBackupAfterPartialPromotionAllowsValidatedRebuild) {
  retainedBackupCanRebuild(true);
}
TEST_F(SectionCacheTest, CorruptCanonicalWithBackupIsRejectedAndRegenerated) {
  std::filesystem::copy_file(cache(), cache().string() + ".davbak");
  auto corrupted = valid;
  const uint32_t invalidOffset = UINT32_MAX;
  memcpy(corrupted.data() + pageLutHeaderOffset, &invalidOffset, sizeof(invalidOffset));
  std::ofstream out(cache(), std::ios::binary | std::ios::trunc);
  out.write(corrupted.data(), corrupted.size());
  out.close();
  Section reopened(epub, 0, renderer);
  EXPECT_FALSE(reopened.loadSectionFile(spec));
  ASSERT_TRUE(reopened.createSectionFile(spec));
  ASSERT_TRUE(reopened.isBuildComplete());
  EXPECT_EQ(reopened.pageCount, fullPages);
  retryAndCheckEveryWord();
}

}
