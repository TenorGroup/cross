#pragma once
// Never reached: the stats fixture is tenor/cross (shells/Shell.h).
class GfxRenderer;
namespace ugly {
enum class Size { S22, S30, S38, S52 };
inline int ascent(Size) { return 0; }
inline int text(const GfxRenderer&, Size, int, int, const char*, bool = true) { return 0; }
}  // namespace ugly
