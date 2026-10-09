#include <Epub/Section.h>
#include <Epub/Page.h>
#include <Epub/css/CssParser.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>
#include <Arduino.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>
#include <unistd.h>
#include <cstdlib>
#include <new>
#include <iostream>
#include <chrono>
#ifdef __APPLE__
#include <malloc/malloc.h>
#define usableSize malloc_size
#else
#include <malloc.h>
#define usableSize malloc_usable_size
#endif

namespace parserImageFixture {
extern bool enabled;
}

namespace allocationProbe {
bool enabled = false;
// Live bytes allocated since `enabled` went on, and their peak (frees of older blocks count too, so the
// figure is the peak growth of the heap). Host pointers are 8 bytes, so this is an upper bound for the X3.
long live = 0;
long peak = 0;
size_t largest = 0;
size_t calls = 0;
size_t liveLimit = 0;
size_t requestLimit = 0;
size_t rejectSize = 0;
bool rejectNextNothrow = false;
unsigned matchingCalls = 0;
unsigned rejectCall = 0;
unsigned rejected = 0;
}
void* operator new(std::size_t size) {
  if (allocationProbe::enabled) {
    allocationProbe::largest = std::max(allocationProbe::largest, size);
    ++allocationProbe::calls;
    if ((allocationProbe::liveLimit && allocationProbe::live + static_cast<long>(size) >
                                          static_cast<long>(allocationProbe::liveLimit)) ||
        (allocationProbe::requestLimit && size > allocationProbe::requestLimit)) {
      ++allocationProbe::rejected;
      throw std::bad_alloc();
    }
  }
  if (void* value = std::malloc(size ? size : 1)) {
    if (allocationProbe::enabled) {
      allocationProbe::live += static_cast<long>(usableSize(value));
      allocationProbe::peak = std::max(allocationProbe::peak, allocationProbe::live);
    }
    return value;
  }
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept {
  if (allocationProbe::enabled && value) allocationProbe::live -= static_cast<long>(usableSize(value));
  std::free(value);
}
void operator delete[](void* value) noexcept { ::operator delete(value); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  if (allocationProbe::rejectNextNothrow) {
    allocationProbe::rejectNextNothrow = false;
    ++allocationProbe::rejected;
    return nullptr;
  }
  if (size == allocationProbe::rejectSize && ++allocationProbe::matchingCalls == allocationProbe::rejectCall) {
    ++allocationProbe::rejected;
    return nullptr;
  }
  try { return ::operator new(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept { return ::operator new(size, tag); }
void operator delete(void* value, const std::nothrow_t&) noexcept { ::operator delete(value); }
void operator delete[](void* value, const std::nothrow_t&) noexcept { std::free(value); }
namespace {
template <typename T>
bool parkSection(T& section) {
  if constexpr (requires(T& candidate) { candidate.parkBuild(); }) {
    return section.parkBuild();
  } else {
    section.suspendBuild();
    return true;
  }
}

template <typename T>
bool sectionIsParked(const T& section) {
  if constexpr (requires(const T& candidate) { candidate.isBuildParked(); }) {
    return section.isBuildParked();
  }
  return false;
}

template <typename T>
bool resumeSection(T& section, const ReaderRenderSpec& spec, int maxPages) {
  if (!section.isBuilding() && !section.startBuild(spec)) return false;
  return section.buildSomeMore(maxPages);
}

std::string bytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
template <typename T> T pod(const std::string& raw, size_t offset) {
  T result{};
  if (offset + sizeof(result) <= raw.size()) memcpy(&result, raw.data() + offset, sizeof(result));
  return result;
}
bool serializedPageSignature(const Page& page, const std::filesystem::path& path, std::string& result) {
  HalFile output;
  if (!output.open(path.c_str(), "w+b") || !page.serialize(output) || !output.flush() || !output.close()) return false;
  const std::string pageBytes = bytes(path);
  result.assign(reinterpret_cast<const char*>(&page.visibleTextOffset), sizeof(page.visibleTextOffset));
  result += pageBytes;
  return true;
}
std::optional<size_t> checkpointOffset(const std::string& raw) {
  constexpr size_t pageCountOffset = 22;
  constexpr size_t visibleLutOffset = 40;
  if (raw.size() < visibleLutOffset + sizeof(uint32_t)) return std::nullopt;
  const uint64_t offset = static_cast<uint64_t>(pod<uint32_t>(raw, visibleLutOffset)) +
                          static_cast<uint64_t>(pod<uint16_t>(raw, pageCountOffset)) * sizeof(uint32_t) +
                          2 * sizeof(uint32_t);
  if (offset > raw.size()) return std::nullopt;
  return static_cast<size_t>(offset);
}
struct LutColumn { size_t begin, end; const char* name; };
std::optional<std::array<LutColumn, 4>> lutColumns(const std::string& raw) {
  if (raw.size() < 44) return std::nullopt;
  const size_t count = pod<uint16_t>(raw, 22);
  const size_t pages = pod<uint32_t>(raw, 24);
  const size_t anchors = pod<uint32_t>(raw, 28);
  const size_t paragraphs = pod<uint32_t>(raw, 32);
  const size_t items = pod<uint32_t>(raw, 36);
  const size_t visible = pod<uint32_t>(raw, 40);
  if (!count || pages < 44 || anchors != pages + 4 * count ||
      paragraphs < anchors || paragraphs + 2 > raw.size() ||
      pod<uint16_t>(raw, paragraphs) != count ||
      items != paragraphs + 2 + 2 * count || visible != items + 2 * count ||
      visible + 4 * count > raw.size()) return std::nullopt;
  return std::array<LutColumn, 4>{{
      {pages, anchors, "page"}, {paragraphs + 2, items, "paragraph"},
      {items, visible, "list-item"}, {visible, visible + 4 * count, "visible"}}};
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
    std::cout << "SECTION_PARTIAL fixture_full_pages=" << fullPages << "\n";
    ASSERT_GT(fullPages, 8);
    valid = bytes(cache());
    ASSERT_EQ(pod<uint16_t>(valid, pageCountOffset), fullPages);
    lutOffset = pod<uint32_t>(valid, pageLutHeaderOffset);
    for (uint16_t i = 0; i < fullPages; ++i) pageOffsets.push_back(pod<uint32_t>(valid, lutOffset + i * 4));
    ASSERT_GT(pageOffsets.front(), 0u);
    ASSERT_LT(pageOffsets.back(), lutOffset);
  }
  void TearDown() override { storageMetrics::enabled = false; storageLutWrites::enabled = false; allocationProbe::enabled = false; allocationProbe::rejectSize = 0; allocationProbe::rejectNextNothrow = false; storageFault::reset(); std::filesystem::remove_all(root); }
  void checkLutWriteBudget(const std::string& raw) {
    const auto columns = lutColumns(raw);
    ASSERT_TRUE(columns.has_value());
    const size_t pages = pod<uint16_t>(raw, pageCountOffset);
    ASSERT_GT(pages, 128u);
    for (const auto& column : *columns) {
      size_t calls = 0, written = 0;
      for (const auto& event : storageLutWrites::events) {
        if (event.offset < column.begin || event.offset >= column.end) continue;
        ++calls;
        written += event.bytes;
        EXPECT_LE(event.bytes, column.end - event.offset) << column.name;
      }
      std::cout << "SECTION_LUT_BATCH column=" << column.name << " pages=" << pages
                << " writes=" << calls << " bytes=" << written
                << " budget=" << (pages + 31) / 32 << "\n";
      EXPECT_EQ(written, column.end - column.begin) << column.name;
      EXPECT_LE(calls, (pages + 31) / 32) << column.name;
    }
  }
  void checkLutPrefix(const std::string& partial, const std::string& complete) {
    const auto partialColumns = lutColumns(partial);
    const auto completeColumns = lutColumns(complete);
    ASSERT_TRUE(partialColumns.has_value());
    ASSERT_TRUE(completeColumns.has_value());
    for (size_t i = 0; i < partialColumns->size(); ++i) {
      const auto& prefix = (*partialColumns)[i];
      const auto& full = (*completeColumns)[i];
      ASSERT_LE(prefix.end - prefix.begin, full.end - full.begin);
      EXPECT_EQ(partial.substr(prefix.begin, prefix.end - prefix.begin),
                complete.substr(full.begin, prefix.end - prefix.begin)) << prefix.name;
    }
  }
  void armWrite(size_t offset) {
    storageFault::target = "/sections/0.bin.part";
    storageFault::writeOffset = offset;
    storageFault::failOnce = true;
  }
  void writeCache(const std::string& raw) {
    std::ofstream output(cache(), std::ios::binary | std::ios::trunc);
    output.write(raw.data(), static_cast<std::streamsize>(raw.size()));
    ASSERT_TRUE(output.good());
  }
  std::string makePartial(unsigned ticks) {
    std::filesystem::remove(cache());
    Section section(epub, 0, renderer);
    if (!section.startBuild(spec)) return {};
    for (unsigned tick = 0; tick < ticks; ++tick) {
      if (!section.buildSomeMore(1) || !section.isBuilding()) return {};
    }
    section.suspendBuild();
    if (!section.isPartial()) return {};
    return bytes(cache());
  }
  std::string makeDenseColdCache() {
    epub->contents = "<html><body>";
    for (unsigned paragraph = 0; paragraph < 420; ++paragraph) {
      epub->contents += "<p>";
      for (unsigned word = 0; word < 65; ++word) {
        epub->contents += "word" + std::to_string(paragraph) + "item" + std::to_string(word) + " ";
      }
      epub->contents += "</p>";
    }
    epub->contents += "</body></html>";
    spec.viewportWidth = 480;
    spec.viewportHeight = 320;
    std::filesystem::remove_all(root / "html");
    std::filesystem::remove(cache());
    Section cold(epub, 0, renderer);
    if (!cold.createSectionFile(spec) || !cold.isBuildComplete() || cold.pageCount <= 128) {
      ADD_FAILURE() << "dense cold cache did not produce more than 128 pages";
      return {};
    }
    std::cout << "SECTION_PREFIX_DENSE cold_pages=" << cold.pageCount << " cache_bytes=" << bytes(cache()).size() << "\n";
    return bytes(cache());
  }
  std::string makeDensePartial(uint16_t target) {
    std::filesystem::remove(cache());
    std::filesystem::remove(cache().string() + ".part");
    std::filesystem::remove(cache().string() + ".lut.part");
    Section partial(epub, 0, renderer);
    if (!partial.startBuild(spec)) { ADD_FAILURE() << "dense partial did not start"; return {}; }
    unsigned ticks = 0;
    while (partial.pageCount < target && partial.isBuilding()) {
      if (!partial.buildSomeMore(2) || ++ticks >= 1000) {
        ADD_FAILURE() << "dense partial did not reach page " << target;
        return {};
      }
    }
    if (!partial.isBuilding()) { ADD_FAILURE() << "dense fixture completed before page " << target; return {}; }
    partial.suspendBuild();
    if (!partial.isPartial()) { ADD_FAILURE() << "dense partial was not committed"; return {}; }
    return bytes(cache());
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
  void verifyPrologResume(const std::string& prolog, bool expectCheckpoint, bool defaultAnchor,
                          bool internalEntity, bool blockEntity = false,
                          const std::string& ancestorAttribute = "") {
    const auto signaturePath = root / "prolog-page-signature.bin";
    std::vector<std::string> anchors = {
        defaultAnchor ? "default-anchor" : "chapter-start", "midpoint", "chapter-end"};
    if (blockEntity) anchors.push_back("entity-anchor");
    epub->contents = prolog + "<html><body><section" +
                     (defaultAnchor ? std::string() : " id=\"chapter-start\"") + ancestorAttribute + ">";
    if (blockEntity) epub->contents += "&blocks;";
    const size_t blockEntitySourceOffset = epub->contents.find("&blocks;");
    for (unsigned index = 0; index < 160; ++index) {
      const std::string id = index == 70 ? " id=\"midpoint\"" :
                             index == 159 ? " id=\"chapter-end\"" : "";
      epub->contents += "<p" + id + ">paragraph " + std::to_string(index) +
                        " carries " + (internalEntity ? "&reader;" : "ordinary") +
                        " text across the chapter.</p>";
    }
    epub->contents += "</section></body></html>";
    std::filesystem::remove(cache());
    std::filesystem::remove(root / "html/0.html");

    struct Snapshot {
      bool valid = true;
      std::vector<std::string> pages;
      std::vector<uint32_t> offsets;
    };
    const auto capture = [&](Section& section, uint16_t count) {
      Snapshot snapshot;
      for (uint16_t index = 0; index < count; ++index) {
        const auto page = section.loadPage(index);
        if (!page) {
          ADD_FAILURE() << "missing page " << index;
          snapshot.valid = false;
          break;
        }
        std::string signature;
        if (!serializedPageSignature(*page, signaturePath, signature)) {
          ADD_FAILURE() << "cannot serialize page " << index;
          snapshot.valid = false;
          break;
        }
        const auto offset = section.getVisibleTextOffsetForPage(index);
        if (!offset) {
          ADD_FAILURE() << "missing visible offset for page " << index;
          snapshot.valid = false;
          break;
        }
        snapshot.pages.push_back(std::move(signature));
        snapshot.offsets.push_back(*offset);
      }
      return snapshot;
    };

    Snapshot cold;
    std::vector<std::optional<uint16_t>> coldAnchors;
    uint16_t completePages = 0;
    bool entityRendered = false;
    storageMetrics::begin();
    {
      Section section(epub, 0, renderer);
      ASSERT_TRUE(section.createSectionFile(spec));
      ASSERT_TRUE(section.isBuildComplete());
      completePages = section.pageCount;
      ASSERT_GT(completePages, 100u);
      cold = capture(section, completePages);
      ASSERT_TRUE(cold.valid);
      for (const auto& anchor : anchors) coldAnchors.push_back(section.findAnchor(anchor));
      if (internalEntity) {
        for (uint16_t pageIndex = 0; pageIndex < completePages; ++pageIndex) {
          const auto page = section.loadPage(pageIndex);
          ASSERT_NE(page, nullptr);
          for (const auto& element : page->elements) {
            if (element->getTag() != TAG_PageLine) continue;
            const auto* block = static_cast<const PageLine&>(*element).getBlock();
            if (!block) continue;
            for (uint16_t word = 0; word < block->wordCount(); ++word) {
              entityRendered |= std::string(block->wordText(word)) == "entityword";
            }
          }
        }
      }
    }
    const uint64_t coldHtmlBytes = storageMetrics::htmlReadBytes;
    storageMetrics::enabled = false;
    ASSERT_GT(coldHtmlBytes, 0u);
    if (internalEntity) ASSERT_TRUE(entityRendered);
    for (size_t index = 0; index < anchors.size(); ++index) {
      ASSERT_TRUE(coldAnchors[index].has_value()) << anchors[index];
    }

    std::filesystem::remove(cache());
    uint16_t watermark = 0;
    for (uint16_t target : {uint16_t(25), uint16_t(70)}) {
      Section section(epub, 0, renderer);
      if (watermark != 0) {
        ASSERT_TRUE(section.loadSectionFile(spec));
        ASSERT_TRUE(section.isPartial());
        ASSERT_EQ(section.pageCount, watermark);
      }
      ASSERT_TRUE(section.startBuild(spec));
      unsigned ticks = 0;
      while (section.isBuilding() && section.pageCount <= target) {
        ASSERT_TRUE(section.buildSomeMore(2));
        ASSERT_LT(++ticks, 2000u);
      }
      ASSERT_TRUE(section.isBuilding());
      ASSERT_GT(section.pageCount, watermark);
      ASSERT_LT(section.pageCount, completePages);
      section.suspendBuild();
      ASSERT_TRUE(section.isPartial());
      watermark = section.pageCount;
      const auto raw = bytes(cache());
      const auto offset = checkpointOffset(raw);
      ASSERT_TRUE(offset.has_value());
      if (blockEntity && target == 25 && raw.size() >= *offset + 12u) {
        const uint32_t sourceOffset = pod<uint32_t>(raw, *offset + 16u);
        std::cout << "SECTION_ENTITY watermark=" << watermark << " source_offset=" << sourceOffset
                  << " reference_offset=" << blockEntitySourceOffset << "\n";
      }
      if (blockEntity && target == 25 && watermark < 40u) {
        EXPECT_EQ(raw.size(), *offset) << "unsafe checkpoint inside markup entity";
      } else if (expectCheckpoint) {
        EXPECT_GE(raw.size(), *offset + 12u) << "watermark=" << watermark;
        if (raw.size() >= *offset + 12u) EXPECT_EQ(pod<uint32_t>(raw, *offset), 0x43504b31u);
      } else {
        EXPECT_EQ(raw.size(), *offset) << "watermark=" << watermark;
      }
      const Snapshot prefix = capture(section, watermark);
      ASSERT_TRUE(prefix.valid);
      const auto pageMismatch = std::mismatch(prefix.pages.begin(), prefix.pages.end(), cold.pages.begin());
      ASSERT_EQ(pageMismatch.first, prefix.pages.end())
          << "first mismatched page=" << std::distance(prefix.pages.begin(), pageMismatch.first);
      ASSERT_TRUE(std::equal(prefix.offsets.begin(), prefix.offsets.end(), cold.offsets.begin()));
    }

    Section final(epub, 0, renderer);
    ASSERT_TRUE(final.loadSectionFile(spec));
    ASSERT_TRUE(final.isPartial());
    ASSERT_EQ(final.pageCount, watermark);
    storageMetrics::begin();
    ASSERT_TRUE(final.startBuild(spec));
    ASSERT_TRUE(final.buildSomeMore(0));
    const uint64_t resumedHtmlBytes = storageMetrics::htmlReadBytes;
    storageMetrics::enabled = false;
    ASSERT_TRUE(final.isBuildComplete());
    ASSERT_EQ(final.pageCount, completePages);
    const Snapshot resumed = capture(final, completePages);
    ASSERT_TRUE(resumed.valid);
    EXPECT_EQ(resumed.pages, cold.pages);
    EXPECT_EQ(resumed.offsets, cold.offsets);
    for (size_t index = 0; index < anchors.size(); ++index) {
      EXPECT_EQ(final.findAnchor(anchors[index]), coldAnchors[index]) << anchors[index];
    }
    if (expectCheckpoint) {
      EXPECT_LT(resumedHtmlBytes, coldHtmlBytes)
          << "resuming reread the full HTML prefix at watermark " << watermark;
    }
    std::cout << "SECTION_PROLOG checkpoint=" << expectCheckpoint << " watermark=" << watermark
              << " cold_html_bytes=" << coldHtmlBytes << " resumed_html_bytes=" << resumedHtmlBytes << "\n";
  }
};
TEST_F(SectionCacheTest, FullProductionRoundTripPreservesAllBookWords) { retryAndCheckEveryWord(); }
TEST_F(SectionCacheTest, RasterOnlyChangesReuseSectionCacheAndPageOffsets) {
  for (int level = 0; level < 6; ++level) for (bool aa : {false, true}) {
    Section reopened(epub, 0, renderer);
    ASSERT_TRUE(reopened.loadSectionFile(spec)) << level << ' ' << aa;
    EXPECT_EQ(reopened.pageCount, fullPages);
    EXPECT_EQ(bytes(cache()), valid);
    ASSERT_NE(reopened.loadPage(3), nullptr);
    for (uint16_t index = 0; index < fullPages; ++index)
      EXPECT_EQ(pod<uint32_t>(bytes(cache()), lutOffset + index * 4), pageOffsets[index]);
  }
}
TEST_F(SectionCacheTest, StarvedHeapParksBuildInsteadOfAbandoning) {
  std::filesystem::remove(cache());
  Section section(epub, 0, renderer);
  ASSERT_TRUE(section.startBuild(spec));
  ASSERT_TRUE(section.buildSomeMore(1));
  ASSERT_TRUE(section.isBuilding());
  const auto pagesBefore = section.pageCount;
  ESP.maxAlloc = 4096;
  EXPECT_FALSE(section.buildSomeMore(1)) << "starved step must not parse";
  EXPECT_TRUE(section.buildStarved());
  EXPECT_TRUE(section.isBuilding()) << "starved build must stay resumable";
  EXPECT_EQ(section.pageCount, pagesBefore);
  ESP.maxAlloc = UINT32_MAX;
  unsigned ticks = 0;
  while (section.isBuilding() && !section.isBuildComplete()) {
    ASSERT_TRUE(section.buildSomeMore(2));
    ASSERT_LT(++ticks, 1000u);
  }
  EXPECT_FALSE(section.buildStarved());
  EXPECT_TRUE(section.isBuildComplete());
  EXPECT_GT(section.pageCount, pagesBefore);
  ESP.freeHeap = 12 * 1024;
  Section again(epub, 0, renderer);
  std::filesystem::remove(cache());
  ASSERT_TRUE(again.startBuild(spec));
  EXPECT_FALSE(again.buildSomeMore(1)) << "low free heap must starve too";
  EXPECT_TRUE(again.buildStarved());
  ESP = {};
}
TEST_F(SectionCacheTest, FinalCommitBatchesSerializedLutColumns) {
  ASSERT_GT(fullPages, 128u);
  Section section(epub, 0, renderer);
  ASSERT_TRUE(section.startBuild(spec));
  storageLutWrites::begin();
  const bool complete = section.buildSomeMore(0);
  storageLutWrites::enabled = false;
  ASSERT_TRUE(complete);
  ASSERT_TRUE(section.isBuildComplete());
  const std::string final = bytes(cache());
  EXPECT_EQ(final, valid);
  checkLutWriteBudget(final);
}
// The X3 card has one sector cache. Each page's index entry went to the index file as the page was
// written to the section file, so every page evicted the other file's sector and read it back: two
// sector writes and two reads a page (r40, 15 ms a page). The entries now go a block at a time, and
// a page whose entry is still waiting reads as any other.
TEST_F(SectionCacheTest, PageIndexEntriesGoToTheirFileABlockAtATime) {
  ASSERT_GT(fullPages, 128u);
  std::filesystem::remove(cache());
  Section section(epub, 0, renderer);
  ASSERT_TRUE(section.startBuild(spec));
  storageMetrics::begin();
  unsigned ticks = 0;
  while (section.pageCount < 40u && section.isBuilding()) {
    ASSERT_TRUE(section.buildSomeMore(1));
    ASSERT_LT(++ticks, 1000u);
  }
  ASSERT_TRUE(section.isBuilding());
  const uint16_t built = section.pageCount;
  for (const uint16_t page : {uint16_t(0), uint16_t(31), uint16_t(32), uint16_t(built - 1)}) {
    SCOPED_TRACE(page);
    ASSERT_NE(section.loadPage(page), nullptr);
    EXPECT_EQ(section.getVisibleTextOffsetForPage(page), pod<uint32_t>(valid, pod<uint32_t>(valid, 40) + 4 * page));
  }
  while (section.isBuilding() && !section.isBuildComplete()) {
    ASSERT_TRUE(section.buildSomeMore(2));
    ASSERT_LT(++ticks, 2000u);
  }
  const uint64_t lutWrites = storageMetrics::lutWriteCalls;
  storageMetrics::enabled = false;
  ASSERT_TRUE(section.isBuildComplete());
  EXPECT_EQ(bytes(cache()), valid);
  std::cout << "SECTION_LUT pages=" << fullPages << " lut_writes=" << lutWrites << "\n";
  EXPECT_LE(lutWrites, fullPages / 32u + 2u) << "page index entries still go one page at a time";
}
TEST_F(SectionCacheTest, PartialSuspendBatchesSerializedLutColumnsAndResumes) {
  ASSERT_GT(fullPages, 130u);
  std::filesystem::remove(cache());
  Section section(epub, 0, renderer);
  ASSERT_TRUE(section.startBuild(spec));
  unsigned ticks = 0;
  while (section.pageCount < 130u && section.isBuilding()) {
    ASSERT_TRUE(section.buildSomeMore(2));
    ASSERT_LT(++ticks, 1000u);
  }
  ASSERT_TRUE(section.isBuilding());
  storageLutWrites::begin();
  section.suspendBuild();
  storageLutWrites::enabled = false;
  ASSERT_TRUE(section.isPartial());
  const std::string partial = bytes(cache());
  checkLutPrefix(partial, valid);
  checkLutWriteBudget(partial);

  Section resumed(epub, 0, renderer);
  ASSERT_TRUE(resumed.loadSectionFile(spec));
  ASSERT_TRUE(resumed.isPartial());
  ASSERT_TRUE(resumed.startBuild(spec));
  ASSERT_TRUE(resumed.buildSomeMore(0));
  ASSERT_TRUE(resumed.isBuildComplete());
  EXPECT_EQ(bytes(cache()), valid);
  for (const uint16_t page : {uint16_t(0), uint16_t(64), uint16_t(129), uint16_t(fullPages - 1)}) {
    ASSERT_NE(resumed.loadPage(page), nullptr);
    EXPECT_EQ(resumed.getVisibleTextOffsetForPage(page),
              pod<uint32_t>(valid, pod<uint32_t>(valid, 40) + 4 * page));
  }
}
TEST_F(SectionCacheTest, LutBatchShortWritesAtObservedPositionsKeepPreviousCache) {
  ASSERT_GT(fullPages, 128u);
  Section measured(epub, 0, renderer);
  ASSERT_TRUE(measured.startBuild(spec));
  storageLutWrites::begin();
  const bool complete = measured.buildSomeMore(0);
  storageLutWrites::enabled = false;
  ASSERT_TRUE(complete);
  ASSERT_EQ(bytes(cache()), valid);
  const auto columns = lutColumns(valid);
  ASSERT_TRUE(columns.has_value());
  std::vector<size_t> offsets;
  for (const auto& event : storageLutWrites::events) {
    for (const auto& column : *columns) {
      if (event.offset >= column.begin && event.offset < column.end) {
        offsets.push_back(event.offset);
        break;
      }
    }
  }
  ASSERT_GE(offsets.size(), 3u);
  for (const size_t index : {size_t(0), offsets.size() / 2, offsets.size() - 1}) {
    SCOPED_TRACE(index);
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.startBuild(spec));
    storageFault::target = "/sections/0.bin.part";
    storageFault::writeOffset = offsets[index];
    storageFault::writeEndOffset = offsets[index] + 1;
    storageFault::failOnce = true;
    EXPECT_FALSE(section.buildSomeMore(0));
    EXPECT_EQ(storageFault::hit, 1u);
    storageFault::reset();
    EXPECT_EQ(bytes(cache()), valid);
  }
}
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
    bool advanced = true;
    unsigned ticks = 0;
    while (section.isBuilding() && storageFault::hit == 0 && ticks++ < 1000u) {
      advanced = section.buildSomeMore(2);
    }
    EXPECT_FALSE(advanced);
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
    unsigned ticks = 0;
    while (section.pageCount <= 2u) {
      ASSERT_TRUE(section.buildSomeMore(2));
      ASSERT_LT(++ticks, 1000u);
    }
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
    bool advanced = true;
    unsigned ticks = 0;
    while (section.isBuilding() && storageFault::hit == 0 && ticks++ < 1000u) {
      advanced = section.buildSomeMore(2);
    }
    EXPECT_FALSE(advanced);
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
    unsigned ticks = 0;
    while (partial.pageCount <= 2u) {
      ASSERT_TRUE(partial.buildSomeMore(2));
      ASSERT_LT(++ticks, 1000u);
    }
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

TEST_F(SectionCacheTest, HeapDropSuspendsOnceRetainsWatermarkAndForegroundCrossesIt) {
  std::filesystem::remove(cache());
  std::filesystem::remove(root / "html/0.html");

  uint16_t oldWatermark = 0;
  std::string oldCache;
  uint32_t oldLastOffset = 0;
  std::string oldLastWord;
  {
    Section initial(epub, 0, renderer);
    ASSERT_TRUE(initial.startBuild(spec));
    unsigned ticks = 0;
    while (initial.pageCount <= 8u) {
      ASSERT_TRUE(initial.buildSomeMore(12));
      ASSERT_LT(++ticks, 1000u);
    }
    ASSERT_TRUE(initial.isBuilding());
    oldWatermark = initial.pageCount;
    std::cout << "SECTION_PARTIAL old_watermark=" << oldWatermark << "\n";
    ASSERT_GT(oldWatermark, 8u);
    initial.currentPage = oldWatermark - 1;
    const auto page = initial.loadPage(initial.currentPage);
    ASSERT_NE(page, nullptr);
    ASSERT_FALSE(page->elements.empty());
    const auto* line = static_cast<const PageLine*>(page->elements.front().get());
    ASSERT_NE(line->getBlock(), nullptr);
    oldLastWord = line->getBlock()->wordText(0);
    ASSERT_EQ(initial.getVisibleTextOffsetForPage(initial.currentPage).has_value(), true);
    oldLastOffset = *initial.getVisibleTextOffsetForPage(initial.currentPage);
    initial.suspendBuild();
    ASSERT_FALSE(initial.isBuilding());
    ASSERT_TRUE(initial.isPartial());
    ASSERT_EQ(initial.pageCount, oldWatermark);
  }
  oldCache = bytes(cache());
  ASSERT_FALSE(std::filesystem::exists(cache().string() + ".part"));
  ASSERT_FALSE(std::filesystem::exists(cache().string() + ".lut.part"));

  // A low-heap transition can suspend a resumed build before its first tick.
  // Reads queued around that transition must continue to use the retained cache.
  {
    Section resumed(epub, 0, renderer);
    ASSERT_TRUE(resumed.loadSectionFile(spec));
    ASSERT_TRUE(resumed.isPartial());
    ASSERT_EQ(resumed.pageCount, oldWatermark);
    resumed.currentPage = oldWatermark - 1;
    ASSERT_TRUE(resumed.startBuild(spec));
    ASSERT_TRUE(resumed.isBuilding());
    const auto previous = resumed.loadPage(oldWatermark - 1);
    ASSERT_NE(previous, nullptr);
    const auto next = resumed.loadPage(oldWatermark);
    EXPECT_EQ(next, nullptr);
    const auto previousAgain = resumed.loadPage(oldWatermark - 2);
    ASSERT_NE(previousAgain, nullptr);
    resumed.suspendBuild();
    ASSERT_FALSE(resumed.isBuilding());
    ASSERT_TRUE(resumed.isPartial());
    EXPECT_EQ(resumed.pageCount, oldWatermark);
    EXPECT_EQ(bytes(cache()), oldCache);
    EXPECT_EQ(resumed.currentPage, oldWatermark - 1);
    ASSERT_NE(resumed.loadPage(oldWatermark - 1), nullptr);
    ASSERT_EQ(resumed.getVisibleTextOffsetForPage(oldWatermark - 1), oldLastOffset);
    const auto retained = resumed.loadPage(oldWatermark - 1);
    const auto* retainedLine = static_cast<const PageLine*>(retained->elements.front().get());
    ASSERT_NE(retainedLine->getBlock(), nullptr);
    EXPECT_EQ(retainedLine->getBlock()->wordText(0), oldLastWord);
  }

  // Once the user explicitly asks for the page beyond the watermark, one rebuild may
  // advance it and leave that page readable after the same suspend path.
  uint16_t foregroundPage = oldWatermark;
  {
    Section foreground(epub, 0, renderer);
    ASSERT_TRUE(foreground.loadSectionFile(spec));
    foreground.currentPage = foregroundPage;
    ASSERT_TRUE(foreground.startBuild(spec));
    while (foreground.isBuilding() && foreground.currentPage >= foreground.pageCount) {
      ASSERT_TRUE(foreground.buildSomeMore(2));
    }
    ASSERT_GT(foreground.pageCount, foregroundPage);
    ASSERT_NE(foreground.loadPage(foregroundPage), nullptr);
    const bool foregroundCompleted = foreground.isBuildComplete();
    foreground.suspendBuild();
    ASSERT_FALSE(foreground.isBuilding());
    ASSERT_GT(foreground.pageCount, foregroundPage);
    if (foregroundCompleted) {
      ASSERT_FALSE(foreground.isPartial());
    } else {
      ASSERT_TRUE(foreground.isPartial());
    }
    ASSERT_NE(foreground.loadPage(foregroundPage), nullptr);
  }
  Section reopened(epub, 0, renderer);
  ASSERT_TRUE(reopened.loadSectionFile(spec));
  if (reopened.isPartial()) {
    EXPECT_GT(reopened.pageCount, foregroundPage);
  } else {
    EXPECT_EQ(reopened.pageCount, fullPages);
  }
  ASSERT_NE(reopened.loadPage(foregroundPage), nullptr);
}

TEST_F(SectionCacheTest, PartialWatermarkExtensionAvoidsReplayingTheColdPrefix) {
  std::filesystem::remove(cache());

  struct PartialFixture {
    uint16_t watermark = 0;
    std::string cacheBytes;
  };
  std::vector<PartialFixture> fixtures;
  for (const uint16_t targetWatermark : {uint16_t(70), uint16_t(130)}) {
    std::filesystem::remove(cache());
    std::filesystem::remove(cache().string() + ".part");
    std::filesystem::remove(cache().string() + ".lut.part");
    std::filesystem::remove(cache().string() + ".davbak");
    Section partial(epub, 0, renderer);
    ASSERT_TRUE(partial.startBuild(spec));
    unsigned ticks = 0;
    while (partial.pageCount < targetWatermark) {
      ASSERT_TRUE(partial.buildSomeMore(2));
      ASSERT_TRUE(partial.isBuilding());
      ASSERT_LT(++ticks, 1000u);
    }
    partial.suspendBuild();
    ASSERT_TRUE(partial.isPartial());
    ASSERT_GT(partial.pageCount, 0u);
    ASSERT_LT(partial.pageCount, fullPages);
    if (!fixtures.empty()) ASSERT_GT(partial.pageCount, fixtures.back().watermark);
    fixtures.push_back({partial.pageCount, bytes(cache())});
  }

  struct ExtensionSample {
    uint64_t elapsedUs = 0;
    uint64_t htmlReadBytes = 0;
    uint64_t htmlReadCalls = 0;
    uint64_t cacheReadBytes = 0;
    uint64_t cacheReadCalls = 0;
    uint64_t cacheWriteBytes = 0;
    uint64_t cacheSeekCalls = 0;
    uint16_t pages = 0;
    uint32_t visibleOffset = 0;
    std::string pageSignature;
  };

  const auto clearSectionFiles = [&]() {
    std::filesystem::remove(cache());
    std::filesystem::remove(cache().string() + ".part");
    std::filesystem::remove(cache().string() + ".lut.part");
    std::filesystem::remove(cache().string() + ".davbak");
  };
  const auto restorePartial = [&](const std::string& raw) {
    clearSectionFiles();
    std::ofstream output(cache(), std::ios::binary | std::ios::trunc);
    output.write(raw.data(), static_cast<std::streamsize>(raw.size()));
    output.close();
    ASSERT_TRUE(output.good());
  };
  const auto signature = [](const Page& page) {
    std::string result = std::to_string(page.visibleTextOffset);
    for (const auto& element : page.elements) {
      result += ":" + std::to_string(element->getTag()) + "@" + std::to_string(element->xPos) + "," +
                std::to_string(element->yPos);
      if (element->getTag() != TAG_PageLine) continue;
      const auto* block = static_cast<const PageLine&>(*element).getBlock();
      if (!block) continue;
      for (uint16_t word = 0; word < block->wordCount(); ++word) {
        result += ":";
        result += block->wordText(word);
      }
    }
    return result;
  };
  auto measure = [&](const PartialFixture* fixture, const uint16_t targetPage) -> ExtensionSample {
    if (fixture) {
      restorePartial(fixture->cacheBytes);
    } else {
      std::filesystem::remove(cache());
    }
    Section section(epub, 0, renderer);
    if (fixture) {
      if (!section.loadSectionFile(spec) || !section.isPartial() || section.pageCount != fixture->watermark) {
        ADD_FAILURE() << "partial cache did not reopen at the expected watermark";
        return {};
      }
    }
    section.currentPage = targetPage;
    storageMetrics::begin();
    const auto start = std::chrono::steady_clock::now();
    if (!section.startBuild(spec)) {
      ADD_FAILURE() << "extension build did not start";
      storageMetrics::enabled = false;
      return {};
    }
    unsigned ticks = 0;
    while (section.isBuilding() && section.currentPage >= section.pageCount) {
      if (!section.buildSomeMore(2)) {
        ADD_FAILURE() << "extension build failed";
        storageMetrics::enabled = false;
        return {};
      }
      if (++ticks >= 1000u) {
        ADD_FAILURE() << "extension build exceeded tick guard";
        storageMetrics::enabled = false;
        return {};
      }
    }
    if (section.pageCount <= targetPage) {
      ADD_FAILURE() << "extension did not cross the requested page watermark";
      storageMetrics::enabled = false;
      section.suspendBuild();
      return {};
    }
    const auto page = section.loadPage(targetPage);
    const auto end = std::chrono::steady_clock::now();
    if (!page) {
      ADD_FAILURE() << "target page was unavailable after crossing its watermark";
      storageMetrics::enabled = false;
      section.suspendBuild();
      return {};
    }
    ExtensionSample result{
      static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()),
      storageMetrics::htmlReadBytes, storageMetrics::htmlReadCalls,
      storageMetrics::cacheReadBytes, storageMetrics::cacheReadCalls,
      storageMetrics::cacheWriteBytes, storageMetrics::cacheSeekCalls,
      section.pageCount, page->visibleTextOffset, signature(*page)};
    storageMetrics::enabled = false;
    section.suspendBuild();
    return result;
  };

  struct Percentiles {
    uint64_t p50 = 0;
    uint64_t p95 = 0;
    uint64_t max = 0;
  };
  const auto summarize = [](const std::vector<ExtensionSample>& samples, const auto value) {
    std::vector<uint64_t> values;
    values.reserve(samples.size());
    for (const auto& sample : samples) values.push_back(value(sample));
    std::sort(values.begin(), values.end());
    const auto percentile = [&](const unsigned pct) {
      const size_t rank = (values.size() * pct + 99) / 100;
      return values[std::max<size_t>(rank, 1) - 1];
    };
    return Percentiles{percentile(50), percentile(95), values.back()};
  };
  const auto printMetric = [](const char* name, const Percentiles& value) {
    std::cout << " " << name << "_p50=" << value.p50 << " " << name << "_p95=" << value.p95
              << " " << name << "_max=" << value.max;
  };

  constexpr unsigned sampleCount = 20;
  for (const auto& fixture : fixtures) {
    std::vector<ExtensionSample> partialSamples;
    std::vector<ExtensionSample> coldSamples;
    partialSamples.reserve(sampleCount);
    coldSamples.reserve(sampleCount);
    for (unsigned sample = 0; sample < sampleCount; ++sample) {
      partialSamples.push_back(measure(&fixture, fixture.watermark));
      coldSamples.push_back(measure(nullptr, fixture.watermark));
      EXPECT_EQ(partialSamples.back().pageSignature, coldSamples.back().pageSignature);
      EXPECT_EQ(partialSamples.back().visibleOffset, coldSamples.back().visibleOffset);
    }

    const auto partialUs = summarize(partialSamples, [](const auto& sample) { return sample.elapsedUs; });
    const auto coldUs = summarize(coldSamples, [](const auto& sample) { return sample.elapsedUs; });
    const auto partialHtml = summarize(partialSamples, [](const auto& sample) { return sample.htmlReadBytes; });
    const auto coldHtml = summarize(coldSamples, [](const auto& sample) { return sample.htmlReadBytes; });
    const auto partialCacheRead = summarize(partialSamples, [](const auto& sample) { return sample.cacheReadBytes; });
    const auto coldCacheRead = summarize(coldSamples, [](const auto& sample) { return sample.cacheReadBytes; });
    const auto partialCacheWrite = summarize(partialSamples, [](const auto& sample) { return sample.cacheWriteBytes; });
    const auto coldCacheWrite = summarize(coldSamples, [](const auto& sample) { return sample.cacheWriteBytes; });
    const auto partialCacheSeek = summarize(partialSamples, [](const auto& sample) { return sample.cacheSeekCalls; });
    const auto coldCacheSeek = summarize(coldSamples, [](const auto& sample) { return sample.cacheSeekCalls; });

    std::cout << "SECTION_WATERMARK_BENCH mode=partial watermark=" << fixture.watermark
              << " target=" << fixture.watermark << " samples=" << sampleCount;
    printMetric("us", partialUs);
    printMetric("html_read_bytes", partialHtml);
    printMetric("cache_read_bytes", partialCacheRead);
    printMetric("cache_write_bytes", partialCacheWrite);
    printMetric("cache_seek_calls", partialCacheSeek);
    std::cout << "\n";
    std::cout << "SECTION_WATERMARK_BENCH mode=cold watermark=" << fixture.watermark
              << " target=" << fixture.watermark << " samples=" << sampleCount;
    printMetric("us", coldUs);
    printMetric("html_read_bytes", coldHtml);
    printMetric("cache_read_bytes", coldCacheRead);
    printMetric("cache_write_bytes", coldCacheWrite);
    printMetric("cache_seek_calls", coldCacheSeek);
    std::cout << "\n";

    EXPECT_LT(partialUs.p95, coldUs.p50)
      << "partial extension replayed the cold parser prefix at watermark " << fixture.watermark;
    if (fixture.watermark >= 120) {
      EXPECT_LT(partialHtml.p95, coldHtml.p50)
        << "partial extension reread the cold HTML prefix at watermark " << fixture.watermark;
    }
  }
}

TEST_F(SectionCacheTest, DenseCheckpointPrefixUsesBoundedCacheIoAndMatchesColdBytes) {
  const std::string cold = makeDenseColdCache();
  ASSERT_FALSE(cold.empty());
  constexpr size_t headerBytes = 44;
  for (const uint16_t target : {uint16_t(15), uint16_t(70), uint16_t(128)}) {
    SCOPED_TRACE(target);
    const std::string partial = makeDensePartial(target);
    ASSERT_FALSE(partial.empty());
    const uint16_t pages = pod<uint16_t>(partial, pageCountOffset);
    const size_t pageLut = pod<uint32_t>(partial, pageLutHeaderOffset);
    const auto checkpoint = checkpointOffset(partial);
    ASSERT_GE(pages, target);
    ASSERT_GT(pageLut, headerBytes);
    ASSERT_TRUE(checkpoint.has_value());
    ASSERT_EQ(pod<uint32_t>(partial, *checkpoint), 0x43504b31u);
    const size_t prefixBytes = pageLut - headerBytes;
    if (target == 128) ASSERT_GE(prefixBytes, 64u * 1024u);

    Section resumed(epub, 0, renderer);
    ASSERT_TRUE(resumed.loadSectionFile(spec));
    ASSERT_TRUE(resumed.isPartial());
    storageMetrics::begin();
    storageMetrics::watchPrefix(cache().string(), headerBytes, pageLut);
    const bool started = resumed.startBuild(spec);
    storageMetrics::enabled = false;
    ASSERT_TRUE(started);
    ASSERT_EQ(bytes(cache()), partial);
    const size_t bulkBudget = (prefixBytes + 2047) / 2048;
    std::cout << "SECTION_PREFIX_IO watermark=" << pages << " prefix_bytes=" << prefixBytes
              << " prefix_read_calls=" << storageMetrics::prefixReadCalls
              << " prefix_write_calls=" << storageMetrics::prefixWriteCalls
              << " cache_read_calls=" << storageMetrics::cacheReadCalls
              << " cache_write_calls=" << storageMetrics::cacheWriteCalls
              << " bulk_budget=" << bulkBudget
              << " metadata_read_calls=" << storageMetrics::cacheReadCalls - storageMetrics::prefixReadCalls
              << " metadata_write_calls=" << storageMetrics::cacheWriteCalls - storageMetrics::prefixWriteCalls << "\n";
    EXPECT_EQ(storageMetrics::prefixReadBytes, prefixBytes);
    EXPECT_EQ(storageMetrics::prefixWriteBytes, prefixBytes);
    EXPECT_LE(storageMetrics::prefixReadCalls, bulkBudget);
    EXPECT_LE(storageMetrics::prefixWriteCalls, bulkBudget);
    // The exact prefix range has zero overhead; other calls serve keys, checkpoint and LUTs.
    ASSERT_TRUE(resumed.buildSomeMore(0));
    ASSERT_TRUE(resumed.isBuildComplete());
    EXPECT_EQ(bytes(cache()), cold);
  }
}

TEST_F(SectionCacheTest, HeapBufferAllocationFailureUsesCheckpointFallback) {
  const std::string cold = makeDenseColdCache();
  ASSERT_FALSE(cold.empty());
  const std::string partial = makeDensePartial(128);
  ASSERT_FALSE(partial.empty());
  const size_t pageLut = pod<uint32_t>(partial, pageLutHeaderOffset);
  const auto checkpoint = checkpointOffset(partial);
  ASSERT_TRUE(checkpoint.has_value());
  ASSERT_EQ(pod<uint32_t>(partial, *checkpoint), 0x43504b31u);
  ASSERT_GE(pageLut - 44, 64u * 1024u);
  Section resumed(epub, 0, renderer);
  ASSERT_TRUE(resumed.loadSectionFile(spec));
  allocationProbe::rejectSize = 4096;
  allocationProbe::rejectCall = 1;
  allocationProbe::matchingCalls = allocationProbe::rejected = 0;
  storageMetrics::begin();
  storageMetrics::watchPrefix(cache().string(), 44, pageLut);
  const bool started = resumed.startBuild(spec);
  storageMetrics::enabled = false;
  allocationProbe::rejectSize = 0;
  ASSERT_TRUE(started);
  EXPECT_EQ(allocationProbe::rejected, 1u);
  EXPECT_EQ(storageMetrics::prefixReadBytes, pageLut - 44);
  EXPECT_EQ(storageMetrics::prefixWriteBytes, pageLut - 44);
  EXPECT_EQ(bytes(cache()), partial);
  ASSERT_TRUE(resumed.buildSomeMore(0));
  ASSERT_TRUE(resumed.isBuildComplete());
  EXPECT_EQ(bytes(cache()), cold);
}

TEST_F(SectionCacheTest, PrefixShortReadsAndWritesPreservePartialUntilExactRebuild) {
  const std::string cold = makeDenseColdCache();
  ASSERT_FALSE(cold.empty());
  const std::string partial = makeDensePartial(128);
  ASSERT_FALSE(partial.empty());
  constexpr size_t headerBytes = 44;
  const size_t pageLut = pod<uint32_t>(partial, pageLutHeaderOffset);
  const auto checkpoint = checkpointOffset(partial);
  ASSERT_TRUE(checkpoint.has_value());
  ASSERT_EQ(pod<uint32_t>(partial, *checkpoint), 0x43504b31u);
  ASSERT_GT(pageLut, headerBytes + 64u * 1024u);
  const size_t prefixBytes = pageLut - headerBytes;
  const size_t offsets[] = {headerBytes, headerBytes + (prefixBytes / 2 / 4096) * 4096,
                            headerBytes + ((prefixBytes - 1) / 4096) * 4096};
  for (const bool readFault : {true, false}) {
    for (unsigned position = 0; position < 3; ++position) {
      SCOPED_TRACE(readFault ? "short read" : "short write");
      SCOPED_TRACE(position);
      writeCache(partial);
      Section resumed(epub, 0, renderer);
      ASSERT_TRUE(resumed.loadSectionFile(spec));
      storageFault::reset();
      storageFault::target = readFault ? "/sections/0.bin" : "/sections/0.bin.part";
      const size_t zoneEnd = std::min(offsets[position] + 4096, pageLut);
      if (readFault) {
        storageFault::readOffset = offsets[position];
        storageFault::readEndOffset = zoneEnd;
        storageFault::shortReadOnce = true;
      } else {
        storageFault::writeOffset = offsets[position];
        storageFault::writeEndOffset = zoneEnd;
        storageFault::failOnce = true;
      }
      ASSERT_TRUE(resumed.startBuild(spec));
      EXPECT_EQ(storageFault::hit, 1u);
      EXPECT_GE(storageFault::hitOffset, offsets[position]);
      EXPECT_LT(storageFault::hitOffset, zoneEnd);
      EXPECT_LT(storageFault::hitOffset, pageLut);
      EXPECT_EQ(bytes(cache()), partial);
      storageFault::reset();
      ASSERT_TRUE(resumed.buildSomeMore(0));
      ASSERT_TRUE(resumed.isBuildComplete());
      EXPECT_EQ(bytes(cache()), cold);
    }
  }
}

TEST_F(SectionCacheTest, MissingOrDamagedCheckpointKeepsPartialReadableAndRebuilds) {
  const std::string saved = makePartial(1);
  ASSERT_FALSE(saved.empty());
  const auto offset = checkpointOffset(saved);
  ASSERT_TRUE(offset.has_value());
  ASSERT_GE(saved.size(), *offset + 12u) << "partial checkpoint extension was not written";
  ASSERT_EQ(pod<uint32_t>(saved, *offset), 0x43504b31u);
  const uint16_t watermark = pod<uint16_t>(saved, pageCountOffset);
  ASSERT_GT(watermark, 0u);

  std::vector<std::pair<std::string, std::string>> variants;
  variants.emplace_back("legacy", saved.substr(0, *offset));
  std::string badMagic = saved;
  badMagic[*offset] ^= 1;
  variants.emplace_back("magic", std::move(badMagic));
  std::string badCrc = saved;
  badCrc[*offset + 8] ^= 1;
  variants.emplace_back("crc", std::move(badCrc));
  std::string hugeLength = saved;
  const uint32_t invalidLength = UINT32_MAX;
  memcpy(hugeLength.data() + *offset + 4, &invalidLength, sizeof(invalidLength));
  variants.emplace_back("length", std::move(hugeLength));
  variants.emplace_back("truncated", saved.substr(0, saved.size() - 1));

  for (const auto& [name, raw] : variants) {
    SCOPED_TRACE(name);
    writeCache(raw);
    {
      Section reopened(epub, 0, renderer);
      ASSERT_TRUE(reopened.loadSectionFile(spec));
      ASSERT_TRUE(reopened.isPartial());
      ASSERT_EQ(reopened.pageCount, watermark);
      ASSERT_NE(reopened.loadPage(0), nullptr);
      ASSERT_NE(reopened.loadPage(watermark - 1), nullptr);
      ASSERT_TRUE(reopened.getVisibleTextOffsetForPage(watermark - 1).has_value());
    }
    retryAndCheckEveryWord();
    EXPECT_EQ(bytes(cache()), valid);
  }
}

TEST_F(SectionCacheTest, CheckpointWriteSeekAndSyncFailuresKeepOldPartialReadable) {
  const std::string oldPartial = makePartial(1);
  ASSERT_FALSE(oldPartial.empty());
  const auto oldOffset = checkpointOffset(oldPartial);
  ASSERT_TRUE(oldOffset.has_value());
  ASSERT_GE(oldPartial.size(), *oldOffset + 12u);
  const uint16_t watermark = pod<uint16_t>(oldPartial, pageCountOffset);

  writeCache(oldPartial);
  unsigned extensionTicks = 0;
  {
    Section reference(epub, 0, renderer);
    ASSERT_TRUE(reference.loadSectionFile(spec));
    ASSERT_TRUE(reference.startBuild(spec));
    while (reference.isBuilding() && reference.pageCount <= watermark) {
      ASSERT_TRUE(reference.buildSomeMore(2));
      ASSERT_LT(++extensionTicks, 1000u);
    }
    ASSERT_TRUE(reference.isBuilding());
    reference.suspendBuild();
    ASSERT_TRUE(reference.isPartial());
    ASSERT_GT(reference.pageCount, watermark);
  }
  const auto newOffset = checkpointOffset(bytes(cache()));
  ASSERT_TRUE(newOffset.has_value());
  ASSERT_GT(*newOffset, *oldOffset);

  for (const std::string mode : {"write", "seek", "sync"}) {
    SCOPED_TRACE(mode);
    writeCache(oldPartial);
    {
      Section extending(epub, 0, renderer);
      ASSERT_TRUE(extending.loadSectionFile(spec));
      ASSERT_TRUE(extending.startBuild(spec));
      for (unsigned tick = 0; tick < extensionTicks; ++tick) {
        ASSERT_TRUE(extending.buildSomeMore(2));
      }
      ASSERT_TRUE(extending.isBuilding());
      ASSERT_GT(extending.pageCount, watermark);
      storageFault::target = "/sections/0.bin.part";
      if (mode == "write") {
        storageFault::writeOffset = *newOffset;
        storageFault::failOnce = true;
      } else if (mode == "seek") {
        storageFault::failSeek = true;
      } else {
        storageFault::failFlush = true;
      }
      extending.suspendBuild();
      EXPECT_EQ(storageFault::hit, 1u);
      storageFault::reset();
    }
    EXPECT_EQ(bytes(cache()), oldPartial);
    {
      Section reopened(epub, 0, renderer);
      ASSERT_TRUE(reopened.loadSectionFile(spec));
      ASSERT_TRUE(reopened.isPartial());
      ASSERT_EQ(reopened.pageCount, watermark);
      ASSERT_NE(reopened.loadPage(watermark - 1), nullptr);
    }
    retryAndCheckEveryWord();
  }
}

TEST_F(SectionCacheTest, CheckpointInFinalHtmlBufferResumesToColdOutput) {
  epub->contents = "<html><body>";
  for (unsigned index = 0; index < 45; ++index) {
    epub->contents += "<p>short" + std::to_string(index) + "</p>";
  }
  epub->contents += "</body></html>";
  ASSERT_LT(epub->contents.size(), 1024u);
  std::filesystem::remove(cache());
  std::filesystem::remove(root / "html/0.html");
  {
    Section cold(epub, 0, renderer);
    ASSERT_TRUE(cold.createSectionFile(spec));
    ASSERT_EQ(cold.pageCount, 45u);
  }
  const std::string coldBytes = bytes(cache());
  const std::string partial = makePartial(1);
  ASSERT_FALSE(partial.empty());
  const auto offset = checkpointOffset(partial);
  ASSERT_TRUE(offset.has_value());
  ASSERT_GE(partial.size(), *offset + 12u);
  ASSERT_EQ(pod<uint32_t>(partial, *offset), 0x43504b31u);
  {
    Section resumed(epub, 0, renderer);
    ASSERT_TRUE(resumed.loadSectionFile(spec));
    ASSERT_TRUE(resumed.isPartial());
    ASSERT_TRUE(resumed.startBuild(spec));
    ASSERT_TRUE(resumed.buildSomeMore(0));
    ASSERT_TRUE(resumed.isBuildComplete());
    ASSERT_EQ(resumed.pageCount, 45u);
  }
  EXPECT_EQ(bytes(cache()), coldBytes);
}

TEST_F(SectionCacheTest, DoctypePartialKeepsCheckpointAndReadablePrefix) {
  epub->contents = "<!DOCTYPE html><html><body>";
  for (unsigned index = 0; index < 120; ++index) {
    epub->contents += "<p>declared" + std::to_string(index) + "</p>";
  }
  epub->contents += "</body></html>";
  std::filesystem::remove(cache());
  std::filesystem::remove(root / "html/0.html");
  {
    Section cold(epub, 0, renderer);
    ASSERT_TRUE(cold.createSectionFile(spec));
    ASSERT_EQ(cold.pageCount, 120u);
  }
  const std::string coldBytes = bytes(cache());
  const std::string partial = makePartial(1);
  ASSERT_FALSE(partial.empty());
  const auto offset = checkpointOffset(partial);
  ASSERT_TRUE(offset.has_value());
  ASSERT_GE(partial.size(), *offset + 12u);
  ASSERT_EQ(pod<uint32_t>(partial, *offset), 0x43504b31u);
  const uint16_t watermark = pod<uint16_t>(partial, pageCountOffset);
  {
    Section resumed(epub, 0, renderer);
    ASSERT_TRUE(resumed.loadSectionFile(spec));
    ASSERT_TRUE(resumed.isPartial());
    ASSERT_EQ(resumed.pageCount, watermark);
    ASSERT_NE(resumed.loadPage(watermark - 1), nullptr);
    ASSERT_TRUE(resumed.startBuild(spec));
    ASSERT_TRUE(resumed.buildSomeMore(0));
    ASSERT_TRUE(resumed.isBuildComplete());
    ASSERT_EQ(resumed.pageCount, 120u);
  }
  EXPECT_EQ(bytes(cache()), coldBytes);
}

TEST_F(SectionCacheTest, ExternalDoctypePrologResumesWithoutRereadingHtmlPrefix) {
  const std::string prolog =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.1//EN\" "
      "\"http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd\">\n";
  ASSERT_LT(prolog.size() + 100u, 2048u);
  verifyPrologResume(prolog, true, false, false);
}

TEST_F(SectionCacheTest, InternalEntityAndDefaultAttributeResumeFromPrologCheckpoint) {
  const std::string prolog =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<!DOCTYPE html SYSTEM \"http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd\" ["
      "<!ENTITY reader \"entityword\">"
      "<!ATTLIST section id CDATA \"default-anchor\">]>";
  ASSERT_LT(prolog.size() + 100u, 2048u);
  verifyPrologResume(prolog, true, true, true);
}

TEST_F(SectionCacheTest, UsAsciiPrologWithNonAsciiAttributeReferenceMatchesColdAfterResume) {
  const std::string prolog = "<?xml version=\"1.0\" encoding=\"US-ASCII\"?>\n";
  verifyPrologResume(prolog, true, false, false, false, " title=\"&#x1EA1;\"");
}

TEST_F(SectionCacheTest, MarkupEntityBoundaryMatchesColdPagesAfterTwoResumes) {
  std::string replacement;
  for (unsigned index = 0; index < 40; ++index) {
    replacement += "<p";
    if (index == 0) replacement += " id='entity-anchor'";
    replacement += ">inside " + std::to_string(index) + "</p>";
  }
  const std::string prolog =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE html [<!ENTITY blocks \"" +
      replacement + "\">]>\n";
  ASSERT_LT(prolog.size() + 100u, 2048u);
  verifyPrologResume(prolog, true, false, false, true);
}

TEST_F(SectionCacheTest, OversizedPrologKeepsColdFallbackAndPageParity) {
  const std::string prolog =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE html>\n<!--" +
      std::string(2100, 'x') + "-->\n";
  ASSERT_GT(prolog.size(), 2048u);
  verifyPrologResume(prolog, false, false, false);
}

TEST_F(SectionCacheTest, UnsupportedEncodingKeepsColdFallbackAndPageParity) {
  const std::string prolog = "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n";
  verifyPrologResume(prolog, false, false, false);
}

TEST_F(SectionCacheTest, StructuredCheckpointResumeMatchesColdBuildAcrossThreeSuspends) {
  constexpr uint16_t segmentCount = 36;
  constexpr uint16_t listItemCount = segmentCount * 4;
  const auto signaturePath = root / "page-signature.bin";
  std::vector<std::string> anchors = {"chapter-start", "footnote-one", "chapter-end"};

  epub->contents =
      "<html><body><section id=\"chapter-start\"><div><blockquote>"
      "<p style=\"text-align: center; margin-left: 2px\">Alpha &amp; beta &lt; gamma &gt; delta &quot;quoted&quot; &apos;marked&apos;.</p>"
      "</blockquote></div>";
  for (uint16_t segment = 0; segment < segmentCount; ++segment) {
    const std::string id = "segment-" + std::to_string(segment);
    anchors.push_back(id);
    epub->contents += "<section id=\"" + id + "\"><article><div><blockquote>";
    epub->contents += "<p style=\"margin-left: 3px; text-indent: 2px\">Paragraph " +
                      std::to_string(segment) +
                      " carries enough words to wrap across lines and preserve nested container style.</p>";
    epub->contents += "<ol><li>ordered " + std::to_string(segment) +
                      " first</li><li>ordered " + std::to_string(segment) + " second</li></ol>";
    epub->contents += "<ul><li>outer " + std::to_string(segment) +
                      "<ul><li>nested " + std::to_string(segment) + "</li></ul></li></ul>";
    epub->contents += "<p>Ruby <ruby>base<rt>reading</rt></ruby> tail <a href=\"#footnote-one\" "
                      "style=\"vertical-align: super\">[1]</a>.</p>";
    if (segment % 6 == 0) {
      epub->contents += "<img src=\"fixture-" + std::to_string(segment) +
                        ".png\" alt=\"diagram " + std::to_string(segment) + "\"/>";
    }
    if (segment % 4 == 0) {
      epub->contents += "<table><tr><th>head " + std::to_string(segment) +
                        "</th><th>value</th></tr><tr><td>cell left</td><td>cell right</td></tr></table>";
    }
    epub->contents += "</blockquote></div></article></section>";
  }
  epub->contents +=
      "<aside id=\"footnote-one\"><p>Footnote target with internal navigation metadata.</p></aside>"
      "<p id=\"chapter-end\">The final paragraph closes the structured checkpoint fixture.</p>"
      "</section></body></html>";
  std::filesystem::remove(cache());
  std::filesystem::remove(root / "html/0.html");

  struct Snapshot {
    std::vector<std::string> pageSignatures;
    std::vector<uint32_t> visibleOffsets;
    std::vector<uint16_t> paragraphIndexes;
    std::vector<std::optional<uint16_t>> paragraphPages;
    std::vector<std::optional<uint16_t>> listItemPages;
    std::vector<std::optional<uint16_t>> anchorPages;
  };
  const auto capturePages = [&](Section& section, Snapshot& snapshot, const uint16_t count) {
    for (uint16_t pageIndex = 0; pageIndex < count; ++pageIndex) {
      const auto page = section.loadPage(pageIndex);
      EXPECT_NE(page, nullptr) << "page=" << pageIndex;
      if (!page) return false;
      std::string signature;
      EXPECT_TRUE(serializedPageSignature(*page, signaturePath, signature)) << "page=" << pageIndex;
      if (signature.empty()) return false;
      snapshot.pageSignatures.push_back(std::move(signature));
      const auto visibleOffset = section.getVisibleTextOffsetForPage(pageIndex);
      const auto paragraphIndex = section.getParagraphIndexForPage(pageIndex);
      EXPECT_TRUE(visibleOffset.has_value()) << "page=" << pageIndex;
      EXPECT_TRUE(paragraphIndex.has_value()) << "page=" << pageIndex;
      if (!visibleOffset || !paragraphIndex) return false;
      snapshot.visibleOffsets.push_back(*visibleOffset);
      snapshot.paragraphIndexes.push_back(*paragraphIndex);
    }
    return true;
  };
  const auto captureComplete = [&](Section& section, Snapshot& snapshot) {
    if (!capturePages(section, snapshot, section.pageCount)) return false;
    const uint16_t maxParagraphIndex =
        snapshot.paragraphIndexes.empty()
            ? 0
            : *std::max_element(snapshot.paragraphIndexes.begin(), snapshot.paragraphIndexes.end());
    for (uint32_t index = 0; index <= static_cast<uint32_t>(maxParagraphIndex) + 2; ++index) {
      snapshot.paragraphPages.push_back(section.getPageForParagraphIndex(static_cast<uint16_t>(index)));
    }
    for (uint32_t index = 0; index <= static_cast<uint32_t>(listItemCount) + 2; ++index) {
      snapshot.listItemPages.push_back(section.getPageForListItemIndex(static_cast<uint16_t>(index)));
    }
    for (const auto& anchor : anchors) snapshot.anchorPages.push_back(section.findAnchor(anchor));
    return true;
  };

  Snapshot cold;
  uint16_t completePageCount = 0;
  {
    Section reference(epub, 0, renderer);
    ASSERT_TRUE(reference.createSectionFile(spec));
    ASSERT_TRUE(reference.isBuildComplete());
    completePageCount = reference.pageCount;
    ASSERT_GT(completePageCount, 20u);
    ASSERT_TRUE(captureComplete(reference, cold));
  }
  ASSERT_EQ(cold.pageSignatures.size(), completePageCount);
  ASSERT_EQ(cold.visibleOffsets.size(), completePageCount);
  ASSERT_EQ(cold.paragraphIndexes.size(), completePageCount);
  for (size_t index = 0; index < anchors.size(); ++index) {
    ASSERT_TRUE(cold.anchorPages[index].has_value()) << anchors[index];
  }

  std::filesystem::remove(cache());
  uint16_t previousWatermark = 0;
  for (unsigned cycle = 0; cycle < 3; ++cycle) {
    Section partial(epub, 0, renderer);
    if (cycle > 0) {
      ASSERT_TRUE(partial.loadSectionFile(spec));
      ASSERT_TRUE(partial.isPartial());
      ASSERT_EQ(partial.pageCount, previousWatermark);
    }
    const uint16_t remaining = completePageCount - previousWatermark;
    ASSERT_GT(remaining, 6u) << "fixture completed before all suspend cycles";
    const uint16_t advance = std::max<uint16_t>(2, std::min<uint16_t>(6, remaining / 4));
    const uint16_t target = previousWatermark + advance;
    partial.currentPage = target;
    ASSERT_TRUE(partial.startBuild(spec));
    unsigned ticks = 0;
    while (partial.isBuilding() && partial.pageCount <= target) {
      ASSERT_TRUE(partial.buildSomeMore(2));
      ASSERT_LT(++ticks, 2000u);
    }
    ASSERT_TRUE(partial.isBuilding()) << "fixture completed before suspend cycle " << cycle;
    ASSERT_GT(partial.pageCount, previousWatermark);
    ASSERT_LT(partial.pageCount, completePageCount);
    partial.suspendBuild();
    ASSERT_TRUE(partial.isPartial());
    ASSERT_FALSE(partial.isBuilding());
    previousWatermark = partial.pageCount;

    Snapshot prefix;
    ASSERT_TRUE(capturePages(partial, prefix, previousWatermark));
    ASSERT_TRUE(std::equal(prefix.pageSignatures.begin(), prefix.pageSignatures.end(), cold.pageSignatures.begin()));
    ASSERT_TRUE(std::equal(prefix.visibleOffsets.begin(), prefix.visibleOffsets.end(), cold.visibleOffsets.begin()));
    ASSERT_TRUE(std::equal(prefix.paragraphIndexes.begin(), prefix.paragraphIndexes.end(),
                           cold.paragraphIndexes.begin()));
    std::cout << "SECTION_CHECKPOINT_EQ cycle=" << cycle + 1 << " watermark=" << previousWatermark
              << " complete_pages=" << completePageCount << "\n";
  }

  Snapshot resumed;
  {
    Section final(epub, 0, renderer);
    ASSERT_TRUE(final.loadSectionFile(spec));
    ASSERT_TRUE(final.isPartial());
    ASSERT_EQ(final.pageCount, previousWatermark);
    ASSERT_TRUE(final.startBuild(spec));
    ASSERT_TRUE(final.buildSomeMore(0));
    ASSERT_TRUE(final.isBuildComplete());
    ASSERT_FALSE(final.isPartial());
    ASSERT_EQ(final.pageCount, completePageCount);
    ASSERT_TRUE(captureComplete(final, resumed));
  }
  EXPECT_EQ(resumed.pageSignatures, cold.pageSignatures);
  EXPECT_EQ(resumed.visibleOffsets, cold.visibleOffsets);
  EXPECT_EQ(resumed.paragraphIndexes, cold.paragraphIndexes);
  EXPECT_EQ(resumed.paragraphPages, cold.paragraphPages);
  EXPECT_EQ(resumed.listItemPages, cold.listItemPages);
  EXPECT_EQ(resumed.anchorPages, cold.anchorPages);
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

TEST_F(SectionCacheTest, ParserParkingKeepsBuildActiveAndPreservesExactOutputAcrossCycles) {
  epub->contents = "<html><body><section id=\"chapter-start\">";
  for (unsigned index = 0; index < 260; ++index) {
    const std::string id = index == 130 ? " id=\"chapter-middle\"" :
                           index == 259 ? " id=\"chapter-end\"" : "";
    epub->contents += "<p" + id + ">parking paragraph " + std::to_string(index) +
                      " carries enough words for an exact parser resume comparison.</p>";
  }
  epub->contents += "</section></body></html>";
  std::filesystem::remove(cache());
  std::filesystem::remove(root / "html/0.html");
  const auto signaturePath = root / "parking-page-signature.bin";
  std::vector<std::string> coldPages;
  std::vector<uint32_t> coldOffsets;
  std::array<std::optional<uint16_t>, 3> coldAnchors;
  uint16_t completePages = 0;
  {
    Section cold(epub, 0, renderer);
    ASSERT_TRUE(cold.createSectionFile(spec));
    ASSERT_TRUE(cold.isBuildComplete());
    completePages = cold.pageCount;
    ASSERT_GT(completePages, 80u);
    for (uint16_t page = 0; page < completePages; ++page) {
      const auto loaded = cold.loadPage(page);
      ASSERT_NE(loaded, nullptr);
      std::string signature;
      ASSERT_TRUE(serializedPageSignature(*loaded, signaturePath, signature));
      coldPages.push_back(std::move(signature));
      const auto offset = cold.getVisibleTextOffsetForPage(page);
      ASSERT_TRUE(offset.has_value());
      coldOffsets.push_back(*offset);
    }
    coldAnchors = {cold.findAnchor("chapter-start"), cold.findAnchor("chapter-middle"),
                   cold.findAnchor("chapter-end")};
  }
  for (const auto& anchor : coldAnchors) ASSERT_TRUE(anchor.has_value());
  const std::string committed = bytes(cache());
  std::filesystem::remove(cache());

  Section section(epub, 0, renderer);
  ASSERT_TRUE(section.startBuild(spec));
  while (section.pageCount < 8u && section.isBuilding()) ASSERT_TRUE(section.buildSomeMore(2));
  ASSERT_TRUE(section.isBuilding());
  ASSERT_TRUE(parkSection(section));
  ASSERT_TRUE(section.isBuilding());
  ASSERT_TRUE(sectionIsParked(section));
  ASSERT_FALSE(std::filesystem::exists(cache()));
  EXPECT_EQ(section.findAnchor("chapter-start"), coldAnchors[0]);
  EXPECT_FALSE(section.findAnchor("chapter-middle").has_value());
  EXPECT_FALSE(section.findAnchor("chapter-end").has_value());
  const uint16_t firstWatermark = section.pageCount;
  ASSERT_TRUE(parkSection(section));
  EXPECT_TRUE(sectionIsParked(section));
  EXPECT_EQ(section.pageCount, firstWatermark);

  const std::string stagingPath = cache().string() + ".part";
  ASSERT_NE(section.loadPage(firstWatermark - 1), nullptr);
  const std::string protectedPrefix = bytes(stagingPath);
  ASSERT_FALSE(protectedPrefix.empty());
  storageMetrics::begin();
  storageMetrics::watchPrefix(cache().string(), 0, protectedPrefix.size());
  for (unsigned cycle = 0; cycle < 6; ++cycle) {
    const uint16_t before = section.pageCount;
    ASSERT_TRUE(resumeSection(section, spec, 2));
    ASSERT_TRUE(section.isBuilding());
    ASSERT_FALSE(sectionIsParked(section));
    ASSERT_GT(section.pageCount, before);
    ASSERT_LT(section.pageCount, completePages);
    ASSERT_TRUE(parkSection(section));
    ASSERT_TRUE(section.isBuilding());
    ASSERT_TRUE(sectionIsParked(section));
    const uint16_t sample = section.pageCount - 1;
    const auto page = section.loadPage(sample);
    ASSERT_NE(page, nullptr);
    std::string signature;
    ASSERT_TRUE(serializedPageSignature(*page, signaturePath, signature));
    EXPECT_EQ(signature, coldPages[sample]);
    EXPECT_EQ(section.getVisibleTextOffsetForPage(sample), coldOffsets[sample]);
    EXPECT_EQ(bytes(stagingPath).substr(0, protectedPrefix.size()), protectedPrefix);
    EXPECT_FALSE(std::filesystem::exists(cache()));
  }
  storageMetrics::enabled = false;
  EXPECT_EQ(storageMetrics::prefixReadBytes, 0u);
  EXPECT_EQ(storageMetrics::prefixWriteBytes, 0u);
  EXPECT_EQ(storageMetrics::renameCalls, 0u);

  ASSERT_TRUE(resumeSection(section, spec, 0));
  ASSERT_TRUE(section.isBuildComplete());
  ASSERT_FALSE(section.isBuilding());
  ASSERT_FALSE(sectionIsParked(section));
  ASSERT_EQ(section.pageCount, completePages);
  EXPECT_EQ(bytes(cache()), committed);
  for (uint16_t page = 0; page < completePages; ++page) {
    const auto loaded = section.loadPage(page);
    ASSERT_NE(loaded, nullptr);
    std::string signature;
    ASSERT_TRUE(serializedPageSignature(*loaded, signaturePath, signature));
    EXPECT_EQ(signature, coldPages[page]) << page;
    EXPECT_EQ(section.getVisibleTextOffsetForPage(page), coldOffsets[page]) << page;
  }
  EXPECT_EQ(section.findAnchor("chapter-start"), coldAnchors[0]);
  EXPECT_EQ(section.findAnchor("chapter-middle"), coldAnchors[1]);
  EXPECT_EQ(section.findAnchor("chapter-end"), coldAnchors[2]);
}

TEST_F(SectionCacheTest, ParkedAnchorStorageSurvivesResumeAndSecondPark) {
  epub->contents = "<html><body><section id=\"chapter-start\">";
  for (unsigned index = 0; index < 220; ++index) {
    const std::string id = index == 110 ? " id=\"chapter-middle\"" :
                           index == 219 ? " id=\"chapter-end\"" : "";
    epub->contents += "<p" + id + ">anchor lifetime paragraph " + std::to_string(index) +
                      " carries enough text for a parser checkpoint and resume.</p>";
  }
  epub->contents += "</section></body></html>";
  std::filesystem::remove(cache());
  std::filesystem::remove(root / "html/0.html");

  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.startBuild(spec));
    while (section.pageCount < 8u) {
      ASSERT_TRUE(section.buildSomeMore(2));
      ASSERT_TRUE(section.isBuilding());
    }
    ASSERT_TRUE(section.parkBuild());
    ASSERT_TRUE(section.isBuildParked());
    const void* parkedData = section.parkedAnchorsDataForTest();
    ASSERT_NE(parkedData, nullptr);

    ASSERT_TRUE(section.buildSomeMore(2));
    ASSERT_TRUE(section.isBuilding());
    EXPECT_EQ(section.parkedAnchorsDataForTest(), parkedData);

    ASSERT_TRUE(section.parkBuild());
    ASSERT_TRUE(section.isBuildParked());
    EXPECT_EQ(section.parkedAnchorsDataForTest(), parkedData);
  }

  epub->tocCount = 220;
  epub->contents = "<html><body>";
  for (unsigned index = 0; index < 220; ++index) {
    epub->contents += "<p id=\"anchor-" + std::to_string(index) +
                      "\">anchor ownership paragraph " + std::to_string(index) +
                      " carries enough text for a parser checkpoint and resume.</p>";
  }
  epub->contents += "</body></html>";
  std::filesystem::remove(cache());
  std::filesystem::remove(root / "html/0.html");

  Section largeSection(epub, 0, renderer);
  ASSERT_TRUE(largeSection.startBuild(spec));
  while (largeSection.pageCount < 40u) {
    ASSERT_TRUE(largeSection.buildSomeMore(2));
    ASSERT_TRUE(largeSection.isBuilding());
  }
  ASSERT_TRUE(largeSection.parkBuild());
  ASSERT_TRUE(largeSection.isBuildParked());
  ASSERT_NE(largeSection.parkedAnchorsDataForTest(), nullptr);
  ASSERT_GT(largeSection.parkedAnchorsCapacityForTest(), 4u);

  ASSERT_TRUE(largeSection.buildSomeMore(2));
  ASSERT_TRUE(largeSection.isBuilding());
  EXPECT_EQ(largeSection.parkedAnchorsCapacityForTest(), 0u);
  EXPECT_EQ(largeSection.parkedAnchorsDataForTest(), nullptr);

  ASSERT_TRUE(largeSection.parkBuild());
  ASSERT_TRUE(largeSection.isBuildParked());
  EXPECT_NE(largeSection.parkedAnchorsDataForTest(), nullptr);
}

TEST_F(SectionCacheTest, ParkedBuildCloseReopensAsReadablePartialAndContinues) {
  uint16_t watermark = 0;
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.startBuild(spec));
    while (section.pageCount < 12u && section.isBuilding()) ASSERT_TRUE(section.buildSomeMore(2));
    ASSERT_TRUE(section.isBuilding());
    ASSERT_TRUE(parkSection(section));
    ASSERT_TRUE(section.isBuilding());
    ASSERT_TRUE(sectionIsParked(section));
    watermark = section.pageCount;
    ASSERT_NE(section.loadPage(watermark - 1), nullptr);
  }
  EXPECT_FALSE(std::filesystem::exists(cache().string() + ".checkpoint.part"));
  Section reopened(epub, 0, renderer);
  ASSERT_TRUE(reopened.loadSectionFile(spec));
  ASSERT_TRUE(reopened.isPartial());
  ASSERT_EQ(reopened.pageCount, watermark);
  ASSERT_NE(reopened.loadPage(watermark - 1), nullptr);
  ASSERT_TRUE(reopened.startBuild(spec));
  ASSERT_TRUE(reopened.buildSomeMore(0));
  ASSERT_TRUE(reopened.isBuildComplete());
  EXPECT_EQ(bytes(cache()), valid);
}

TEST_F(SectionCacheTest, CheckpointWriteSyncAndCloseFailuresKeepCommittedBytes) {
  for (const std::string mode : {"write", "sync", "close"}) {
    SCOPED_TRACE(mode);
    writeCache(valid);
    std::filesystem::remove(cache().string() + ".part");
    std::filesystem::remove(cache().string() + ".lut.part");
    std::filesystem::remove(cache().string() + ".checkpoint.part");
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.startBuild(spec));
    while (section.pageCount < 8u && section.isBuilding()) ASSERT_TRUE(section.buildSomeMore(2));
    ASSERT_TRUE(section.isBuilding());
    storageFault::target = "/sections/0.bin.checkpoint.part";
    if (mode == "write") {
      storageFault::writeOffset = 0;
      storageFault::writeEndOffset = std::numeric_limits<size_t>::max();
      storageFault::failOnce = true;
    } else if (mode == "sync") {
      storageFault::failFlush = true;
    } else {
      storageFault::failClose = true;
    }
    EXPECT_FALSE(parkSection(section));
    EXPECT_EQ(storageFault::hit, 1u);
    storageFault::reset();
    EXPECT_EQ(bytes(cache()), valid);
    section.abandonBuild();
  }
}

