#include <Epub/Page.h>
#include <Serialization.h>
#include <HalMemory.h>
#include <GfxRenderer.h>
#include <HeapFloor.h>
#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>
#include <cstddef>
#include <new>

namespace pageAllocProbe {
inline bool enabled = false;
inline size_t limit = SIZE_MAX, live = 0, peak = 0, rejected = 0, epoch = 0;
struct alignas(std::max_align_t) Header { size_t bytes, epoch; };
void* allocate(size_t bytes) {
  if (enabled && bytes > limit) { ++rejected; return nullptr; }
  auto* p = static_cast<Header*>(std::malloc(sizeof(Header) + bytes));
  if (!p) return nullptr;
  *p = {bytes, enabled ? epoch : 0};
  if (enabled) { live += bytes; peak = std::max(peak, live); }
  return p + 1;
}
void release(void* ptr) {
  if (!ptr) return;
  auto* p = static_cast<Header*>(ptr) - 1;
  if (enabled && p->epoch == epoch) live -= p->bytes;
  std::free(p);
}
void start(size_t maximum = SIZE_MAX) { ++epoch; live = peak = rejected = 0; limit = maximum; enabled = true; }
}
void* operator new(size_t bytes) { if (auto* p = pageAllocProbe::allocate(bytes)) return p; throw std::bad_alloc(); }
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void* operator new(size_t bytes, const std::nothrow_t&) noexcept { return pageAllocProbe::allocate(bytes); }
void* operator new[](size_t bytes, const std::nothrow_t&) noexcept { return pageAllocProbe::allocate(bytes); }
void operator delete(void* p) noexcept { pageAllocProbe::release(p); }
void operator delete[](void* p) noexcept { pageAllocProbe::release(p); }
void operator delete(void* p, size_t) noexcept { pageAllocProbe::release(p); }
void operator delete[](void* p, size_t) noexcept { pageAllocProbe::release(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { pageAllocProbe::release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { pageAllocProbe::release(p); }

namespace {
Page textPage() {
  Page page;
  auto block = std::make_unique<TextBlock>(
      std::vector<std::string>{"hello", "world"}, std::vector<int16_t>{0, 40},
      std::vector<EpdFontFamily::Style>{EpdFontFamily::REGULAR, EpdFontFamily::REGULAR},
      std::vector<uint8_t>{}, std::vector<uint16_t>{}, BlockStyle(), std::vector<std::string>{"ruby", ""});
  page.elements.push_back(std::make_unique<PageLine>(std::move(block), 10, 20));
  page.elements.push_back(std::make_unique<PageHorizontalRule>(80, 1, 0, 40));
  page.elements.push_back(std::make_unique<PageImage>(std::make_unique<ImageBlock>("/cache/picture.png", "Images/picture.png", 100, 120), 0, 60));
  page.addFootnote("1", "chapter.xhtml#note");
  page.addLink("chapter2.xhtml", 2, 3, 40, 10);
  return page;
}
HalFile validFile() {
  HalFile file;
  EXPECT_TRUE(textPage().serialize(file));
  file.offset = 0;
  return file;
}
}

TEST(CacheSerialization, RoundTripRetainsTextRubyAndLinks) {
  auto file = validFile();
  auto page = Page::deserialize(file);
  ASSERT_NE(page, nullptr);
  ASSERT_EQ(page->elements.size(), 3u);
  const auto* text = static_cast<PageLine*>(page->elements[0].get())->getBlock();
  ASSERT_EQ(text->wordCount(), 2);
  EXPECT_STREQ(text->wordText(0), "hello");
  ASSERT_EQ(text->getRubyCount(), 1u);
  EXPECT_EQ(text->getRubyText(0), "ruby");
  EXPECT_EQ(page->footnotes.size(), 1u);
  EXPECT_EQ(page->links.size(), 1u);
}

TEST(CacheSerialization, TruncationAtEveryByteRejectsPage) {
  const auto valid = validFile().bytes;
  for (size_t cut = 0; cut < valid.size(); ++cut) {
    HalFile file;
    file.bytes.assign(valid.begin(), valid.begin() + cut);
    EXPECT_EQ(Page::deserialize(file), nullptr) << "cut=" << cut;
  }
}

TEST(CacheSerialization, NegativeReadAtEveryFieldRejectsPage) {
  auto complete = validFile();
  ASSERT_NE(Page::deserialize(complete), nullptr);
  for (size_t field = 0; field < complete.calls; ++field) {
    auto file = validFile();
    file.failAtCall = field;
    EXPECT_EQ(Page::deserialize(file), nullptr) << "read call=" << field;
  }
}

TEST(CacheSerialization, MissingStringBodyNeverAllocatesClaimedLength) {
  HalFile file;
  const uint32_t length = 1024 * 1024;
  serialization::writePod(file, length);
  file.offset = 0;
  std::string result;
  serialization::readString(file, result);
  EXPECT_TRUE(result.empty());
  EXPECT_LT(result.capacity(), 1024u);
}

TEST(CacheSerialization, ShortWriteOfAnyFieldFailsWholePage) {
  // POD headers and style used to ignore a short write, allowing a corrupt page commit.
  HalFile file;
  file.writeLimit = 0;
  Page page;
  page.elements.push_back(std::make_unique<PageHorizontalRule>(80, 1, 0, 40));
  EXPECT_FALSE(page.serialize(file));
}

TEST(CacheSerialization, OversizedRubyRejectsWholePageWithoutTruncation) {
  auto page = textPage();
  auto oversized = std::make_unique<TextBlock>(
      std::vector<std::string>{"base"}, std::vector<int16_t>{0},
      std::vector<EpdFontFamily::Style>{EpdFontFamily::REGULAR},
      std::vector<uint8_t>{}, std::vector<uint16_t>{}, BlockStyle(),
      std::vector<std::string>{std::string(2048, 'r')});
  page.elements[0] = std::make_unique<PageLine>(std::move(oversized), 0, 0);
  HalFile file;
  ASSERT_TRUE(page.serialize(file));
  // Page count/tag/x/y, text scalars, then the one-word arena for "base".
  constexpr size_t rubyLengthAt = 2 + 1 + 4 + 8 + 10;
  const uint32_t oversizedLength = 2049;
  std::memcpy(file.bytes.data() + rubyLengthAt, &oversizedLength, sizeof(oversizedLength));
  file.bytes.insert(file.bytes.begin() + rubyLengthAt + 4 + 2048, 'r');
  file.offset = 0;
  EXPECT_EQ(Page::deserialize(file), nullptr);
}

TEST(CacheSerialization, CountExceedingLargestHeapBlockRejectsPage) {
  Page page;
  for (int i = 0; i < 40; ++i) page.elements.push_back(std::make_unique<PageHorizontalRule>(80, 1, 0, i));
  HalFile file;
  ASSERT_TRUE(page.serialize(file));
  file.offset = 0;
  const size_t saved = HalMemory::largestBlockBytes;
  HalMemory::largestBlockBytes = 128;
  const auto result = Page::deserialize(file);
  HalMemory::largestBlockBytes = saved;
  EXPECT_EQ(result, nullptr);
}

TEST(CacheSerialization, InvalidFocusAndBoolMetadataRejectsPage) {
  auto file = validFile();
  // count(2), tag(1), x/y(4), wordCount(2), focus(1)
  file.bytes[9] = 2;
  EXPECT_EQ(Page::deserialize(file), nullptr);
}

// A chapter build wrote each line of text as one write per field and one per word for its empty
// ruby lengths, each through the storage lock: some 700 small writes a page (X3 r40, 15 ms a page).
// A line now takes a handful of writes, and it reads back field for field.
TEST(CacheSerialization, TextLineGoesOutInAFewWritesAndReadsBackWhole) {
  std::vector<std::string> words;
  std::vector<int16_t> xs;
  for (int i = 0; i < 40; ++i) {
    words.push_back("w" + std::to_string(i));
    xs.push_back(static_cast<int16_t>(i * 12));
  }
  BlockStyle style;
  style.alignment = CssTextAlign::Right;
  style.textAlignDefined = true;
  style.marginTop = 1; style.marginBottom = 2; style.marginLeft = 3; style.marginRight = 4;
  style.paddingTop = 5; style.paddingBottom = 6; style.paddingLeft = 7; style.paddingRight = 8;
  style.textIndent = -9; style.textIndentDefined = true; style.isRtl = true; style.directionDefined = true;
  Page page;
  page.elements.push_back(std::make_unique<PageLine>(
      std::make_unique<TextBlock>(words, xs, std::vector<EpdFontFamily::Style>(40, EpdFontFamily::BOLD),
                                  std::vector<uint8_t>{}, std::vector<uint16_t>{}, style),
      10, 20));
  HalFile file;
  ASSERT_TRUE(page.serialize(file));
  EXPECT_LE(file.writeCalls, 16u) << "a line of 40 words took " << file.writeCalls << " writes";
  file.offset = 0;
  const auto back = Page::deserialize(file);
  ASSERT_NE(back, nullptr);
  EXPECT_EQ(file.offset, file.bytes.size());
  const auto* text = static_cast<PageLine*>(back->elements[0].get())->getBlock();
  ASSERT_EQ(text->wordCount(), 40);
  EXPECT_STREQ(text->wordText(39), "w39");
  EXPECT_EQ(text->getRubyCount(), 0u);
  const auto& got = text->getBlockStyle();
  EXPECT_EQ(got.alignment, CssTextAlign::Right);
  EXPECT_TRUE(got.textAlignDefined);
  EXPECT_EQ(got.marginTop, 1); EXPECT_EQ(got.marginBottom, 2); EXPECT_EQ(got.marginLeft, 3);
  EXPECT_EQ(got.marginRight, 4); EXPECT_EQ(got.paddingTop, 5); EXPECT_EQ(got.paddingBottom, 6);
  EXPECT_EQ(got.paddingLeft, 7); EXPECT_EQ(got.paddingRight, 8); EXPECT_EQ(got.textIndent, -9);
  EXPECT_TRUE(got.textIndentDefined);
  EXPECT_TRUE(got.isRtl);
  EXPECT_TRUE(got.directionDefined);
}

TEST(CacheSerialization, ShortWriteAtEveryFieldFailsWholePage) {
  const Page page = textPage();
  HalFile complete;
  ASSERT_TRUE(page.serialize(complete));
  for (size_t field = 0; field < complete.writeCalls; ++field) {
    HalFile file;
    file.failAtWriteCall = field;
    EXPECT_FALSE(page.serialize(file)) << "write call=" << field;
  }
}

TEST(CacheSerialization, MetadataRefusalStillLoadsTextAndConsumesRecord) {
  auto page = textPage();
  while (page.footnotes.size() < Page::MAX_FOOTNOTES_PER_PAGE) page.addFootnote("n", "chapter.xhtml#note");
  while (page.links.size() < Page::MAX_LINKS_PER_PAGE) page.addLink("chapter2.xhtml", 2, 3, 40, 10);
  HalFile file;
  ASSERT_TRUE(page.serialize(file));
  file.offset = 0;
  const auto saved = HalMemory::largestBlockBytes;
  HalMemory::largestBlockBytes = 8192;
  const auto result = Page::deserialize(file);
  HalMemory::largestBlockBytes = saved;
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->elements.size(), 3u);
  EXPECT_TRUE(result->footnotes.empty());
  EXPECT_TRUE(result->links.empty());
  EXPECT_EQ(file.offset, file.bytes.size());
}

TEST(CacheSerialization, UnknownTagIsCorruptButShortReadIsRetryable) {
  auto file = validFile();
  file.bytes[2] = 255;
  EXPECT_EQ(Page::deserialize(file), nullptr);
  EXPECT_EQ(Page::loadFailure, Page::LoadFailure::Corrupt);
  file = validFile();
  file.failAtCall = 0;
  EXPECT_EQ(Page::deserialize(file), nullptr);
  EXPECT_EQ(Page::loadFailure, Page::LoadFailure::Unknown);
}

TEST(CacheSerialization, MetadataOomHasNoThrowingAllocation) {
  auto page = textPage();
  while (page.footnotes.size() < Page::MAX_FOOTNOTES_PER_PAGE) page.addFootnote("n", "chapter.xhtml#note");
  while (page.links.size() < Page::MAX_LINKS_PER_PAGE) page.addLink("chapter2.xhtml", 2, 3, 40, 10);
  HalFile file;
  ASSERT_TRUE(page.serialize(file));
  for (size_t cap : {SIZE_MAX, size_t(4096)}) {
    file.offset = 0;
    pageAllocProbe::start(cap);
    std::unique_ptr<Page> result;
    bool threw = false;
    try { result = Page::deserialize(file); } catch (const std::bad_alloc&) { threw = true; }
    const auto peak = pageAllocProbe::peak;
    const auto rejected = pageAllocProbe::rejected;
    pageAllocProbe::enabled = false;
    std::cout << "PAGE_HEAP cap=" << cap << " peak=" << peak << " refused=" << rejected << '\n';
    EXPECT_FALSE(threw);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->elements.size(), 3u);
    EXPECT_EQ(result->footnotes.size(), cap == SIZE_MAX ? 16u : 0u);
    EXPECT_EQ(result->links.size(), cap == SIZE_MAX ? 32u : 0u);
  }
}

TEST(CacheSerialization, SparseRubyUsesSpaceForActualAnnotations) {
  std::vector<std::string> words(10000, "a"), ruby(10000);
  ruby.back() = "reading";
  TextBlock block(words, std::vector<int16_t>(10000),
                  std::vector<EpdFontFamily::Style>(10000, EpdFontFamily::REGULAR), {}, {}, BlockStyle(), ruby);
  HalFile file;
  ASSERT_TRUE(block.serialize(file));
  for (size_t cap : {SIZE_MAX, size_t(100000)}) {
    file.offset = 0;
    pageAllocProbe::start(cap);
    std::unique_ptr<TextBlock> result;
    bool threw = false;
    try { result = TextBlock::deserialize(file); } catch (const std::bad_alloc&) { threw = true; }
    const auto peak = pageAllocProbe::peak;
    pageAllocProbe::enabled = false;
    std::cout << "RUBY_LOAD words=10000 ruby=1 cap=" << cap << " peak=" << peak << '\n';
    EXPECT_FALSE(threw);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->wordCount(), 10000);
    HalFile roundTrip;
    ASSERT_TRUE(result->serialize(roundTrip));
    EXPECT_EQ(roundTrip.bytes, file.bytes);
  }
}

