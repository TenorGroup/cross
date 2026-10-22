#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace fontownname {

// A name in the letters of a font of the card, as a 1-bit picture the list draws like a store's (fontstore::NAME_IMAGES):
// rows of ceil(width / 8) bytes, MSB first, 1 = ink, cut to the ink on every side. baseline: the rows from the top down to
// the line the letters stand on. xHeight: the rows of the font's lower-case x at the size drawn (0 when it has none).
struct Picture {
  std::unique_ptr<uint8_t[]> bits;
  size_t size = 0;  // bytes in `bits`
  uint16_t width = 0, height = 0;
  int16_t baseline = 0;
  uint8_t xHeight = 0;
};

// Draws `name` into `out` in the face of the .cpfont at `path`, at most `maxWidth` wide (the name is cut there) and
// `maxHeight` high. Loads the file, warms the name's glyphs, rasterizes them (2-bit glyphs at the first level of grey and
// up, the cut gen_font_names.py makes for the store's pictures), unloads: the card is read once and nothing of the font
// stays in memory. False, with `out` untouched, when the file does not load, lacks a letter of the name (a space is
// never a reason: it is a gap), the name is taller than `maxHeight`, or the heap would fall under the floor
// (heapfloor::kFreeFloor / kLargestFloor): the caller writes words.
bool render(const char* path, const char* name, int maxWidth, int maxHeight, Picture& out);

}  // namespace fontownname