TEST_F(SectionCacheTest, CheckpointReadCrcAndParserOomFailuresKeepCommittedBytes) {
  for (const std::string mode : {"read", "crc", "oom"}) {
    SCOPED_TRACE(mode);
    writeCache(valid);
    std::filesystem::remove(cache().string() + ".part");
    std::filesystem::remove(cache().string() + ".lut.part");
    std::filesystem::remove(cache().string() + ".checkpoint.part");
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.startBuild(spec));
    while (section.pageCount < 8u && section.isBuilding()) ASSERT_TRUE(section.buildSomeMore(2));
    ASSERT_TRUE(section.isBuilding());
    ASSERT_TRUE(parkSection(section));
    ASSERT_TRUE(sectionIsParked(section));
    const std::string checkpointPath = cache().string() + ".checkpoint.part";
    ASSERT_TRUE(std::filesystem::exists(checkpointPath));
    if (mode == "read") {
      storageFault::target = "/sections/0.bin.checkpoint.part";
      storageFault::readOffset = 0;
      storageFault::readEndOffset = std::numeric_limits<size_t>::max();
      storageFault::shortReadOnce = true;
    } else if (mode == "crc") {
      std::fstream checkpoint(checkpointPath, std::ios::binary | std::ios::in | std::ios::out);
      ASSERT_TRUE(checkpoint.good());
      checkpoint.seekg(-1, std::ios::end);
      char value = 0;
      checkpoint.read(&value, 1);
      value ^= 0x5a;
      checkpoint.seekp(-1, std::ios::end);
      checkpoint.write(&value, 1);
      checkpoint.close();
    } else {
      allocationProbe::rejectNextNothrow = true;
    }
    EXPECT_FALSE(section.buildSomeMore(1));
    allocationProbe::rejectNextNothrow = false;
    storageFault::reset();
    EXPECT_EQ(bytes(cache()), valid);
    EXPECT_FALSE(std::filesystem::exists(checkpointPath));
  }
}

