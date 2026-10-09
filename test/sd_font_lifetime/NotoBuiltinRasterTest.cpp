#include <EpdFontFamily.h>
#include <HalStorage.h>
#include <GfxRenderer.h>
#include <SdCardFont.h>
#include <builtinFonts/notoserif_14_regular.h>
#include <builtinFonts/notoserif_14_bold.h>
#include <builtinFonts/notoserif_14_italic.h>
#include <builtinFonts/notoserif_14_bolditalic.h>

#include <cstdlib>
#include <fstream>
#include <iterator>

HalDisplay::HalDisplay() = default;
HalDisplay::~HalDisplay() = default;

void require(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  std::exit(1);
}

int main(int argc, char** argv) {
  require(argc == 2, "cpfont argument required");
  std::ifstream input(argv[1], std::ios::binary);
  require(input.good(), "cpfont fixture readable");
  Storage.files["/Noto.cpfont"] = std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
  const EpdFont regular(&notoserif_14_regular);
  const EpdFont bold(&notoserif_14_bold);
  const EpdFont italic(&notoserif_14_italic);
  const EpdFont boldItalic(&notoserif_14_bolditalic);
  const EpdFontFamily builtin(&regular, &bold, &italic, &boldItalic);
  SdCardFont font;
  require(font.load("/Noto.cpfont"), "production loader accepts Noto fixture");
  require(font.matchesBuiltinLayout(builtin), "full Noto layout matches builtin");
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.insertFont(777, builtin);
  EpdFontFamily raster(font.getEpdFont(0), font.getEpdFont(1), font.getEpdFont(2), font.getEpdFont(3));
  require(renderer.replaceBuiltinFont(777, raster), "builtin map node replaced in place");
  renderer.registerBuiltinRaster(777, &font);
  require(renderer.getFontMaxInkTop(777) == 0, "builtin first-line placement preserved");
  require(renderer.getLineHeight(777) == 40, "builtin line height preserved");
  renderer.removeFont(777);
  renderer.insertFont(777, builtin);
  require(renderer.getFontMap().at(777).getData() == &notoserif_14_regular, "builtin face restored");
  auto& bytes = Storage.files.at("/Noto.cpfont");
  const auto original = bytes;
  const auto offset = static_cast<uint32_t>(bytes[56]) | static_cast<uint32_t>(bytes[57]) << 8 |
                      static_cast<uint32_t>(bytes[58]) << 16 | static_cast<uint32_t>(bytes[59]) << 24;
  bytes[offset + 18 * 12 + 2] ^= 1;
  require(!font.matchesBuiltinLayout(builtin), "advance mismatch rejected");
  bytes = original;
  bytes[offset + 18 * 12 + 1070 * 16 + 2] ^= 1;
  require(!font.matchesBuiltinLayout(builtin), "kern class mismatch rejected");
  bytes = original;
  bytes[offset + 18 * 12 + 1070 * 16 + (381 + 448) * 3] ^= 1;
  require(!font.matchesBuiltinLayout(builtin), "kern matrix mismatch rejected");
  bytes = original;
  bytes[offset + 18 * 12 + 1070 * 16 + (381 + 448) * 3 + 70 * 65] ^= 1;
  require(!font.matchesBuiltinLayout(builtin), "ligature mismatch rejected");
  bytes = original;
  bytes[44] ^= 1;
  SdCardFont changed;
  require(changed.load("/Noto.cpfont"), "changed metrics remain valid cpfont");
  require(!changed.matchesBuiltinLayout(builtin), "metric mismatch rejected");
  std::puts("PASS Noto raster layout, advance, classes, matrix, ligatures and metrics");
}