TEST(CacheSerialization, RubyRenderingHasBoundedWorkspace) {
  std::vector<std::string> words(10000, "a"), ruby(10000);
  ruby.back() = "reading";
  TextBlock block(words, std::vector<int16_t>(10000),
                  std::vector<EpdFontFamily::Style>(10000, EpdFontFamily::REGULAR), {}, {}, BlockStyle(), ruby);
  GfxRenderer renderer;
  pageAllocProbe::start();
  block.render(renderer, 0, 0, 100);
  const auto peak = pageAllocProbe::peak;
  pageAllocProbe::enabled = false;
  std::cout << "RUBY_RENDER words=10000 ruby=1 peak=" << peak << '\n';
  EXPECT_LT(peak, 4096u);
}

TEST(CacheSerialization, RubyRefusalRetainsWordsAndLineShift) {
  TextBlock block({"base"}, {0}, {EpdFontFamily::REGULAR}, {}, {}, BlockStyle(), {"annotation"});
  HalFile file;
  ASSERT_TRUE(block.serialize(file));
  file.offset = 0;
  heapfloor::refuseRuby = true;
  auto result = TextBlock::deserialize(file);
  heapfloor::refuseRuby = false;
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->wordCount(), 1);
  EXPECT_STREQ(result->wordText(0), "base");
  EXPECT_EQ(result->getRubyCount(), 0u);
  EXPECT_EQ(result->getRubyShift(20), block.getRubyShift(20));
  EXPECT_EQ(file.offset, file.bytes.size());
}

