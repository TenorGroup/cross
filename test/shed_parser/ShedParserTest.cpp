// A chapter laid out with the SD font caches shed between build steps must come out byte for
// byte as the same chapter laid out without a shed. The production Section, ChapterHtmlSlimParser,
// ParsedText, GfxRenderer, FontCacheManager and SdCardFont run unchanged over a synthetic
// .cpfont (4 styles, kerning classes, bitmaps) on the host file system.
//
// What is shed is exactly what EpubReaderActivity::releaseHeapForBuild() sheds:
// FontCacheManager::releaseSdFontCaches(). Starvation is driven through Section's own heap check
// (ESP.getFreeHeap() on the k-th call), so the shed lands where production lands it: at the top
// of a parse step, after Section parked the build (parser destroyed, checkpoint on disk) and
// before the next buildSomeMore() resumes it.
#include <Epub/Page.h>
#include <Epub/Section.h>
#include <EpdFontFamily.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <SdCardFont.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

constexpr int FONT_ID = 42;

[[noreturn]] void fail(const std::string& message) {
  std::fprintf(stderr, "FAIL: %s\n", message.c_str());
  std::exit(1);
}
void require(bool condition, const std::string& message) {
  if (!condition) fail(message);
}

std::vector<uint8_t> pixels(HalDisplay::BUFFER_SIZE, 0xff);

// ---- synthetic .cpfont ------------------------------------------------------------------

void u16(std::vector<uint8_t>& data, size_t at, uint16_t value) {
  data[at] = value & 0xff;
  data[at + 1] = value >> 8;
}
void u32(std::vector<uint8_t>& data, size_t at, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) data[at + i] = value >> (8 * i);
}
template <typename T>
void append(std::vector<uint8_t>& bytes, const T& value) {
  const auto* begin = reinterpret_cast<const uint8_t*>(&value);
  bytes.insert(bytes.end(), begin, begin + sizeof(value));
}

