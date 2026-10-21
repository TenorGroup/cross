#include <cstdlib>
#include <iostream>

#include "FontStoreNames.h"
#include "components/UIScale.h"

namespace {
int bad = 0;
void expect(const bool ok, const char* what, const char* pack, const int tier) {
  if (ok || bad++ >= 8) return;
  std::cerr << what << ": " << pack << " tier " << tier << '\n';
}
}  // namespace

// The lower-case x of every picture is as tall as the UI face's (within 1 px, and the spread between fonts within 1 px),
// except a font whose size factor hit the 0.85..1.20 clamp: it is listed, not failed. No picture is taller than a name
// line of the UI, so no row grows.
// Every pack of the store catalog has a name picture at each of the three UI text sizes: whole, inside its block of
// bits with the next picture right after it, cut to its ink, with the baseline inside it.
int main() {
  const size_t blockSize[] = {sizeof(fontstore::NAME_BITS_0), sizeof(fontstore::NAME_BITS_1), sizeof(fontstore::NAME_BITS_2)};
  const uint8_t* blockStart[] = {fontstore::NAME_BITS_0, fontstore::NAME_BITS_1, fontstore::NAME_BITS_2};
  const uint8_t* const xHeight[] = {fontstore::NAME_XHEIGHT_0, fontstore::NAME_XHEIGHT_1, fontstore::NAME_XHEIGHT_2};
  const uint16_t* const scale[] = {fontstore::NAME_SCALE_0, fontstore::NAME_SCALE_1, fontstore::NAME_SCALE_2};
  for (int tier = 0; tier < 3; ++tier) {
    const int ui = fontstore::NAME_XHEIGHT_UI[tier];
    int lo = 255, hi = 0;
    expect(fontstore::NAME_POINTS[tier] == uiTextSizeSpec(static_cast<uint8_t>(tier)).bodyPointSize, "point size", "", tier);
    size_t used = 0;
    for (size_t i = 0; i < fontstore::PACK_COUNT; ++i) {
      const char* pack = fontstore::PACKS[i].name;
      const auto& image = fontstore::NAME_IMAGES[tier][i];
      const auto& bitmap = image.bitmap;
      expect(static_cast<bool>(bitmap) && bitmap.format == freeink::ui::BitmapFormat::BW1, "no picture", pack, tier);
      const size_t stride = (bitmap.width + 7u) / 8u, size = stride * bitmap.height;
      expect(bitmap.data == blockStart[tier] + used && used + size <= blockSize[tier], "picture off its place", pack, tier);
      if (!bitmap || used + size > blockSize[tier]) break;
      expect(bitmap.width <= 300, "wider than the name column", pack, tier);
      expect(bitmap.height <= uiTextSizeSpec(static_cast<uint8_t>(tier)).bodyLineHeight, "taller than a name line", pack, tier);
      const int x = xHeight[tier][i];
      if (scale[tier][i] >= 1195 || scale[tier][i] <= 855) {  // at the clamp: the size could not follow the UI x-height
        if (x != ui) std::cout << "x-height clamped: " << pack << " tier " << tier << " x " << x << " (UI " << ui << ")\n";
      } else {
        expect(x >= ui - 1 && x <= ui + 1, "x-height off the UI's by more than 1 px", pack, tier);
        lo = x < lo ? x : lo;
        hi = x > hi ? x : hi;
      }
      expect(image.baseline > 0 && image.baseline <= bitmap.height, "baseline outside", pack, tier);
      bool firstRow = false, lastRow = false, padBits = false;
      for (size_t x = 0; x < stride; ++x) {
        firstRow |= bitmap.data[x] != 0;
        lastRow |= bitmap.data[size - stride + x] != 0;
      }
      const unsigned pad = bitmap.width % 8 ? 0xFFu >> (bitmap.width % 8) : 0u;  // the bits past the width
      for (size_t y = 0; y < bitmap.height; ++y) padBits |= (bitmap.data[y * stride + stride - 1] & pad) != 0;
      expect(firstRow && lastRow, "not cut to the ink", pack, tier);
      expect(!padBits, "ink past the width", pack, tier);
      used += size;
    }
    expect(used == blockSize[tier], "bits left over", "", tier);
    expect(hi - lo <= 1, "x-height spread between fonts over 1 px", "", tier);
  }
  if (bad) {
    std::cerr << "FAIL: " << bad << " problems\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
