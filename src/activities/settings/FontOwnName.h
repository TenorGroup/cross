#pragma once

#include <cstdint>

class GfxRenderer;
struct SdCardFontFamilyInfo;

namespace fontownname {

// Draws `name` in the face of the family's own .cpfont, the file nearest to `pointSize`, left edge at `x`, its
// baseline on `baselineY`, cut to `maxWidth`: the font of the reader's own on the card, shown as it will read.
// Loads the file, warms only the name's glyphs, draws, unloads: the card is read each time and nothing is kept, on
// it or in memory. False, with nothing drawn, when the family is a vector font, has no file, lacks a glyph of the
// name, or the heap would fall under the floor (heapfloor::kFreeFloor / kLargestFloor): the caller writes words.
bool draw(GfxRenderer& renderer, const SdCardFontFamilyInfo& family, const char* name, uint8_t pointSize, int x,
          int baselineY, int maxWidth, bool black);

}  // namespace fontownname