TEST_F(SectionCacheTest, StartBuildRemovesOrphanParserCheckpoint) {
  const std::string checkpointPath = cache().string() + ".checkpoint.part";
  {
    std::ofstream orphan(checkpointPath, std::ios::binary | std::ios::trunc);
    orphan << "orphan";
  }
  ASSERT_TRUE(std::filesystem::exists(checkpointPath));
  Section section(epub, 0, renderer);
  ASSERT_TRUE(section.startBuild(spec));
  EXPECT_FALSE(std::filesystem::exists(checkpointPath));
  section.abandonBuild();
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


// v1.0.14: a checkpoint exists only right after the parse step that finished a page. A build that
// yields on its step or time budget instead (the usual stop on the X3, 20 ms a tick) could neither
// be parked nor leave a checkpoint in its partial file, so the next open laid the chapter out again
// from its first page (783 to 857 ms a turn on the X3). Parking and suspending now parse on to the
// next checkpoint first. Every stop must park, and the pages must stay byte-identical to a cold build.
std::string streakChapter(ReaderRenderSpec& spec) {
  // Tall pages hold many paragraphs, so most ticks end on the step budget with no page finished.
  spec.viewportWidth = 480;
  spec.viewportHeight = 1600;
  std::string html = "<html><body><section>";
  for (unsigned paragraph = 0; paragraph < 160; ++paragraph) {
    html += "<p>";
    for (unsigned word = 0; word < 40; ++word) html += "streak" + std::to_string(paragraph) + "w" + std::to_string(word) + " ";
    html += "</p>";
  }
  return html + "</section></body></html>";
}

TEST_F(SectionCacheTest, EveryBuildStopParksAtTheNextCheckpointAndMatchesColdPages) {
  epub->contents = streakChapter(spec);
  std::filesystem::remove(cache());
  std::filesystem::remove(root / "html/0.html");
  const auto signaturePath = root / "streak-page-signature.bin";
  std::vector<std::string> coldPages;
  std::string cold;
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.createSectionFile(spec));
    std::cout << "SECTION_STREAK cold_pages=" << section.pageCount << " html=" << epub->contents.size() << "\n";
    ASSERT_GT(section.pageCount, 8u);
    for (uint16_t page = 0; page < section.pageCount; ++page) {
      const auto loaded = section.loadPage(page);
      ASSERT_NE(loaded, nullptr);
      std::string signature;
      ASSERT_TRUE(serializedPageSignature(*loaded, signaturePath, signature));
      coldPages.push_back(std::move(signature));
    }
    cold = bytes(cache());
  }
  std::filesystem::remove(cache());

  Section section(epub, 0, renderer);
  ASSERT_TRUE(section.startBuild(spec));
  unsigned stops = 0, parked = 0;
  while (section.isBuilding()) {
    ASSERT_TRUE(section.buildSomeMore(1));
    if (!section.isBuilding()) break;
    // A restore needs one finished page, so a stop before the first one has nothing to keep.
    if (section.pageCount == 0) continue;
    ++stops;
    if (parkSection(section) && sectionIsParked(section)) ++parked;
    if (!section.isBuilding()) break;
    ASSERT_LT(stops, 2000u);
  }
  std::cout << "SECTION_STREAK stops=" << stops << " parked=" << parked << "\n";
  ASSERT_GT(stops, 10u);
  EXPECT_EQ(parked, stops) << "a stop without a checkpoint could not be parked";
  ASSERT_TRUE(section.isBuildComplete());
  ASSERT_EQ(section.pageCount, coldPages.size());
  EXPECT_EQ(bytes(cache()), cold);
  for (uint16_t page = 0; page < section.pageCount; ++page) {
    const auto loaded = section.loadPage(page);
    ASSERT_NE(loaded, nullptr);
    std::string signature;
    ASSERT_TRUE(serializedPageSignature(*loaded, signaturePath, signature));
    EXPECT_EQ(signature, coldPages[page]) << page;
  }
}

