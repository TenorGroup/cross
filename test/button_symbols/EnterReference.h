#pragma once
#include <cstdint>
// Hand-drawn Enter (return) symbol, one literal copy per size: pixel grids drawn row by row, not derived from Back. Box 24x15 and 31x18: Back's head and shaft rows, a tail that climbs as far above the shaft as Back's falls below it.
namespace enterReference {
struct Glyph { const uint32_t* rows; int width; int height; };
constexpr uint32_t rows14[] = {0x00000002,0x00000007,0x00060007,0x000f0007,0x001f0007,0x003e0007,0x007c000f,0x00fffffe,0x00fffffc,0x00fffff8,0x007c0000,0x003c0000,0x001e0000,0x000f0000,0x00070000};
constexpr Glyph net14{rows14,24,15};
constexpr uint32_t rows18[] = {0x00000006,0x00e0000f,0x01e0000f,0x03e0000f,0x07e0000f,0x0fc0000f,0x1f80000f,0x3f00001f,0x7ffffffe,0x7ffffffc,0x7ffffff8,0x3ffffff0,0x1f800000,0x0fc00000,0x07e00000,0x03e00000,0x01e00000,0x00e00000};
constexpr Glyph net18{rows18,31,18};
}