TEST(CacheSerialization, RubyCountHasLineLimit) {
  TextBlock block(std::vector<std::string>(200, "base"), std::vector<int16_t>(200),
                  std::vector<EpdFontFamily::Style>(200, EpdFontFamily::REGULAR), {}, {}, BlockStyle(),
                  std::vector<std::string>(200, "reading"));
  EXPECT_EQ(block.wordCount(), 200);
  EXPECT_EQ(block.getRubyCount(), 128u);
  EXPECT_EQ(block.getRubyShift(20), 10);
}

TEST(CacheSerialization, RubyConstructionRefusalKeepsCachedLayoutConsistent) {
  heapfloor::refuseRuby = true;
  TextBlock block({"base"}, {0}, {EpdFontFamily::REGULAR}, {}, {}, BlockStyle(), {"annotation"});
  heapfloor::refuseRuby = false;
  ASSERT_TRUE(block.valid());
  EXPECT_EQ(block.getRubyCount(), 0u);
  HalFile file;
  ASSERT_TRUE(block.serialize(file));
  file.offset = 0;
  auto cached = TextBlock::deserialize(file);
  ASSERT_NE(cached, nullptr);
  EXPECT_EQ(cached->getRubyShift(20), block.getRubyShift(20));
  EXPECT_STREQ(cached->wordText(0), "base");
}

TEST(CacheSerialization, RubyBytesHaveLineLimit) {
  TextBlock block(std::vector<std::string>(5, "base"), std::vector<int16_t>(5),
                  std::vector<EpdFontFamily::Style>(5, EpdFontFamily::REGULAR), {}, {}, BlockStyle(),
                  {std::string(2047, 'a'), std::string(2047, 'b'), std::string(2047, 'c'),
                   std::string(2047, 'd'), "e"});
  EXPECT_EQ(block.getRubyCount(), 4u);
  EXPECT_EQ(block.getRubyText(3).size(), 2047u);
  EXPECT_TRUE(block.getRubyText(4).empty());
  EXPECT_EQ(block.wordCount(), 5u);
  HalFile file;
  ASSERT_TRUE(block.serialize(file));
  file.offset = 0;
  auto cached = TextBlock::deserialize(file);
  ASSERT_NE(cached, nullptr);
  EXPECT_EQ(cached->getRubyCount(), 4u);
}