std::vector<uint32_t> fontCodepoints() {
  std::vector<uint32_t> cps{' ', ',', '-', '.', 0xfffd};
  for (uint32_t cp = 'A'; cp <= 'Z'; ++cp) cps.push_back(cp);
  for (uint32_t cp = 'a'; cp <= 'z'; ++cp) cps.push_back(cp);
  std::sort(cps.begin(), cps.end());
  return cps;
}
bool isLetter(uint32_t cp) { return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'); }
// 12.4 advance with a fractional part, so summing before rounding and rounding per glyph differ.
uint16_t advanceFor(uint32_t cp, uint8_t style) {
  return uint16_t((3 + (cp * 7) % 6 + (style & 1)) * 16 + (cp * 5) % 16);
}

std::vector<uint8_t> makeFont() {
  constexpr uint8_t STYLES = 4;
  const auto cps = fontCodepoints();
  std::vector<uint8_t> bytes(32 + 32 * STYLES, 0);
  std::memcpy(bytes.data(), "CPFONT\0\0", 8);
  u16(bytes, 8, CPFONT_VERSION);
  bytes[12] = STYLES;
  constexpr int LEFT_CLASSES = 4, RIGHT_CLASSES = 4;
  for (uint8_t style = 0; style < STYLES; ++style) {
    const size_t toc = 32 + style * 32;
    size_t letters = 0;
    for (uint32_t cp : cps) letters += isLetter(cp);
    bytes[toc] = style;
    u32(bytes, toc + 4, cps.size());
    u32(bytes, toc + 8, cps.size());
    bytes[toc + 12] = 14;
    u16(bytes, toc + 13, 11);
    u16(bytes, toc + 15, uint16_t(-3));
    u16(bytes, toc + 17, letters);
    u16(bytes, toc + 19, letters);
    bytes[toc + 21] = LEFT_CLASSES;
    bytes[toc + 22] = RIGHT_CLASSES;
    bytes[toc + 23] = 0;
    u32(bytes, toc + 24, bytes.size());
    for (uint32_t i = 0; i < cps.size(); ++i) append(bytes, EpdUnicodeInterval{cps[i], cps[i], i});
    uint32_t offset = 0;
    for (uint32_t cp : cps) {
      const uint8_t width = uint8_t(advanceFor(cp, style) / 16);
      const uint16_t length = uint16_t(((width + 7) / 8) * 2);
      append(bytes, EpdGlyph{width, 2, advanceFor(cp, style), 0, 2, length, offset});
      offset += length;
    }
    for (uint32_t cp : cps)
      if (isLetter(cp)) append(bytes, EpdKernClassEntry{uint16_t(cp), uint8_t(1 + cp % LEFT_CLASSES)});
    for (uint32_t cp : cps)
      if (isLetter(cp)) append(bytes, EpdKernClassEntry{uint16_t(cp), uint8_t(1 + (cp / 4) % RIGHT_CLASSES)});
    for (int l = 0; l < LEFT_CLASSES; ++l)
      for (int r = 0; r < RIGHT_CLASSES; ++r) bytes.push_back(uint8_t(int8_t(((l * 5 + r * 3) % 7 - 3) * 8)));
    for (uint32_t cp : cps) {
      const uint8_t width = uint8_t(advanceFor(cp, style) / 16);
      const size_t length = ((width + 7) / 8) * 2;
      for (size_t i = 0; i < length; ++i) bytes.push_back(uint8_t(0x55 + cp + i));
    }
  }
  return bytes;
}

// ---- chapter ----------------------------------------------------------------------------

std::string chapterHtml() {
  uint32_t seed = 12345;
  const auto next = [&seed](uint32_t n) {
    seed = seed * 1664525u + 1013904223u;
    return (seed >> 8) % n;
  };
  const auto word = [&]() {
    std::string w;
    const uint32_t length = 2 + next(8);
    for (uint32_t i = 0; i < length; ++i) w += char((i == 0 && next(5) == 0 ? 'A' : 'a') + next(26));
    if (next(9) == 0) w += ',';
    return w;
  };
  std::string html = "<html><body>";
  for (int paragraph = 0; paragraph < 130; ++paragraph) {
    if (paragraph % 17 == 0) html += "<h2>" + word() + " " + word() + "</h2>";
    html += "<p>";
    const uint32_t words = 8 + next(70);
    for (uint32_t i = 0; i < words; ++i) {
      const uint32_t style = next(14);
      const std::string w = word();
      if (style == 0) html += "<b>" + w + "</b>";
      else if (style == 1) html += "<i>" + w + "</i>";
      else if (style == 2) html += "<b><i>" + w + "</i></b>";
      else html += w;
      // Some tokens run straight into the next one (no space): their gap is a kerning pair,
      // which is where resident kern tables could change a line.
      html += (i + 1 == words) ? "." : (next(4) == 0 ? "" : " ");
    }
    html += "</p>";
  }
  return html + "</body></html>";
}

// ---- one build --------------------------------------------------------------------------

struct Scene {
  HalDisplay display;
  SdCardFont font;
  GfxRenderer renderer{display};
  FontCacheManager cache{renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts()};
  Scene() {
    Storage.mkdir("/tmp");
    std::ofstream out(path(), std::ios::binary | std::ios::trunc);
    const auto bytes = makeFont();
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    require(font.load(path().c_str()), "load cpfont fixture");
    renderer.begin();
    renderer.insertFont(FONT_ID, EpdFontFamily(font.getEpdFont(0), font.getEpdFont(1), font.getEpdFont(2),
                                              font.getEpdFont(3)));
    renderer.registerSdCardFont(FONT_ID, &font);
    renderer.setFontCacheManager(&cache);
  }
  ~Scene() { std::filesystem::remove(path()); }
  static std::string path() { return "/tmp/shed-parser-" + std::to_string(getpid()) + ".cpfont"; }
  // What a reader that has shown a page leaves behind: the glyph arenas, the mini kern tables
  // and the advance table of the previous chapter.
  void warm() {
    const std::string text = "Warm up text Reading the first page, with all the letters, jumpy fox quiz vex";
    require(font.prewarm(text.c_str(), 0x0F) == 0, "warm prewarm");
    require(font.buildAdvanceTable(text.c_str(), 0x0F) == 0, "warm advance table");
  }
};

struct Result {
  std::string bin;
  uint64_t renderHash = 0;
  unsigned pages = 0;
  unsigned sheds = 0;
  unsigned starved = 0;
  unsigned parkedAtShed = 0;  // shed while the parser was destroyed (park succeeded)
  uint64_t heapCalls = 0;
};

std::string readAll(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

enum class Mode {
  Plain,       // nothing shed
  Cold,        // caches released once before the build, nothing after
  EveryTick,   // shed after every tick, parser alive
  StarveAt,    // starve on call k, shed, resume (production path)
  StarveEvery  // starve on every kth call, shed each time
};

struct Run {
  Mode mode = Mode::Plain;
  uint64_t k = 0;
  int pagesPerTick = 1;
  bool warm = true;
};

Result build(const Run& run, const std::string& html) {
  Scene scene;
  if (run.warm) scene.warm();
  if (run.mode == Mode::Cold) scene.cache.releaseSdFontCaches();

  const auto root = std::filesystem::temp_directory_path() / ("shed-parser-" + std::to_string(getpid()));
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  auto epub = std::make_shared<Epub>();
  epub->cachePath = root.string();
  epub->contents = html;
  ReaderRenderSpec spec;
  spec.fontId = FONT_ID;
  spec.viewportWidth = 300;
  spec.viewportHeight = 220;
  spec.embeddedStyle = false;
  spec.dropCapMode = 0;

  Result result;
  ESP = EspHostStub{};
  {
    Section section(epub, 0, scene.renderer);
    require(section.startBuild(spec), "start build");
    ESP.calls = 0;
    if (run.mode == Mode::StarveAt) ESP.starveAt = run.k;
    if (run.mode == Mode::StarveEvery) ESP.starveEvery = run.k;
    unsigned guard = 0;
    while (!section.isBuildComplete()) {
      require(++guard < 100000, "build did not finish");
      if (!section.buildSomeMore(run.pagesPerTick)) {
        // Same loop as EpubReaderActivity: a starved build asks for heap, then tries again.
        require(section.buildStarved(), "build failed for a reason other than starvation");
        ++result.starved;
        if (section.isBuildParked()) ++result.parkedAtShed;
        scene.cache.releaseSdFontCaches();  // releaseHeapForBuild(): font caches go first
        ++result.sheds;
        continue;
      }
      if (run.mode == Mode::EveryTick) {
        scene.cache.releaseSdFontCaches();
        ++result.sheds;
      }
    }
    result.heapCalls = ESP.calls;
    ESP = EspHostStub{};
    result.pages = section.pageCount;
    // Every page drawn through the real prewarm scope, as renderContents() does.
    uint64_t hash = 1469598103934665603ull;
    for (unsigned page = 0; page < section.pageCount; ++page) {
      const auto drawn = section.loadPage(static_cast<int>(page));
      require(static_cast<bool>(drawn), "load page " + std::to_string(page));
      std::fill(pixels.begin(), pixels.end(), 0xff);
      auto scope = scene.cache.createPrewarmScope();
      drawn->render(scene.renderer, FONT_ID, 0, 0);
      scope.endScanAndPrewarm();
      drawn->render(scene.renderer, FONT_ID, 0, 0);
      for (uint8_t byte : pixels) hash = (hash ^ byte) * 1099511628211ull;
    }
    result.renderHash = hash;
  }
  result.bin = readAll(root / "sections/0.bin");
  std::filesystem::remove_all(root);
  require(!result.bin.empty(), "section file written");
  return result;
}

const char* modeName(Mode mode) {
  switch (mode) {
    case Mode::Plain: return "plain";
    case Mode::Cold: return "cold";
    case Mode::EveryTick: return "every-tick";
    case Mode::StarveAt: return "starve-at";
    case Mode::StarveEvery: return "starve-every";
  }
  return "?";
}

bool same(const Result& a, const Result& b) { return a.bin == b.bin && a.renderHash == b.renderHash; }

void report(const Run& run, const Result& result, const Result& reference) {
  std::printf("SHED_PARSER mode=%s k=%llu ppt=%d pages=%u sheds=%u bin=%zu hash=%016llx %s\n", modeName(run.mode),
              static_cast<unsigned long long>(run.k), run.pagesPerTick, result.pages, result.sheds,
              result.bin.size(), static_cast<unsigned long long>(result.renderHash),
              same(result, reference) ? "SAME"
                                      : (result.bin != reference.bin ? "DIFFERENT(bin)" : "DIFFERENT(render)"));
}

}  // namespace

HalDisplay::HalDisplay() = default;
HalDisplay::~HalDisplay() = default;
uint8_t* HalDisplay::getFrameBuffer() const { return pixels.data(); }
uint8_t* HalDisplay::lendFrameBufferStorage(uint32_t* sizeOut) {
  if (sizeOut) *sizeOut = BUFFER_SIZE;
  return pixels.data();
}
void HalDisplay::returnFrameBufferStorage() {}
uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }
uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }
uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }

