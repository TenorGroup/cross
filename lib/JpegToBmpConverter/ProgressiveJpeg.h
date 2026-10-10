#pragma once

#ifdef BOARD_HAS_PSRAM
#include <HalMemory.h>
class HalFile;

namespace progressivejpeg {
// The capability policy shared by cover decoding and cover cache versions.
bool supported();
HalMemory::PsramBuffer allocate(size_t bytes);
HalMemory::PsramBuffer decode(HalFile& file, int width, int height);
}  // namespace progressivejpeg
#endif
