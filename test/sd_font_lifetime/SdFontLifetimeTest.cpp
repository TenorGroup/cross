#include <EpdFontFamily.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <SdCardFont.h>
#include <Epub/blocks/TextBlock.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace allocationProbe {
bool enabled = false;
size_t limit = std::numeric_limits<size_t>::max();
size_t liveBudget = std::numeric_limits<size_t>::max();
size_t failMin = std::numeric_limits<size_t>::max();
size_t failMax = 0;
bool failOnce = false;
size_t largestAttempt = 0;
size_t rejectedSize = 0;
unsigned rejected = 0;
size_t liveBytes = 0, peakBytes = 0;
uint64_t generation = 0;
struct alignas(std::max_align_t) Header { size_t size; uint64_t generation; };
void reset() {
  enabled = false;
  limit = liveBudget = failMin = std::numeric_limits<size_t>::max();
  failMax = 0;
  failOnce = false;
  largestAttempt = rejectedSize = 0;
  rejected = 0;
  liveBytes = peakBytes = 0;
  ++generation;
}
void* allocate(size_t size) {
  if (enabled) {
    largestAttempt = std::max(largestAttempt, size);
    if (size > limit || size > liveBudget - liveBytes ||
        (failOnce && size >= failMin && size <= failMax)) {
      failOnce = false;
      rejectedSize = size;
      ++rejected;
      return nullptr;
    }
  }
  const size_t bytes = size ? size : 1;
  if (bytes > std::numeric_limits<size_t>::max() - sizeof(Header)) return nullptr;
  auto* header = static_cast<Header*>(std::malloc(sizeof(Header) + bytes));
  if (!header) return nullptr;
  header->size = size;
  header->generation = enabled ? generation : 0;
  if (enabled) {
    liveBytes += size;
    peakBytes = std::max(peakBytes, liveBytes);
  }
  return header + 1;
}
void release(void* value) noexcept {
  if (!value) return;
  auto* header = static_cast<Header*>(value) - 1;
  if (enabled && header->generation == generation) liveBytes -= header->size;
  std::free(header);
}
}
void* operator new[](std::size_t size) {
  if (void* value = allocationProbe::allocate(size)) return value;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  return allocationProbe::allocate(size);
}
void operator delete[](void* value) noexcept { allocationProbe::release(value); }
void operator delete[](void* value, std::size_t) noexcept { allocationProbe::release(value); }
void operator delete[](void* value, const std::nothrow_t&) noexcept { allocationProbe::release(value); }

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}
std::vector<uint8_t> pixels(HalDisplay::BUFFER_SIZE, 0xff);
void u16(std::vector<uint8_t>& data, size_t at, uint16_t value) {
  data[at] = value & 0xff;
  data[at + 1] = value >> 8;
}
void u32(std::vector<uint8_t>& data, size_t at, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) data[at + i] = value >> (8 * i);
}
template <typename T> void append(std::vector<uint8_t>& bytes, const T& value) {
  const auto* begin = reinterpret_cast<const uint8_t*>(&value);
  bytes.insert(bytes.end(), begin, begin + sizeof(value));
}
std::vector<uint8_t> makeFont(bool regularLig, bool boldLig, size_t* regularLigOffset = nullptr) {
  std::vector<uint8_t> bytes(96, 0);
  std::memcpy(bytes.data(), "CPFONT\0\0", 8);
  u16(bytes, 8, CPFONT_VERSION);
  bytes[12] = 2;
  for (uint8_t style = 0; style < 2; ++style) {
    const size_t toc = 32 + style * 32;
    const bool hasLig = style == 0 ? regularLig : boldLig;
    bytes[toc] = style;
    u32(bytes, toc + 4, 5);
    u32(bytes, toc + 8, 5);
    bytes[toc + 12] = 12;
    u16(bytes, toc + 13, 10);
    u16(bytes, toc + 15, uint16_t(-2));
    bytes[toc + 23] = hasLig ? 1 : 0;
    u32(bytes, toc + 24, bytes.size());
    const uint32_t cps[] = {' ', 'f', 'i', 0xfb01, 0xfffd};
    for (uint32_t i = 0; i < 5; ++i) append(bytes, EpdUnicodeInterval{cps[i], cps[i], i});
    const uint8_t widths[] = {2, uint8_t(4 + style * 2), uint8_t(3 + style * 2), uint8_t(6 + style * 2), 4};
    for (uint32_t i = 0; i < 5; ++i) {
      append(bytes, EpdGlyph{widths[i], 1, uint16_t(widths[i] * 16), 0, 1, 1, i});
    }
    if (style == 0 && regularLigOffset) *regularLigOffset = bytes.size();
    if (hasLig) append(bytes, EpdLigaturePair{('f' << 16) | 'i', 0xfb01});
    bytes.insert(bytes.end(), 5, 0xff);
  }
  return bytes;
}
uint16_t advanceFor(uint32_t cp) { return uint16_t((2 + cp % 13) * 16); }
void appendUtf8(std::string& text, uint32_t cp) {
  if (cp < 0x80) text += char(cp);
  else if (cp < 0x800) {
    text += char(0xc0 | (cp >> 6));
    text += char(0x80 | (cp & 0x3f));
  } else {
    text += char(0xe0 | (cp >> 12));
    text += char(0x80 | ((cp >> 6) & 0x3f));
    text += char(0x80 | (cp & 0x3f));
  }
}
std::vector<uint8_t> makeAdvanceFont() {
  std::vector<uint32_t> cps{' ', '-', '.', ',', 0xfffd};
  for (uint32_t cp = 'A'; cp <= 'Z'; ++cp) cps.push_back(cp);
  for (uint32_t cp = 'a'; cp <= 'z'; ++cp) cps.push_back(cp);
  for (uint32_t cp : {0x00e1u, 0x0103u, 0x0111u, 0x01a1u, 0x01b0u, 0x1ebfu, 0x1ec7u, 0x1ef3u}) cps.push_back(cp);
  for (uint32_t cp = 0x4e00; cp < 0x4e00 + 160; ++cp) cps.push_back(cp);
  std::sort(cps.begin(), cps.end());
  cps.erase(std::unique(cps.begin(), cps.end()), cps.end());
  std::vector<uint8_t> bytes(64, 0);
  std::memcpy(bytes.data(), "CPFONT\0\0", 8);
  u16(bytes, 8, CPFONT_VERSION);
  bytes[12] = 1;
  bytes[32] = 0;
  u32(bytes, 36, cps.size());
  u32(bytes, 40, cps.size());
  bytes[44] = 18;
  u16(bytes, 45, 14);
  u16(bytes, 47, uint16_t(-3));
  u32(bytes, 56, bytes.size());
  for (uint32_t i = 0; i < cps.size(); ++i) append(bytes, EpdUnicodeInterval{cps[i], cps[i], i});
  for (uint32_t i = 0; i < cps.size(); ++i) {
    append(bytes, EpdGlyph{uint8_t(advanceFor(cps[i]) / 16), 1, advanceFor(cps[i]), 0, 1, 1, i});
  }
  bytes.insert(bytes.end(), cps.size(), 0xff);
  return bytes;
}
void loadAdvanceFont(SdCardFont& font) {
  Storage.files["/advance.cpfont"] = makeAdvanceFont();
  require(font.load("/advance.cpfont"), "load advance cpfont fixture");
}
std::string utf8Text(const std::vector<uint32_t>& cps) {
  std::string text;
  for (uint32_t cp : cps) appendUtf8(text, cp);
  return text;
}
void checkAdvances(const SdCardFont& font, const std::vector<uint32_t>& cps) {
  for (uint32_t cp : cps) require(font.getAdvance(cp, 0) == advanceFor(cp), "advance matches cpfont glyph metric");
}
// Word batches go through the packed API the renderer uses: one segment of
// consecutive NUL-terminated words, with the space slot reserved only when
// there is more than one word (the renderer's own rule).
int buildWordAdvances(SdCardFont& font, const std::deque<std::string>& words, bool includeHyphen, uint8_t styleMask,
                      const char* extraText) {
  std::string packed;
  for (const std::string& word : words) packed.append(word).push_back('\0');
  const char* segment = packed.data();
  const size_t segmentLen = packed.size();
  return font.buildAdvanceTablePacked(&segment, &segmentLen, 1, words.size() > 1, includeHyphen, styleMask,
                                      extraText);
}
uint64_t advanceDigest(const SdCardFont& font, const std::vector<uint32_t>& cps) {
  uint64_t digest = 0;
  for (uint32_t cp : cps) digest = digest * 131 + font.getAdvance(cp, 0);
  return digest;
}
void testAdvanceLowHeap() {
  const std::vector<uint32_t> phrase{'L', 'a', 't', 'i', 'n', ' ', 0x00e1, 0x0103, 0x0111, 0x01a1,
                                     0x01b0, 0x1ebf, 0x1ec7, 0x1ef3, '.', 'f', 'i'};
  std::string paragraph;
  for (int i = 0; i < 250; ++i) paragraph += utf8Text(phrase);
  std::deque<std::string> words{paragraph, paragraph};
  const std::string extra = utf8Text({'-', 'A', 'z'});
  SdCardFont reference, constrained;
  loadAdvanceFont(reference);
  loadAdvanceFont(constrained);
  require(reference.buildAdvanceTable(paragraph.c_str(), 1, extra.c_str()) == 0, "reference paragraph builds");
  require(buildWordAdvances(reference, words, true, 1, extra.c_str()) == 0, "reference word batch builds");
  const std::vector<uint32_t> expected{'L', 'a', 't', 'i', 'n', ' ', 0x00e1, 0x0103, 0x0111, 0x01a1,
                                        0x01b0, 0x1ebf, 0x1ec7, 0x1ef3, '.', 'f', 'i', '-', 'A', 'z'};
  for (int repeat = 0; repeat < 3; ++repeat) {
    allocationProbe::reset();
    allocationProbe::limit = 4096;
    allocationProbe::enabled = true;
    const int plainResult = constrained.buildAdvanceTable(paragraph.c_str(), 1, extra.c_str());
    const int wordsResult = buildWordAdvances(constrained, words, true, 1, extra.c_str());
    allocationProbe::enabled = false;
    std::printf("ADVANCE_LOW_HEAP repeat=%d plain=%d words=%d largest_array=%zu rejected=%u rejected_size=%zu\n",
                repeat, plainResult, wordsResult, allocationProbe::largestAttempt,
                allocationProbe::rejected, allocationProbe::rejectedSize);
    require(plainResult == 0 && wordsResult == 0, "low-heap advance build succeeds for both overloads");
    require(allocationProbe::rejected == 0, "all temporary array allocations fit 4096-byte block");
    require(constrained.hasAdvanceTable(), "low-heap advance table populated");
    checkAdvances(constrained, expected);
    for (uint32_t cp : expected) {
      require(constrained.getAdvance(cp, 0) == reference.getAdvance(cp, 0), "low-heap metrics match reference");
    }
    constrained.clearCache();
  }
  allocationProbe::reset();
}
void testAdvanceCjk() {
  SdCardFont font;
  loadAdvanceFont(font);
  std::vector<uint32_t> cps;
  for (uint32_t cp = 0x4e00; cp < 0x4e00 + 160; ++cp) cps.push_back(cp);
  std::string first = utf8Text(cps);
  HalFile::readCalls = 0;
  require(font.buildAdvanceTable(first.c_str(), 1) == 0, "160 unique CJK advances build");
  const size_t firstReads = HalFile::readCalls;
  require(firstReads >= cps.size(), "cold CJK build reads glyph metrics");
  checkAdvances(font, cps);
  const uint64_t digest = advanceDigest(font, cps);
  std::vector<uint32_t> shuffled;
  for (size_t i = 0; i < cps.size(); ++i) shuffled.push_back(cps[(i * 37) % cps.size()]);
  const std::string reordered = utf8Text(shuffled) + utf8Text(shuffled);
  HalFile::readCalls = 0;
  require(font.buildAdvanceTable(reordered.c_str(), 1) == 0, "repeated shuffled CJK advances build");
  require(HalFile::readCalls == 0, "warm CJK build avoids SD reads");
  checkAdvances(font, shuffled);
  require(advanceDigest(font, cps) == digest, "CJK metrics survive shuffled warm build");
  std::printf("ADVANCE_CJK unique=160 cold_reads=%zu warm_reads=%zu metric_digest=%llu\n",
              firstReads, HalFile::readCalls, static_cast<unsigned long long>(digest));
}
void testAdvanceCap() {
  std::string filler;
  for (uint32_t cp = 0x5000; cp < 0x5000 + 4095; ++cp) {
    appendUtf8(filler, cp);
    if ((cp - 0x5000) % 512 == 0) appendUtf8(filler, cp);
  }
  const std::string included = utf8Text({0x4e10});
  const std::string excluded = utf8Text({0x4e00});
  SdCardFont plain;
  loadAdvanceFont(plain);
  require(plain.buildAdvanceTable((filler + included + included + excluded).c_str(), 1) == 0,
          "4096-unique cap build succeeds");
  require(plain.getAdvance(0x4e10, 0) == advanceFor(0x4e10), "4096th unique marker is retained");
  require(plain.getAdvance(0x4e00, 0) == 0, "4097th unique marker is excluded");
  require(plain.getAdvance(0x5000, 0) == advanceFor(0xfffd), "first missing filler uses replacement metric");
  require(plain.getAdvance(0x5000 + 4094, 0) == 0, "persistent cache retains only first 768 sorted entries");

  SdCardFont batch;
  loadAdvanceFont(batch);
  std::deque<std::string> words{filler, filler.substr(0, 3)};
  const std::string extra = included + included + excluded;
  require(buildWordAdvances(batch, words, true, 1, extra.c_str()) == 0, "cap with extra text builds");
  require(batch.getAdvance(0x4e10, 0) == advanceFor(0x4e10), "extra text fills 4096th input slot");
  require(batch.getAdvance(0x4e00, 0) == 0, "extra text excludes 4097th unique input");
  require(batch.getAdvance(' ', 0) == advanceFor(' '), "space uses reserved cap slot");
  require(batch.getAdvance('-', 0) == advanceFor('-'), "hyphen uses reserved cap slot");
  std::printf("ADVANCE_CAP unique_input=4096 excluded=4097 persistent_limit=768 included_advance=%u space_advance=%u hyphen_advance=%u\n",
              batch.getAdvance(0x4e10, 0), batch.getAdvance(' ', 0), batch.getAdvance('-', 0));
}
void testAdvanceGrowthFailure() {
  SdCardFont font;
  loadAdvanceFont(font);
  require(font.buildAdvanceTable("a", 1) == 0, "seed cached advance");
  const uint16_t saved = font.getAdvance('a', 0);
  std::vector<uint32_t> cps;
  for (uint32_t cp = 0x4e00; cp < 0x4e00 + 160; ++cp) cps.push_back(cp);
  const std::string text = utf8Text(cps);
  HalFile::readCalls = 0;
  allocationProbe::reset();
  allocationProbe::failMin = 600;
  allocationProbe::failMax = 4096;
  allocationProbe::failOnce = allocationProbe::enabled = true;
  const int result = font.buildAdvanceTable(text.c_str(), 1);
  allocationProbe::enabled = false;
  std::printf("ADVANCE_GROWTH_FAIL result=%d rejected=%u rejected_size=%zu sd_reads=%zu\n",
              result, allocationProbe::rejected, allocationProbe::rejectedSize, HalFile::readCalls);
  require(result == -1 && allocationProbe::rejected == 1, "temporary growth failure returns -1");
  require(HalFile::readCalls == 0, "growth failure stops before SD reads");
  require(font.getAdvance('a', 0) == saved, "growth failure preserves cached advance");
  require(font.getAdvance(0x4e00, 0) == 0, "failed growth publishes no partial CJK table");
  allocationProbe::reset();
  require(font.buildAdvanceTable(text.c_str(), 1) == 0, "growth retry succeeds");
  require(font.getAdvance('a', 0) == saved, "growth retry preserves old metric");
  checkAdvances(font, cps);
}
void testAdvanceGrowthPeak() {
  SdCardFont font;
  loadAdvanceFont(font);
  std::vector<uint32_t> cps;
  std::string reverse;
  for (uint32_t cp = 0x5000; cp < 0x5000 + 4096; ++cp) cps.push_back(cp);
  for (auto it = cps.rbegin(); it != cps.rend(); ++it) appendUtf8(reverse, *it);
  require(font.buildAdvanceTable(reverse.c_str(), 1) == 0, "cold reverse4096 fills advance cache");
  require(font.getAdvance(0x5000 + 767, 0) == advanceFor(0xfffd), "persistent cache has 768 entries");
  require(font.getAdvance(0x5000 + 768, 0) == 0, "persistent cache stops at 768 entries");
  const uint64_t digest = advanceDigest(font, cps);
  HalFile::readCalls = 0;
  allocationProbe::reset();
  allocationProbe::enabled = true;
  const int uncappedResult = font.buildAdvanceTable(reverse.c_str(), 1);
  const size_t uncappedPeak = allocationProbe::peakBytes;
  const size_t uncappedLive = allocationProbe::liveBytes;
  allocationProbe::enabled = false;
  require(uncappedResult == 0 && uncappedLive == 0, "uncapped warm collection succeeds without temporary leaks");
  require(HalFile::readCalls == 0, "uncapped warm collection avoids SD reads");
  require(advanceDigest(font, cps) == digest, "uncapped warm metric digest stays unchanged");
  HalFile::readCalls = 0;
  allocationProbe::reset();
  allocationProbe::liveBudget = 16392;
  allocationProbe::enabled = true;
  const int result = font.buildAdvanceTable(reverse.c_str(), 1);
  const size_t live = allocationProbe::liveBytes;
  const size_t peak = allocationProbe::peakBytes;
  allocationProbe::enabled = false;
  std::printf("ADVANCE_GROWTH_PEAK uncapped_peak=%zu uncapped_live=%zu result=%d budget=16392 "
              "budget_peak=%zu live=%zu rejected=%u rejected_size=%zu warm_reads=%zu metric_digest=%llu\n",
              uncappedPeak, uncappedLive, result, peak, live, allocationProbe::rejected, allocationProbe::rejectedSize,
              HalFile::readCalls, static_cast<unsigned long long>(advanceDigest(font, cps)));
  require(uncappedPeak <= 16392, "uncapped temporary growth peak exceeds baseline");
  require(result == 0 && allocationProbe::rejected == 0, "warm reverse4096 fits live allocation budget");
  require(peak <= 16392 && live == 0, "budgeted temporary growth peak or lifetime exceeds budget");
  require(HalFile::readCalls == 0, "full persistent cache avoids warm SD reads");
  require(advanceDigest(font, cps) == digest, "warm reverse4096 metric digest stays unchanged");
  allocationProbe::reset();
}
struct Scene {
  SdCardFont font;
  HalDisplay display;
  GfxRenderer renderer{display};
  FontCacheManager cache{renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts()};
  Scene(bool regularLig, bool boldLig) {
    Storage.files["/fixture.cpfont"] = makeFont(regularLig, boldLig);
    require(font.load("/fixture.cpfont"), "load complete cpfont fixture");
    renderer.begin();
    renderer.insertFont(42, EpdFontFamily(font.getEpdFont(0), font.getEpdFont(1)));
    renderer.registerSdCardFont(42, &font);
    renderer.setFontCacheManager(&cache);
    require(font.prewarm("fi", 3) == 0, "prewarm both actual font styles");
  }
  void scanAndDraw(bool annotation) {
    // Exercise the same real TextBlock scan used when dictionary Back releases
    // SD caches, before PrewarmScope restores the ligatures for the draw pass.
    TextBlock block({annotation ? "f" : "fi"}, {20}, {EpdFontFamily::BOLD}, {}, {}, BlockStyle{},
                    {annotation ? "fi" : "f"});
    require(block.valid() && block.hasRuby(), "actual TextBlock has ruby");
    auto scope = cache.createPrewarmScope();
    require(renderer.isFontCacheScanning(), "real prewarm scan enabled");
    block.render(renderer, 42, 10, 20);
    scope.endScanAndPrewarm();
    require(!renderer.isFontCacheScanning(), "real prewarm scan ended");
    block.render(renderer, 42, 10, 20);
    require(std::any_of(pixels.begin(), pixels.end(), [](uint8_t value) { return value != 0xff; }),
            "actual renderer drew glyph pixels");
  }
  void assertReleased() {
    for (uint8_t style = 0; style < 2; ++style) {
      const auto* data = font.getEpdFont(style)->data;
      require(data->ligaturePairs == nullptr && data->ligaturePairCount == 0, "released alias is empty");
      int w = 0, h = 0;
      font.getEpdFont(style)->getTextDimensions("fi", &w, &h);
      require(w > 0 && h > 0, "font remains measurable on demand after release");
    }
  }
};
}  // namespace