TEST_F(SectionCacheTest, PartialLeftAtAStopWithoutCheckpointStillCarriesOne) {
  epub->contents = streakChapter(spec);
  std::filesystem::remove(root / "html/0.html");
  std::filesystem::remove(cache());
  std::string cold;
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.createSectionFile(spec));
    cold = bytes(cache());
  }
  // Every stop in the first pages: each close must leave a partial that resumes from its checkpoint.
  for (unsigned extra = 0; extra < 12; ++extra) {
    SCOPED_TRACE(extra);
    std::filesystem::remove(cache());
    uint16_t watermark = 0;
    {
      Section section(epub, 0, renderer);
      ASSERT_TRUE(section.startBuild(spec));
      while (section.pageCount == 0) ASSERT_TRUE(section.buildSomeMore(1));
      for (unsigned tick = 0; tick < extra; ++tick) ASSERT_TRUE(section.buildSomeMore(1));
      ASSERT_TRUE(section.isBuilding());
      section.suspendBuild();
      ASSERT_TRUE(section.isPartial());
      watermark = section.pageCount;
    }
    const std::string partial = bytes(cache());
    const auto checkpoint = checkpointOffset(partial);
    ASSERT_TRUE(checkpoint.has_value());
    EXPECT_LT(*checkpoint, partial.size()) << "the partial file carries no checkpoint";
    Section reopened(epub, 0, renderer);
    ASSERT_TRUE(reopened.loadSectionFile(spec));
    ASSERT_EQ(reopened.pageCount, watermark);
    storageMetrics::begin();
    storageMetrics::watchPrefix(root.string() + "/html/0.html", 0, 64);
    ASSERT_TRUE(reopened.startBuild(spec));
    storageMetrics::enabled = false;
    EXPECT_EQ(storageMetrics::prefixReadBytes, 0u) << "the chapter was laid out again from its first byte";
    ASSERT_TRUE(reopened.buildSomeMore(0));
    ASSERT_TRUE(reopened.isBuildComplete());
    EXPECT_EQ(bytes(cache()), cold);
  }
}


