// The X3's tenor/ink path (JPEGDEC, no PSRAM) on the covers of the idle step's test: the split-DC one
// fails, so Home has no temporary cover for it; the ordinary one gives its 1/8 grid cover.
#include <HalStorage.h>
#include <JpegToBmpConverter.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

EspClass ESP;
namespace heapfloor {
bool judge(size_t, const void*, const char*) { return true; }
void* take(size_t n, const char*) { return std::malloc(n); }
}  // namespace heapfloor

int main() {
  struct Sink : Print {
    size_t bytes = 0;
    size_t write(uint8_t) override { return ++bytes, 1; }
    size_t write(const uint8_t*, size_t n) override { return bytes += n, n; }
  };
  for (const char* name : {"dcsplit.jpg", "cover.jpg"}) {
    std::ifstream in(std::string(FIXTURES) + "/" + name, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
    HalFile file(bytes);
    Sink out;
    const bool ok = JpegToBmpConverter::jpegFileToInkBmpStream(file, out, 96, 140, false);
    assert(ok == (name[0] == 'c'));
  }
  std::puts("GREEN the X3's JPEGDEC path fails on the split-DC cover and draws the ordinary one");
  return 0;
}