// The display boundary supplies a RAM framebuffer. Actual GfxRenderer renders
// into it; unused panel operations are discarded by the linker.
HalDisplay::HalDisplay() = default;
HalDisplay::~HalDisplay() = default;
uint8_t* HalDisplay::getFrameBuffer() const { return pixels.data(); }
uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }
uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }
uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }

int main(int argc, char** argv) {
  require(argc == 2, "one scenario argument required");
  const std::string scenario = argv[1];
  if (scenario == "ruby-base" || scenario == "ruby-annotation") {
    const bool annotation = scenario == "ruby-annotation";
    Scene scene(annotation, !annotation);
    const auto& family = scene.renderer.getFontMap().at(42);
    const auto style = annotation ? EpdFontFamily::SUP : EpdFontFamily::BOLD;
    require(family.getData(style) == scene.font.getEpdFont(annotation ? 0 : 1)->data,
            "ruby/base style resolves to selected SD style");
    require(family.getData(style)->ligaturePairCount == 1, "resolved SD style has a live ligature");
    require(scene.renderer.getTextAdvanceX(42, "fi", style) == (annotation ? 3 : 8),
            "real renderer measurement consumed fi ligature before release");
    scene.cache.releaseSdFontCaches();
    // This must run before checking aliases so ASan reports the actual stale
    // lookup inside the ruby/base scan on the original implementation.
    scene.scanAndDraw(annotation);
    require(scene.font.getEpdFont(annotation ? 0 : 1)->getLigature('f', 'i') == 0xfb01,
            "render prewarm restored ligature table");
  } else if (scenario == "repeat-release") {
    Scene scene(true, true);
    for (int iteration = 0; iteration < 25; ++iteration) {
      scene.cache.releaseSdFontCaches();
      scene.cache.releaseSdFontCaches();
      scene.scanAndDraw(iteration % 2);
      scene.cache.releaseSdFontCaches();
      scene.assertReleased();
      require(scene.font.prewarm("fi", 3) == 0, "reload after release");
    }
  } else if (scenario == "failed-read" || scenario == "failed-seek") {
    Scene scene(true, true);
    size_t ligOffset = 0;
    Storage.files["/fixture.cpfont"] = makeFont(true, true, &ligOffset);
    scene.cache.releaseSdFontCaches();
    if (scenario == "failed-read") HalFile::shortReadAt = ligOffset;
    else HalFile::failSeekAt = ligOffset;
    scene.font.prewarm("fi", 1);
    int width = 0, height = 0;
    scene.font.getEpdFont(0)->getTextDimensions("fi", &width, &height);
    scene.assertReleased();
    HalFile::shortReadAt = HalFile::failSeekAt = std::numeric_limits<size_t>::max();
    require(scene.font.prewarm("fi", 3) == 0, "reload succeeds after injected I/O failure");
    require(scene.font.getEpdFont(0)->getLigature('f', 'i') == 0xfb01, "successful retry republishes ligature");
  } else if (scenario == "failed-reload") {
    Scene scene(true, true);
    auto* heldFont = scene.font.getEpdFont(0);
    require(!scene.font.load("/missing.cpfont"), "missing font reload fails");
    require(heldFont->getLigature('f', 'i') == 0, "held exposed font has no expired alias after failed reload");
    require(heldFont->data->ligaturePairs == nullptr && heldFont->data->ligaturePairCount == 0,
            "failed full reload cleared exposed aliases");
    require(scene.font.load("/fixture.cpfont"), "load succeeds after failed full reload");
    require(scene.font.prewarm("fi", 3) == 0, "prewarm after full reload");
    scene.scanAndDraw(false);
  } else if (scenario == "advance-low-heap") {
    testAdvanceLowHeap();
  } else if (scenario == "advance-cjk") {
    testAdvanceCjk();
  } else if (scenario == "advance-cap") {
    testAdvanceCap();
  } else if (scenario == "advance-growth-failure") {
    testAdvanceGrowthFailure();
  } else if (scenario == "advance-growth-peak") {
    testAdvanceGrowthPeak();
  } else {
    require(false, "unknown scenario");
  }
  std::printf("PASS %s\n", scenario.c_str());
}
