#include <cstdlib>
#include <cstddef>

size_t brandDecoderAllocations = 0;
void* brandDecoderMalloc(size_t bytes) {
  ++brandDecoderAllocations;
  return std::malloc(bytes);
}
void brandDecoderFree(void* memory) { std::free(memory); }

#define malloc brandDecoderMalloc
#define free brandDecoderFree
#include "../../lib/InflateReader/InflateReader.cpp"