int main(int argc, char** argv) {
  const std::string scenario = argc > 1 ? argv[1] : "";
  const std::string html = chapterHtml();
  const Result plain = build({Mode::Plain, 0, 1, true}, html);
  require(plain.pages > 40, "chapter is long enough: " + std::to_string(plain.pages) + " pages");
  std::printf("SHED_PARSER reference pages=%u bin=%zu heap_calls=%llu hash=%016llx\n", plain.pages, plain.bin.size(),
              static_cast<unsigned long long>(plain.heapCalls), static_cast<unsigned long long>(plain.renderHash));
  const auto expectSame = [&](const Run& run) {
    const Result result = build(run, html);
    report(run, result, plain);
    if (run.mode != Mode::Plain && run.mode != Mode::Cold)
      require(result.sheds > 0, std::string(modeName(run.mode)) + ": no shed happened, the run proves nothing");
    require(same(result, plain), std::string(modeName(run.mode)) + " k=" + std::to_string(run.k) +
                                     ": pages differ from the build without a shed");
  };

  if (scenario == "premise") {
    // The cache state the build can see really differs before and after a shed.
    Scene scene;
    scene.warm();
    const std::string text = "Warm up text Reading the first page, with all the letters, jumpy fox quiz vex";
    const auto kernSum = [&]() {
      int sum = 0, nonzero = 0;
      for (const char a : text)
        for (const char b : text) {
          const int kern = scene.renderer.getFontMap().at(FONT_ID).getKerning(a, b, EpdFontFamily::REGULAR);
          sum += kern;
          nonzero += kern != 0;
        }
      return std::pair<int, int>(sum, nonzero);
    };
    const auto warmKern = kernSum();
    require(warmKern.second > 0, "warm font kerns some pairs");
    for (const char a : text)
      for (const char b : text)
        require(scene.renderer.getKerning(FONT_ID, a, b, EpdFontFamily::REGULAR) == 0,
                "layout kerning of an SD font does not read resident kern tables");
    require(scene.font.hasAdvanceTable(), "warm font has an advance table");
    scene.cache.releaseSdFontCaches();
    const auto shedKern = kernSum();
    require(shedKern.second == 0, "a shed font kerns nothing");
    require(!scene.font.hasAdvanceTable(), "a shed font has no advance table");
    std::printf("SHED_PARSER premise warm_kern_pairs=%d shed_kern_pairs=%d\n", warmKern.second, shedKern.second);
  } else if (scenario == "determinism") {
    // Two plain builds agree, and the cold start agrees with the warm one: the baseline itself
    // does not move with cache residency, so any later difference is the shed.
    expectSame({Mode::Plain, 0, 1, true});
    expectSame({Mode::Cold, 0, 1, true});
    expectSame({Mode::Plain, 0, 1, false});
  } else if (scenario == "every-tick") {
    for (int ppt : {1, 2, 5}) expectSame({Mode::EveryTick, 0, ppt, true});
  } else if (scenario == "starve-sweep") {
    // Starve at every step boundary the build has (the k-th heap query), one run each.
    const uint64_t total = plain.heapCalls;
    unsigned runs = 0, parked = 0;
    for (uint64_t k = 1; k <= total; ++k) {
      const Run run{Mode::StarveAt, k, 1, true};
      const Result result = build(run, html);
      report(run, result, plain);
      require(result.sheds == 1, "starve-at: exactly one shed expected at k=" + std::to_string(k));
      require(same(result, plain), "starve-at k=" + std::to_string(k) + ": pages differ from the build without a shed");
      parked += result.parkedAtShed;
      ++runs;
    }
    std::printf("SHED_PARSER sweep runs=%u heap_calls=%llu shed_with_parser_destroyed=%u shed_with_parser_alive=%u\n",
                runs, static_cast<unsigned long long>(total), parked, runs - parked);
    // Both ends of production's retry loop are covered: Section parked the build before the shed,
    // and (a failed park) kept the parser resident through it.
    require(parked > 0, "no shed landed on a parked build");
  } else if (scenario == "starve-every") {
    for (uint64_t k : {3, 5, 7, 11, 23}) expectSame({Mode::StarveEvery, k, 1, true});
    for (uint64_t k : {3, 7}) expectSame({Mode::StarveEvery, k, 3, true});
  } else {
    fail("unknown scenario " + scenario);
  }
  std::printf("PASS %s\n", scenario.c_str());
}
