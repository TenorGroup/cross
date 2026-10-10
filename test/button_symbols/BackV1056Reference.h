#pragma once
#include <InlineSymbolBitmaps.h>
// Exact Back rows and dimensions from v1.0.56 (470186c5).
namespace backV1056 {
using inlineSymbolBitmaps::Glyph;
constexpr uint32_t kBackNet14Rows[] = {
    0x00060000,
    0x000F0000,
    0x001F0000,
    0x003E0000,
    0x007C0000,
    0x00FFFFF8,
    0x00FFFFFE,
    0x00FFFFFE,
    0x007C000F,
    0x003C0007,
    0x001E0007,
    0x000F000F,
    0x000703FF,
    0x000007FE,
    0x000003FC,
};
constexpr Glyph kBackNet14{kBackNet14Rows, 24, 15};
constexpr uint32_t kBackNet18Rows[] = {
    0x00E00000,
    0x01E00000,
    0x03E00000,
    0x07E00000,
    0x0FC00000,
    0x1F800000,
    0x3F000060,
    0x7FFFFFF8,
    0x7FFFFFFC,
    0x7FFFFFFE,
    0x3FFFFFFF,
    0x1F80000F,
    0x0FC0000F,
    0x07E0000F,
    0x03E0001F,
    0x01E01FFF,
    0x00E01FFE,
    0x00001FFC,
    0x00001FF8,
};
constexpr Glyph kBackNet18{kBackNet18Rows, 31, 19};
}