// v1.0.14: go-to-percent laid out the whole chapter before showing the target page, seconds on a
// long chapter with the indexing popup up. It now lays out only as far as the target share, and
// the page it lands on matches the page the finished chapter gives for the same share.
TEST_F(SectionCacheTest, PercentTargetLaysOutOnlyToItsShareAndLandsOnTheFinishedPage) {
  epub->contents = streakChapter(spec);
  std::filesystem::remove(root / "html/0.html");
  std::filesystem::remove(cache());
  uint16_t fullCount = 0;
  std::vector<uint16_t> finishedTargets;
  const std::array<float, 5> shares = {0.0f, 0.2f, 0.5f, 0.73f, 0.99f};
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.createSectionFile(spec));
    fullCount = section.pageCount;
    EXPECT_FLOAT_EQ(section.laidOutFraction(), 1.0f);
    for (const float share : shares) {
      // The finished chapter keeps the formula the reader always used.
      const int expected = std::min(static_cast<int>(share * static_cast<float>(fullCount)), fullCount - 1);
      EXPECT_EQ(section.pageAtFraction(share), expected) << share;
      finishedTargets.push_back(section.pageAtFraction(share));
    }
  }
  for (size_t i = 0; i < shares.size(); ++i) {
    SCOPED_TRACE(shares[i]);
    std::filesystem::remove(cache());
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.startBuild(spec));
    while (!section.isBuildComplete() && !section.laidOutTo(shares[i])) ASSERT_TRUE(section.buildSomeMore(8));
    if (shares[i] < 0.9f) {
      EXPECT_TRUE(section.isBuilding());
      EXPECT_LE(section.pageCount, static_cast<uint16_t>(shares[i] * fullCount) + 3) << "laid out past the target";
    }
    const int landed = section.pageAtFraction(shares[i]);
    EXPECT_LE(std::abs(landed - static_cast<int>(finishedTargets[i])), 1) << landed << " vs " << finishedTargets[i];
    ASSERT_NE(section.loadPage(landed), nullptr);
  }
}

