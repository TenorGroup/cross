#include <cassert>
#include <cstdio>
#include <FontDecompressor.h>
#include <builtinFonts/geist_14_regular.h>
#include <builtinFonts/geist_14_bold.h>
#include <builtinFonts/geist_16_regular.h>
#include <builtinFonts/geist_16_bold.h>
namespace reference {
#include <geist_14_regular-reference.h>
#include <geist_14_bold-reference.h>
#include <geist_16_regular-reference.h>
#include <geist_16_bold-reference.h>
}

void verify(const EpdFontData& actual, const EpdFontData& expected) {
  FontDecompressor decoder;
  assert(decoder.init());
  size_t checked = 0;
  for (uint32_t i = 0; i < actual.intervalCount; ++i) {
    const auto& range = actual.intervals[i];
    for (uint32_t cp = range.first; cp <= range.last; ++cp) {
      const auto index = range.offset + cp - range.first;
      const auto& glyph = actual.glyph[index];
      const auto& original = expected.glyph[index];
      assert(glyph.width == original.width && glyph.height == original.height);
      if (!glyph.width || !glyph.height) continue;
      const auto* decoded = decoder.getBitmap(&actual, &glyph, index);
      assert(decoded);
      const auto* packed = expected.bitmap + original.dataOffset;
      for (unsigned pixel = 0; pixel < glyph.width * glyph.height; ++pixel) {
        const unsigned expectedInk = ((packed[pixel/8] >> (7-pixel%8)) & 1) * 3;
        assert(((decoded[pixel/4] >> (6-2*(pixel%4))) & 3) == expectedInk);
      }
      assert(decoder.getStats().hotGroupBytes <= 2048);
      ++checked;
    }
  }
  std::printf("PASS: %zu nonempty glyphs, max active group <=2048 bytes\n", checked);
}
int main() {
  verify(geist_14_regular, reference::geist_14_regular);
  verify(geist_14_bold, reference::geist_14_bold);
  verify(geist_16_regular, reference::geist_16_regular);
  verify(geist_16_bold, reference::geist_16_bold);
}
