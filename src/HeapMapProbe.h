#pragma once

// Probe builds only: prints every internal heap block (address, size, used) so a
// fragmented heap shows which live block splits the free space.
#if defined(TENOR_PRESS_PROBE) && !defined(SIMULATOR)
void heapMapDump(const char* tag);
#else
inline void heapMapDump(const char*) {}
#endif
