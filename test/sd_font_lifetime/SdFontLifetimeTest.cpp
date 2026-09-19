#include <EpdFontFamily.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <SdCardFont.h>
#include <Epub/blocks/TextBlock.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

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
struct Scene {
  SdCardFont font;
  HalDisplay display;
  GfxRenderer renderer{display};
  FontCacheManager cache{renderer.getFontMap(), renderer.getSdCardFonts()};
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
  } else {
    require(false, "unknown scenario");
  }
  std::printf("PASS %s\n", scenario.c_str());
}