TEST_F(SectionCacheTest, PartialCoversAnOffsetOnlyBeforeItsLastPage) {
  epub->contents = streakChapter(spec);
  std::filesystem::remove(root / "html/0.html");
  std::filesystem::remove(cache());
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.startBuild(spec));
    while (section.pageCount < 5) ASSERT_TRUE(section.buildSomeMore(1));
    section.suspendBuild();
  }
  Section partial(epub, 0, renderer);
  ASSERT_TRUE(partial.loadSectionFile(spec));
  ASSERT_TRUE(partial.isPartial());
  const auto last = partial.getVisibleTextOffsetForPage(partial.pageCount - 1);
  const auto second = partial.getVisibleTextOffsetForPage(1);
  ASSERT_TRUE(last.has_value() && second.has_value());
  EXPECT_TRUE(partial.coversVisibleTextOffset(*second));
  EXPECT_TRUE(partial.coversVisibleTextOffset(*last - 1));
  // The last page may run on past the watermark: its offset still needs the layout.
  EXPECT_FALSE(partial.coversVisibleTextOffset(*last));
  EXPECT_FALSE(partial.coversVisibleTextOffset(*last + 500));
}


// A suspend near the end of a chapter whose tail cannot hold a checkpoint (a table) parses on to
// the chapter's end. The build then finishes instead of leaving a partial, and the file matches
// the cold build.
TEST_F(SectionCacheTest, SuspendThatReachesTheChapterEndFinishesTheBuild) {
  epub->contents = streakChapter(spec);
  const std::string tail = "</section></body></html>";
  epub->contents.resize(epub->contents.size() - tail.size());
  const size_t tableStart = epub->contents.size();
  epub->contents += "<table>";
  for (unsigned row = 0; row < 12; ++row) {
    epub->contents += "<tr><td>";
    for (unsigned word = 0; word < 30; ++word) epub->contents += "cell" + std::to_string(row) + "w" + std::to_string(word) + " ";
    epub->contents += "</td></tr>";
  }
  epub->contents += "</table>" + tail;
  std::filesystem::remove(root / "html/0.html");
  std::filesystem::remove(cache());
  std::string cold;
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.createSectionFile(spec));
    cold = bytes(cache());
  }
  std::filesystem::remove(cache());
  const float inTable = static_cast<float>(tableStart + 400) / static_cast<float>(epub->contents.size());
  bool finished = false;
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.startBuild(spec));
    while (section.isBuilding() && section.laidOutFraction() < inTable) ASSERT_TRUE(section.buildSomeMore(1));
    ASSERT_TRUE(section.isBuilding()) << "the fixture finished before its table";
    section.suspendBuild();
    finished = !section.isPartial();
  }
  std::cout << "SECTION_TAIL finished=" << finished << "\n";
  if (!finished) {
    Section reopened(epub, 0, renderer);
    ASSERT_TRUE(reopened.loadSectionFile(spec));
    ASSERT_TRUE(reopened.startBuild(spec));
    ASSERT_TRUE(reopened.buildSomeMore(0));
  }
  EXPECT_EQ(bytes(cache()), cold);
}


// v1.0.14 round 2, from the X3 (r41): with the radio up the heap sits near free 46 KB / largest
// 20 KB. A parked build resumed there can starve before its first step (free 16.8 KB, largest
// 8 KB, below the step floor), so it cannot parse on to a checkpoint. It used to be written out as
// a partial without one, and the next turn laid the chapter out again from its first page
// (3.6 s with the indexing popup). No page was finished since the park, so the checkpoint written
// at the park still describes it exactly: the build parks there again instead.
TEST_F(SectionCacheTest, StarvedRightAfterAResumeParksAgainAtTheLastCheckpoint) {
  epub->contents = streakChapter(spec);
  std::filesystem::remove(root / "html/0.html");
  std::filesystem::remove(cache());
  std::string cold;
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.createSectionFile(spec));
    cold = bytes(cache());
  }
  std::filesystem::remove(cache());
  const auto bleHeap = [] { ESP.freeHeap = 46 * 1024; ESP.maxAlloc = 20 * 1024; };
  const auto starvedHeap = [] { ESP.freeHeap = 16820; ESP.maxAlloc = 8180; };
  uint16_t watermark = 0;
  {
    bleHeap();
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.startBuild(spec));
    while (section.pageCount < 4) ASSERT_TRUE(section.buildSomeMore(1));
    ASSERT_TRUE(section.parkBuild());
    ASSERT_TRUE(section.isBuildParked());
    // The look-ahead resumes the parked parser and parses on without finishing a page (r41: last
    // park at 19 pages, starved at 19 pages), then the heap drops under the step floor.
    for (unsigned tries = 0;; ++tries) {
      ASSERT_LT(tries, 50u);
      watermark = section.pageCount;
      ASSERT_TRUE(section.buildSomeMore(1));
      if (section.pageCount == watermark) break;
      ASSERT_TRUE(section.parkBuild());
    }
    ASSERT_FALSE(section.isBuildParked());
    starvedHeap();
    EXPECT_FALSE(section.buildSomeMore(2));
    EXPECT_TRUE(section.buildStarved());
    EXPECT_TRUE(section.isBuildParked()) << "a starved resume kept the parser resident without a checkpoint";
    EXPECT_EQ(section.pageCount, watermark);
    // The reader's release under heap pressure, still starved, then the close.
    if (!section.isBuildParked()) section.suspendBuild();
  }
  ESP = {};
  const std::string partial = bytes(cache());
  const auto checkpoint = checkpointOffset(partial);
  ASSERT_TRUE(checkpoint.has_value());
  EXPECT_LT(*checkpoint, partial.size()) << "the partial file carries no checkpoint";
  Section reopened(epub, 0, renderer);
  ASSERT_TRUE(reopened.loadSectionFile(spec));
  ASSERT_EQ(reopened.pageCount, watermark);
  storageMetrics::begin();
  storageMetrics::watchPrefix(root.string() + "/html/0.html", 0, 64);
  ASSERT_TRUE(reopened.startBuild(spec));
  storageMetrics::enabled = false;
  EXPECT_EQ(storageMetrics::prefixReadBytes, 0u) << "the chapter was laid out again from its first byte";
  ASSERT_TRUE(reopened.buildSomeMore(0));
  EXPECT_EQ(bytes(cache()), cold);
}

