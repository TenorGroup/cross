#pragma once
#include <cstdint>
// Hand-drawn Enter (return) symbol, one literal copy per size: pixel grids drawn row by row, not derived from Back.
namespace enterReference {
struct Glyph { const uint32_t* rows; int width; int height; };
constexpr uint32_t rows14[] = {0x00000002,0x00000007,0x00018007,0x0003c007,0x0007c007,0x000f8007,0x001f000f,0x003ffffe,0x003ffffc,0x003ffff8,0x001f0000,0x000f8000,0x0007c000,0x0003c000,0x00018000};
constexpr Glyph net14{rows14,22,15};
constexpr uint32_t rows18[] = {0x00000006,0x0000000f,0x0038000f,0x0078000f,0x00f8000f,0x01f8000f,0x03f0000f,0x07e0000f,0x0fc0001f,0x1ffffffe,0x1ffffffc,0x1ffffff8,0x0ffffff0,0x07e00000,0x03f00000,0x01f80000,0x00f80000,0x00780000,0x00380000};
constexpr Glyph net18{rows18,29,19};
}
