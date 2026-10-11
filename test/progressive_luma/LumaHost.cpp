// Host driver for the progressive luma decoder (run.py feeds it generated and damaged files).
//   decode <in> <out.pgm>   open + run, every row checked to arrive once and in order
//   sniff <in>              the frame kind, fed in 7-byte pieces as a zip stream would give it
//   stop <in> <n>           stop answers yes at its n-th question
//   refuse <in> <door>      the heap floor door refuses that block
#include <HalStorage.h>
#include <HeapFloor.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <vector>

#include "ProgressiveLuma.h"

namespace {
size_t live = 0;
const char* refuseWho = nullptr;
const char* name(const progluma::Result r) {
  static const char* names[] = {"Done", "Stopped", "NoHeap", "Corrupt", "Unsupported", "Io"};
  return names[static_cast<int>(r)];
}
}  // namespace

namespace heapfloor {
bool judge(size_t, const void*, const char* who) { return !refuseWho || std::strcmp(who, refuseWho) != 0; }
void* take(size_t bytes, const char*) { return std::malloc(bytes); }
}  // namespace heapfloor

// Live bytes of the nothrow allocations (every block the decoder takes).
// The nothrow allocations are the firmware's blocks (makeUniqueNoThrow, heapfloor::array): only
// those count; the test's own containers use the plain forms.
void* allocate(size_t n, bool counted) {
  auto* p = static_cast<size_t*>(std::malloc(n + 16));
  if (!p) return nullptr;
  p[0] = counted ? n : 0;
  live += p[0];
  return p + 2;
}
void* operator new(size_t n, const std::nothrow_t&) noexcept { return allocate(n, true); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return allocate(n, true); }
void* operator new(size_t n) {
  void* p = allocate(n, false);
  if (!p) std::abort();
  return p;
}
void* operator new[](size_t n) { return operator new(n); }
void operator delete(void* p) noexcept {
  if (!p) return;
  auto* head = static_cast<size_t*>(p) - 2;
  live -= head[0];
  std::free(head);
}
void operator delete[](void* p) noexcept { operator delete(p); }
void operator delete(void* p, size_t) noexcept { operator delete(p); }
void operator delete[](void* p, size_t) noexcept { operator delete(p); }

int main(int argc, char** argv) {
  if (argc < 3) return 2;
  const std::string mode = argv[1];
  std::ifstream in(argv[2], std::ios::binary);
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
  if (mode == "sniff") {
    progluma::Sniffer sniffer;
    for (size_t at = 0; at < bytes.size() && !sniffer.feed(bytes.data() + at, std::min<size_t>(7, bytes.size() - at)); at += 7) {
    }
    static const char* kinds[] = {"Baseline", "Progressive", "Unsupported", "Corrupt"};
    std::printf("kind=%s w=%d h=%d\n", kinds[static_cast<int>(sniffer.kind())], sniffer.width(), sniffer.height());
    return 0;
  }
  if (mode == "refuse") refuseWho = argv[3];
  HalFile file(bytes);
  struct Run {
    progluma::Decoder decoder;
    FILE* out = nullptr;
    int next = 0, asks = 0, stopAt = -1;
    uint32_t lastAsk = 0, maxGap = 0, atYes = 0;
  } run;
  if (mode == "stop") run.stopAt = std::atoi(argv[3]);
  progluma::Sink sink;
  sink.user = &run;
  sink.row = [](void* user, const uint8_t* pixels, const int y) {
    auto& r = *static_cast<Run*>(user);
    assert(y == r.next++);
    if (r.out) std::fwrite(pixels, 1, r.decoder.width(), r.out);
    return true;
  };
  sink.stop = [](void* user) {
    auto& r = *static_cast<Run*>(user);
    const uint32_t blocks = r.decoder.blocks();
    if (blocks - r.lastAsk > r.maxGap) r.maxGap = blocks - r.lastAsk;
    r.lastAsk = blocks;
    if (++r.asks != r.stopAt) return false;
    r.atYes = blocks;
    return true;
  };
  const size_t before = live;
  const auto opened = run.decoder.open(file, sink);
  // Bytes the decoder holds after open (its own blocks only).
  std::printf("open=%s w=%d h=%d live=%zu read=%zu asks=%d\n", name(opened), run.decoder.width(), run.decoder.height(),
              live - before, file.bytesRead, run.asks);
  if (opened != progluma::Result::Done) return 0;
  if (mode == "decode") {
    run.out = std::fopen(argv[3], "wb");
    std::fprintf(run.out, "P5\n%d %d\n255\n", run.decoder.width(), run.decoder.height());
  }
  const auto result = run.decoder.run(sink);
  if (run.out) std::fclose(run.out);
  if (result == progluma::Result::Done) assert(run.next == run.decoder.height());
  std::printf("run=%s rows=%d asks=%d max_gap=%u after=%u blocks=%u\n", name(result), run.next, run.asks, run.maxGap,
              run.stopAt > 0 ? run.decoder.blocks() - run.atYes : 0u, run.decoder.blocks());
  return 0;
}