// A page finished after the park moves the build past that checkpoint: parking back there would
// drop the page. The build keeps the old fallback (partial without a checkpoint) and stays correct.
TEST_F(SectionCacheTest, StarvedAfterANewPageKeepsTheOldFallback) {
  epub->contents = streakChapter(spec);
  std::filesystem::remove(root / "html/0.html");
  std::filesystem::remove(cache());
  std::string cold;
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.createSectionFile(spec));
    cold = bytes(cache());
  }
  std::filesystem::remove(cache());
  {
    ESP.freeHeap = 46 * 1024; ESP.maxAlloc = 20 * 1024;
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.startBuild(spec));
    while (section.pageCount < 3) ASSERT_TRUE(section.buildSomeMore(1));
    ASSERT_TRUE(section.parkBuild());
    const uint16_t parkedPages = section.pageCount;
    while (section.pageCount == parkedPages) ASSERT_TRUE(section.buildSomeMore(1));
    ESP.freeHeap = 16820; ESP.maxAlloc = 8180;
    EXPECT_FALSE(section.buildSomeMore(1));
    EXPECT_TRUE(section.buildStarved());
    EXPECT_TRUE(section.isBuilding());
    EXPECT_GT(section.pageCount, parkedPages) << "a finished page was dropped";
    if (!section.isBuildParked()) section.suspendBuild();
  }
  ESP = {};
  Section reopened(epub, 0, renderer);
  ASSERT_TRUE(reopened.loadSectionFile(spec));
  ASSERT_TRUE(reopened.startBuild(spec));
  ASSERT_TRUE(reopened.buildSomeMore(0));
  EXPECT_EQ(bytes(cache()), cold);
}


// A build resumed from a partial file's checkpoint (a reopened chapter) that starves before its
// first new page parks back at that checkpoint too. It lives inside the partial file, not in a
// park's checkpoint file, so the parked build later resumes from the partial file.
TEST_F(SectionCacheTest, StarvedAfterARestoreParksBackAtThePartialCheckpoint) {
  epub->contents = streakChapter(spec);
  std::filesystem::remove(root / "html/0.html");
  std::filesystem::remove(cache());
  std::string cold;
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.createSectionFile(spec));
    cold = bytes(cache());
  }
  std::filesystem::remove(cache());
  uint16_t watermark = 0;
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.startBuild(spec));
    while (section.pageCount < 4) ASSERT_TRUE(section.buildSomeMore(1));
    section.suspendBuild();
    ASSERT_TRUE(section.isPartial());
    watermark = section.pageCount;
  }
  {
    ESP.freeHeap = 46 * 1024; ESP.maxAlloc = 20 * 1024;
    Section reopened(epub, 0, renderer);
    ASSERT_TRUE(reopened.loadSectionFile(spec));
    ASSERT_TRUE(reopened.startBuild(spec));
    ASSERT_EQ(reopened.pageCount, watermark);
    // Parse on without finishing a page, then the heap drops under the step floor.
    ASSERT_TRUE(reopened.buildSomeMore(1));
    ASSERT_EQ(reopened.pageCount, watermark) << "the fixture finished a page in one tick";
    ESP.freeHeap = 16820; ESP.maxAlloc = 8180;
    EXPECT_FALSE(reopened.buildSomeMore(1));
    EXPECT_TRUE(reopened.buildStarved());
    EXPECT_TRUE(reopened.isBuildParked()) << "a restored build could not park back at the partial's checkpoint";
    EXPECT_EQ(reopened.pageCount, watermark);
    ESP = {};
    // Resumed later, from the partial file, it finishes exactly like a cold build.
    while (!reopened.isBuildComplete()) ASSERT_TRUE(reopened.buildSomeMore(8));
  }
  ESP = {};
  EXPECT_EQ(bytes(cache()), cold);
}

// v1.0.53, X3 07/10/2026: back in the book the radio restarts and takes ~50 KB, and the parked build's
// resume runs out of heap in its checkpoint restore. It was abandoned ("Unable to resume") and the
// chapter laid out again from its first page behind the indexing notice. A resume short of heap is a
// starved build: it stays parked, the reader frees the heap, and it resumes from the same checkpoint.
TEST_F(SectionCacheTest, ResumeShortOfHeapStaysParked) {
  epub->contents = streakChapter(spec);
  std::filesystem::remove(root / "html/0.html");
  std::filesystem::remove(cache());
  std::string cold;
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.createSectionFile(spec));
    cold = bytes(cache());
  }
  std::filesystem::remove(cache());
  Section section(epub, 0, renderer);
  ASSERT_TRUE(section.startBuild(spec));
  while (section.pageCount < 4) ASSERT_TRUE(section.buildSomeMore(1));
  ASSERT_TRUE(section.parkBuild());
  const uint16_t parked = section.pageCount;
  ESP.freeHeap = 16820;
  ESP.maxAlloc = 8180;
  allocationProbe::rejectNextNothrow = true;
  EXPECT_FALSE(section.buildSomeMore(1));
  EXPECT_TRUE(section.buildStarved());
  EXPECT_TRUE(section.isBuilding()) << "a resume short of heap abandoned the build";
  EXPECT_TRUE(section.isBuildParked());
  EXPECT_EQ(section.pageCount, parked);
  ESP = {};
  while (!section.isBuildComplete()) ASSERT_TRUE(section.buildSomeMore(8));
  EXPECT_EQ(bytes(cache()), cold);
}

// The same for a chapter reopened from its partial file: the restore at the start runs out of heap.
// It used to lay the chapter out again from its first byte; the start is refused as starved, the
// partial stays readable, and the next start restores from its checkpoint.
TEST_F(SectionCacheTest, PartialRestoreShortOfHeapIsRefusedAsStarved) {
  epub->contents = streakChapter(spec);
  std::filesystem::remove(root / "html/0.html");
  std::filesystem::remove(cache());
  std::string cold;
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.createSectionFile(spec));
    cold = bytes(cache());
  }
  std::filesystem::remove(cache());
  uint16_t watermark = 0;
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.startBuild(spec));
    while (section.pageCount < 4) ASSERT_TRUE(section.buildSomeMore(1));
    section.suspendBuild();
    ASSERT_TRUE(section.isPartial());
    watermark = section.pageCount;
  }
  Section reopened(epub, 0, renderer);
  ASSERT_TRUE(reopened.loadSectionFile(spec));
  // The first TextBlock the restore asks for is refused, on a heap under the step floor.
  bool refused = false;
  for (unsigned call = 1; call < 20 && !refused; ++call) {
    ESP.freeHeap = 16820;
    ESP.maxAlloc = 8180;
    allocationProbe::rejectSize = sizeof(TextBlock);
    allocationProbe::matchingCalls = 0;
    allocationProbe::rejectCall = call;
    allocationProbe::rejected = 0;
    storageMetrics::begin();
    storageMetrics::watchPrefix(root.string() + "/html/0.html", 0, 64);
    const bool started = reopened.startBuild(spec);
    storageMetrics::enabled = false;
    allocationProbe::rejectSize = 0;
    ESP = {};
    ASSERT_EQ(allocationProbe::rejected, 1u) << "no TextBlock was asked for at call " << call;
    if (started) {
      EXPECT_EQ(storageMetrics::prefixReadBytes, 0u) << "laid out again from the first byte at call " << call;
      reopened.abandonBuild();
      continue;
    }
    refused = true;
    EXPECT_TRUE(reopened.buildStarved());
    EXPECT_FALSE(reopened.isBuilding());
    EXPECT_EQ(reopened.pageCount, watermark) << "the partial's pages are gone";
  }
  ASSERT_TRUE(refused);
  storageMetrics::begin();
  storageMetrics::watchPrefix(root.string() + "/html/0.html", 0, 64);
  ASSERT_TRUE(reopened.startBuild(spec));
  storageMetrics::enabled = false;
  EXPECT_EQ(storageMetrics::prefixReadBytes, 0u) << "the chapter was laid out again from its first byte";
  while (!reopened.isBuildComplete()) ASSERT_TRUE(reopened.buildSomeMore(8));
  EXPECT_EQ(bytes(cache()), cold);
}

}

// The page shown while the chapter is laid out again under new text settings: it opens at the line
// that holds the old page's first word and carries the same lines as the full layout from there.
namespace {
std::vector<std::string> pageLines(const Page& page) {
  std::vector<std::string> lines;
  for (const auto& element : page.elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto& block = *static_cast<const PageLine&>(*element).getBlock();
    std::string line;
    for (uint16_t i = 0; i < block.wordCount(); ++i) line += (i ? " " : "") + std::string(block.wordText(i));
    lines.push_back(line);
  }
  return lines;
}
bool hasWord(const std::string& line, const std::string& word) {
  return (" " + line + " ").find(" " + word + " ") != std::string::npos;
}
struct PreviewBook {
  std::filesystem::path root = std::filesystem::temp_directory_path() / ("cross-preview-" + std::to_string(getpid()));
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  GfxRenderer renderer;
  ReaderRenderSpec narrow, wide;
  PreviewBook() {
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    epub->cachePath = root.string();
    epub->contents = "<html><body><div>";
    for (int p = 0; p < 40; ++p) {
      epub->contents += "<p>";
      for (int w = 0; w < 9 + (p * 7) % 31; ++w) epub->contents += "p" + std::to_string(p) + "w" + std::to_string(w) + " ";
      epub->contents += "</p>";
    }
    epub->contents += "</div></body></html>";
    narrow.viewportWidth = 160; narrow.viewportHeight = 120; narrow.embeddedStyle = false; narrow.dropCapMode = 0;
    wide = narrow; wide.viewportWidth = 240; wide.lineCompression = 1.25f;
  }
  ~PreviewBook() { std::filesystem::remove_all(root); }
  std::vector<std::string> allLines(const ReaderRenderSpec& spec) {
    Section section(epub, 0, renderer);
    if (!section.loadSectionFile(spec) && !section.createSectionFile(spec)) return {};
    std::vector<std::string> lines;
    for (int i = 0; i < section.pageCount; ++i) {
      auto page = section.loadPage(i);
      if (!page) return {};
      for (auto& line : pageLines(*page)) lines.push_back(line);
    }
    return lines;
  }
};
}  // namespace

TEST(PreviewPage, OpensAtTheLineHoldingTheOldPageStart) {
  PreviewBook book;
  struct Start { uint32_t offset; std::string word; };
  std::vector<Start> starts;
  {
    Section old(book.epub, 0, book.renderer);
    ASSERT_TRUE(old.createSectionFile(book.narrow));
    ASSERT_GT(old.pageCount, 12);
    for (uint16_t k = 1; k < old.pageCount; ++k) {
      auto page = old.loadPage(k);
      ASSERT_TRUE(page);
      const auto lines = pageLines(*page);
      ASSERT_FALSE(lines.empty());
      starts.push_back({*old.getVisibleTextOffsetForPage(k), lines[0].substr(0, lines[0].find(' '))});
    }
  }
  ASSERT_TRUE(std::filesystem::exists(book.root / "sections/0.dd"));
  const auto wideLines = book.allLines(book.wide);
  ASSERT_FALSE(wideLines.empty());
  for (const auto& start : starts) {
    Section section(book.epub, 0, book.renderer);
    auto page = section.previewPage(book.wide, start.offset);
    ASSERT_TRUE(page) << "offset " << start.offset;
    EXPECT_LE(page->visibleTextOffset, start.offset);
    const auto lines = pageLines(*page);
    ASSERT_FALSE(lines.empty());
    EXPECT_TRUE(hasWord(lines[0], start.word)) << "first line '" << lines[0] << "' lacks " << start.word;
    const auto at = std::find(wideLines.begin(), wideLines.end(), lines[0]);
    ASSERT_NE(at, wideLines.end()) << lines[0];
    for (size_t i = 0; i < lines.size() && at + i != wideLines.end(); ++i) EXPECT_EQ(lines[i], *(at + i));
  }
}

TEST(PreviewPage, NoResumePointsNoPreview) {
  PreviewBook book;
  uint32_t offset = 0;
  {
    Section old(book.epub, 0, book.renderer);
    ASSERT_TRUE(old.createSectionFile(book.narrow));
    offset = *old.getVisibleTextOffsetForPage(5);
  }
  std::filesystem::remove(book.root / "sections/0.dd");
  Section section(book.epub, 0, book.renderer);
  EXPECT_FALSE(section.previewPage(book.wide, offset));
  // A build under other settings does not bring them back: only one from the chapter top writes them,
  // and that is the same build whatever the settings.
  ASSERT_TRUE(section.createSectionFile(book.wide));
  EXPECT_TRUE(std::filesystem::exists(book.root / "sections/0.dd"));
  Section again(book.epub, 0, book.renderer);
  EXPECT_TRUE(again.previewPage(book.narrow, offset));
}

TEST(PreviewPage, FirstPageWithoutResumeIndexFitsSheetHeapBudget) {
  PreviewBook book;
  book.epub->contents = "<html><body><div>";
  for (int p = 0; p < 1000; ++p)
    book.epub->contents += "<p>alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu</p>";
  book.epub->contents += "</div></body></html>";
  std::vector<std::string> expected;
  {
    Section old(book.epub, 0, book.renderer);
    ASSERT_TRUE(old.createSectionFile(book.wide));
    auto first = old.loadPage(0);
    ASSERT_TRUE(first);
    expected = pageLines(*first);
  }
  std::filesystem::remove(book.root / "sections/0.dd");
  Section section(book.epub, 0, book.renderer);
  allocationProbe::live = allocationProbe::peak = 0;
  allocationProbe::calls = allocationProbe::largest = allocationProbe::rejected = 0;
  allocationProbe::liveLimit = 70 * 1024;
  allocationProbe::requestLimit = 40948;
  allocationProbe::enabled = true;
  std::unique_ptr<Page> page;
  try {
    page = section.previewPage(book.wide, 0);
  } catch (...) {
    allocationProbe::enabled = false;
    allocationProbe::liveLimit = allocationProbe::requestLimit = 0;
    throw;
  }
  allocationProbe::enabled = false;
  allocationProbe::liveLimit = allocationProbe::requestLimit = 0;
  std::cout << "PREVIEW_NO_DD peak=" << allocationProbe::peak << " largest=" << allocationProbe::largest
            << " rejected=" << allocationProbe::rejected << '\n';
  ASSERT_TRUE(page);
  EXPECT_EQ(pageLines(*page), expected);
  EXPECT_EQ(page->visibleTextOffset, 0u);
  EXPECT_EQ(allocationProbe::rejected, 0u);
  EXPECT_FALSE(std::filesystem::exists(book.root / "sections/0.dd"));
  EXPECT_FALSE(section.isBuilding());
}

TEST(PreviewPage, FallbackParserReleasesHeapAndKeepsPageWhenParked) {
  PreviewBook book;
  Section section(book.epub, 0, book.renderer);
#ifdef __APPLE__
  const auto heapUsed = [] {
    malloc_statistics_t stats{};
    malloc_zone_statistics(nullptr, &stats);
    return stats.size_in_use;
  };
  const size_t heapBefore = heapUsed();
#endif
  allocationProbe::live = allocationProbe::peak = 0;
  allocationProbe::enabled = true;
  const bool started = section.startBuild(book.wide);
  const bool built = started && section.buildSomeMore(8);
  const long active = allocationProbe::live;
#ifdef __APPLE__
  const size_t heapActive = heapUsed();
#endif
  const bool parked = built && section.parkBuild();
  const long after = allocationProbe::live;
  allocationProbe::enabled = false;
#ifdef __APPLE__
  const size_t heapAfter = heapUsed();
  std::cout << "FALLBACK_ALL_HEAP active=" << heapActive - heapBefore << " parked=" << heapAfter - heapBefore
            << " released=" << heapActive - heapAfter << '\n';
#endif
  std::cout << "FALLBACK_PARSER active=" << active << " parked=" << after << " released=" << active - after << '\n';
  ASSERT_TRUE(parked);
  ASSERT_TRUE(section.isBuildParked());
  EXPECT_GT(active, after);
  const auto page = section.loadPage(0);
  ASSERT_TRUE(page);
  EXPECT_FALSE(pageLines(*page).empty());
  EXPECT_EQ(page->visibleTextOffset, 0u);
}

TEST(PreviewPage, KeepsParagraphGapAfterTheHeldFinalLine) {
  PreviewBook book;
  book.epub->contents = "<html><body><p>BEFORE</p><p>ANCHOR</p><p>AFTER</p></body></html>";
  book.narrow.viewportWidth = 240;
  book.narrow.viewportHeight = 18;
  uint32_t target = 0;
  {
    Section old(book.epub, 0, book.renderer);
    ASSERT_TRUE(old.createSectionFile(book.narrow));
    for (uint16_t i = 0; i < old.pageCount; ++i) {
      auto page = old.loadPage(i);
      ASSERT_TRUE(page);
      const auto lines = pageLines(*page);
      if (!lines.empty() && lines.front() == "ANCHOR") target = *old.getVisibleTextOffsetForPage(i);
    }
  }
  ASSERT_GT(target, 0u);
  book.wide.viewportHeight = 400;
  book.wide.lineCompression = 1.0f;
  book.wide.extraParagraphSpacing = readerSpacing::VERY_WIDE;
  Section full(book.epub, 0, book.renderer);
  ASSERT_TRUE(full.createSectionFile(book.wide));
  auto reference = full.loadPage(0);
  ASSERT_TRUE(reference);
  ASSERT_EQ(pageLines(*reference), (std::vector<std::string>{"BEFORE", "ANCHOR", "AFTER"}));
  Section section(book.epub, 0, book.renderer);
  auto preview = section.previewPage(book.wide, target);
  ASSERT_TRUE(preview);
  EXPECT_EQ(pageLines(*preview), (std::vector<std::string>{"ANCHOR", "AFTER"}));
  ASSERT_EQ(preview->elements.size(), 2u);
  const int referenceGap = reference->elements[2]->yPos - reference->elements[1]->yPos;
  const int previewGap = preview->elements[1]->yPos - preview->elements[0]->yPos;
  std::cout << "HELD_GAP reference=" << referenceGap << " preview=" << previewGap << '\n';
  EXPECT_EQ(previewGap, referenceGap);
}

TEST(PreviewPage, KeepsImageAndCssMarginsAfterTheHeldFinalLine) {
  struct EnableImages {
    EnableImages() { parserImageFixture::enabled = true; }
    ~EnableImages() { parserImageFixture::enabled = false; }
  } enableImages;
  PreviewBook book;
  CssParser css(book.root.string());
  book.epub->cssParser = &css;
  const uint8_t png[] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10, 0, 0, 0, 13,
                         'I', 'H', 'D', 'R', 0, 0, 0, 32, 0, 0, 0, 24};
  for (const char* path : {"before.png", "after.png"}) {
    book.epub->images[path] = std::string(reinterpret_cast<const char*>(png), sizeof(png));
  }
  book.epub->contents = "<html><body><p>BEFORE</p><img src=\"before.png\"/>"
                        "<p style=\"margin-bottom:11px\">ANCHOR</p>"
                        "<div style=\"margin-top:9px;margin-bottom:13px\"><img src=\"after.png\"/></div>"
                        "<p style=\"margin-top:7px\">AFTER</p></body></html>";
  book.narrow.embeddedStyle = true;
  book.narrow.viewportWidth = 240;
  book.narrow.viewportHeight = 18;
  uint32_t target = 0;
  {
    Section old(book.epub, 0, book.renderer);
    ASSERT_TRUE(old.createSectionFile(book.narrow));
    for (uint16_t i = 0; i < old.pageCount; ++i) {
      auto page = old.loadPage(i);
      ASSERT_TRUE(page);
      const auto lines = pageLines(*page);
      if (!lines.empty() && lines.front() == "ANCHOR") target = *old.getVisibleTextOffsetForPage(i);
    }
  }
  ASSERT_GT(target, 0u);
  book.wide = book.narrow;
  book.wide.viewportHeight = 400;
  book.wide.extraParagraphSpacing = readerSpacing::VERY_WIDE;
  Section full(book.epub, 0, book.renderer);
  ASSERT_TRUE(full.createSectionFile(book.wide));
  auto reference = full.loadPage(0);
  ASSERT_TRUE(reference);
  ASSERT_EQ(reference->elements.size(), 5u);
  ASSERT_EQ(reference->elements[1]->getTag(), TAG_PageImage);
  ASSERT_EQ(reference->elements[2]->getTag(), TAG_PageLine);
  ASSERT_EQ(reference->elements[3]->getTag(), TAG_PageImage);
  for (const uint32_t start : {target, target + 1, target + 5}) {
    SCOPED_TRACE(start);
    Section section(book.epub, 0, book.renderer);
    auto preview = section.previewPage(book.wide, start);
    ASSERT_TRUE(preview);
    EXPECT_EQ(pageLines(*preview), (std::vector<std::string>{"ANCHOR", "AFTER"}));
    ASSERT_EQ(preview->elements.size(), 3u);
    for (size_t i = 0; i < preview->elements.size(); ++i) {
      EXPECT_EQ(preview->elements[i]->getTag(), reference->elements[i + 2]->getTag());
      EXPECT_EQ(preview->elements[i]->xPos, reference->elements[i + 2]->xPos);
      EXPECT_EQ(preview->elements[i]->yPos,
                reference->elements[i + 2]->yPos - reference->elements[2]->yPos);
    }
    const auto& image = static_cast<const PageImage&>(*preview->elements[1]).getImageBlock();
    EXPECT_TRUE(image.getImagePath().ends_with("img_0_1.png"));
    EXPECT_EQ(image.getWidth(), 32);
    EXPECT_EQ(image.getHeight(), 24);
    std::cout << "HELD_IMAGE target=" << start << " image_y=" << preview->elements[1]->yPos
              << " after_y=" << preview->elements[2]->yPos << '\n';
  }
  for (const uint32_t start : {target + 6, target + 10}) {  // next-paragraph boundary and chapter's last line
    Section section(book.epub, 0, book.renderer);
    auto preview = section.previewPage(book.wide, start);
    ASSERT_TRUE(preview);
    EXPECT_EQ(pageLines(*preview), (std::vector<std::string>{"AFTER"}));
    ASSERT_EQ(preview->elements.size(), 1u);
    EXPECT_EQ(preview->elements.front()->yPos, 0);
  }
  book.wide.viewportHeight = 96;
  Section shortPage(book.epub, 0, book.renderer);
  auto preview = shortPage.previewPage(book.wide, target);
  ASSERT_TRUE(preview);
  EXPECT_EQ(pageLines(*preview), (std::vector<std::string>{"ANCHOR"}));
  ASSERT_EQ(preview->elements.size(), 2u);
  EXPECT_EQ(preview->elements[1]->getTag(), TAG_PageImage);
  EXPECT_EQ(preview->elements[1]->yPos, 36);
}

TEST(PreviewPage, CorruptDdLengthsFailBeforeAllocation) {
  PreviewBook book;
  // A valid long prolog lets the combined-bound case replay valid XML on the old reader.
  std::string prolog = "<?xml version=\"1.0\"?>\n";
  prolog.resize(1024, ' ');
  book.epub->contents.insert(0, prolog);
  Section old(book.epub, 0, book.renderer);
  ASSERT_TRUE(old.createSectionFile(book.narrow));
  const auto ddPath = book.root / "sections/0.dd";
  const auto htmlPath = book.root / "html/0.html";
  const auto valid = bytes(ddPath);
  const auto html = bytes(htmlPath);
  const auto cache = bytes(book.root / "sections/0.bin");
  ASSERT_GE(valid.size(), 28u);
  const uint32_t target = pod<uint32_t>(valid, 20) + 1;
  struct Damage {
    const char* name;
    uint16_t prolog, length;
    uint32_t records;
    std::string prefix;
    size_t allocationLimit;
    bool shortHtml = false;
    bool preview = false;
  };
  std::string longPrefix = "<html data-test=\"";
  longPrefix.append(1025 - longPrefix.size() - std::string("\"><body><div>").size(), 'a');
  longPrefix += "\"><body><div>";
  std::string maxPrefix = longPrefix;
  maxPrefix.erase(maxPrefix.find("aaa"), 1);
  const Damage damage[] = {
      {"prolog65535", UINT16_MAX, 17, 1, "<html><body><div>", 2049},
      {"prefix65535", 1024, UINT16_MAX, 1, std::string(UINT16_MAX, 'a'), 2049},
      {"truncatedPrefix", 0, 1900, 160, "x", 1900},
      {"combined2049", 1024, 1025, 1, longPrefix, 2049},
      {"recordCountOverflow", 1024, 17, UINT32_MAX, "<html><body><div>", 2049},
      {"prologPastHtml", 1024, 17, 1, "<html><body><div>", 1024, true},
      {"validCombined2048", 1024, 1024, 1, maxPrefix, SIZE_MAX, false, true},
  };
  for (const auto& item : damage) {
    SCOPED_TRACE(item.name);
    std::string damaged = valid.substr(0, 16);
    const uint32_t htmlSize = item.shortHtml ? 64 : html.size();
    memcpy(damaged.data() + 4, &htmlSize, sizeof(htmlSize));
    memcpy(damaged.data() + 8, &item.records, sizeof(item.records));
    memcpy(damaged.data() + 12, &item.prolog, sizeof(item.prolog));
    damaged[14] = 1;
    std::string record = valid.substr(16, 12);
    record[10] = 0;
    for (uint32_t i = 0; i < (item.records == UINT32_MAX ? 1 : item.records); ++i) damaged += record;
    damaged.append(reinterpret_cast<const char*>(&item.length), sizeof(item.length));
    damaged += item.prefix;
    std::ofstream(ddPath, std::ios::binary | std::ios::trunc).write(damaged.data(), damaged.size());
    std::ofstream(htmlPath, std::ios::binary | std::ios::trunc).write(html.data(), htmlSize);
    Section section(book.epub, 0, book.renderer);
    allocationProbe::largest = 0;
    allocationProbe::enabled = true;
    auto page = section.previewPage(book.wide, target);
    allocationProbe::enabled = false;
    const size_t largest = allocationProbe::largest;
    std::cout << "DD_BOUNDS name=" << item.name << " largest=" << largest << " preview=" << bool(page) << '\n';
    EXPECT_EQ(bool(page), item.preview);
    EXPECT_LT(largest, item.allocationLimit);
    EXPECT_EQ(bytes(ddPath), damaged);
    EXPECT_EQ(bytes(htmlPath), html.substr(0, htmlSize));
    EXPECT_EQ(bytes(book.root / "sections/0.bin"), cache);
  }
}

TEST(PreviewPage, AbandonedBuildKeepsItsPointsAndTheCommittedCache) {
  PreviewBook book;
  {
    Section old(book.epub, 0, book.renderer);
    ASSERT_TRUE(old.createSectionFile(book.narrow));
  }
  const auto kept = bytes(book.root / "sections/0.dd");
  std::filesystem::remove(book.root / "sections/0.dd");
  std::filesystem::copy_file(book.root / "sections/0.bin", book.root / "keep.bin");
  {
    Section build(book.epub, 0, book.renderer);
    ASSERT_TRUE(build.startBuild(book.wide));
    for (int i = 0; i < 6; ++i) ASSERT_TRUE(build.buildSomeMore(1));
    build.abandonBuild();
  }
  // The points found before the stop are kept (they do not depend on the layout), the staging files go.
  EXPECT_TRUE(std::filesystem::exists(book.root / "sections/0.dd"));
  EXPECT_LT(std::filesystem::file_size(book.root / "sections/0.dd"), kept.size());
  EXPECT_FALSE(std::filesystem::exists(book.root / "sections/0.dd.part"));
  EXPECT_FALSE(std::filesystem::exists(book.root / "sections/0.bin.part"));
  EXPECT_EQ(bytes(book.root / "sections/0.bin"), bytes(book.root / "keep.bin")) << "the committed cache changed";
  EXPECT_FALSE(kept.empty());
}

// Heap and time of the preview page against laying the chapter out to the same place, printed for the
// research notes (not a gate: host pointers are twice the X3's).
TEST(PreviewPage, HeapAndTimeAgainstLayingOutToThePage) {
  PreviewBook book;
  book.epub->contents = "<html><body><div>";
  for (int p = 0; p < 400; ++p) {
    book.epub->contents += "<p>";
    for (int w = 0; w < 30; ++w) book.epub->contents += "p" + std::to_string(p) + "w" + std::to_string(w) + " ";
    book.epub->contents += "</p>";
  }
  book.epub->contents += "</div></body></html>";
  uint32_t offset = 0;
  uint16_t pages = 0;
  {
    Section old(book.epub, 0, book.renderer);
    ASSERT_TRUE(old.createSectionFile(book.narrow));
    pages = old.pageCount;
    offset = *old.getVisibleTextOffsetForPage(pages * 9 / 10);
  }
  const auto measure = [](auto&& work) {
    allocationProbe::live = allocationProbe::peak = 0;
    allocationProbe::calls = allocationProbe::largest = 0;
    allocationProbe::enabled = true;
    const auto started = std::chrono::steady_clock::now();
    const bool ok = work();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    allocationProbe::enabled = false;
    return std::make_tuple(ok, allocationProbe::peak, ms);
  };
  const auto [previewOk, previewPeak, previewMs] = measure([&] {
    Section section(book.epub, 0, book.renderer);
    return static_cast<bool>(section.previewPage(book.wide, offset));
  });
  const size_t previewCalls = allocationProbe::calls;
  const size_t previewLargest = allocationProbe::largest;
  const auto [buildOk, buildPeak, buildMs] = measure([&] {
    Section section(book.epub, 0, book.renderer);
    if (!section.startBuild(book.wide)) return false;
    while (!section.isBuildComplete() && !section.buildReachedVisibleTextOffset(offset))
      if (!section.buildSomeMore(1)) return false;
    section.abandonBuild();
    return true;
  });
  ASSERT_TRUE(previewOk);
  ASSERT_TRUE(buildOk);
  std::cout << "PREVIEW_COST pages=" << pages << " page=" << pages * 9 / 10 << " preview_peak=" << previewPeak
            << " preview_calls=" << previewCalls << " preview_largest=" << previewLargest
            << " preview_ms=" << previewMs << " build_to_page_peak=" << buildPeak << " build_to_page_ms=" << buildMs
            << "\n";
  EXPECT_LT(previewMs, buildMs);
}

// A chapter entered by a turn starts as a few pages laid out on a quiet pass and suspended (the
// next-chapter look-ahead), and is laid out further from that partial. The resume points must
// cover the pages laid out after the partial too, or a text change deep in it has no point near.
TEST(PreviewPage, PointsGrowAcrossAResumedPartial) {
  PreviewBook book;
  uint16_t pages = 0;
  {
    Section first(book.epub, 0, book.renderer);
    ASSERT_TRUE(first.startBuild(book.narrow));
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(first.buildSomeMore(1));
    first.suspendBuild();
    ASSERT_TRUE(first.isPartial());
  }
  {
    Section resumed(book.epub, 0, book.renderer);
    ASSERT_TRUE(resumed.loadSectionFile(book.narrow));
    ASSERT_TRUE(resumed.isPartial());
    ASSERT_TRUE(resumed.startBuild(book.narrow));
    while (!resumed.isBuildComplete()) ASSERT_TRUE(resumed.buildSomeMore(8));
    pages = resumed.pageCount;
  }
  const auto raw = bytes(book.root / "sections/0.dd");
  ASSERT_GE(raw.size(), 16u);
  uint32_t count = 0;
  memcpy(&count, raw.data() + 8, 4);
  std::cout << "DD_RESUMED pages=" << pages << " points=" << count << "\n";
  EXPECT_GE(count, pages / 2u) << "points stop where the partial stopped";
}

TEST(AllocationProbe, ArrayDeleteGivesBackWhatArrayNewCounted) {
  allocationProbe::enabled = true;
  const long before = allocationProbe::live;
  char* volatile block = new char[64];
  delete[] block;
  const long after = allocationProbe::live;
  allocationProbe::enabled = false;
  EXPECT_EQ(after, before);
}
